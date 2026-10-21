#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <builtin_interfaces/msg/time.hpp>
#include <rclcpp/rclcpp.hpp>

#include <capabilities2_msgs/msg/capability_event_code.hpp>
#include <capabilities2_msgs/msg/capability_event_stamped.hpp>
#include <capabilities2_msgs/srv/get_event_snapshot.hpp>

#include <fabric_base/monitor_base.hpp>
#include <fabric_base/utils/status_structs.hpp>

namespace fabric
{

class CapabilityEventMonitor : public MonitorBase
{
public:
  using GetEventSnapshot = capabilities2_msgs::srv::GetEventSnapshot;
  using GetEventSnapshotClient = rclcpp::Client<GetEventSnapshot>;
  using SystemStatus = fabric::status::SystemStatus;

  CapabilityEventMonitor() = default;
  ~CapabilityEventMonitor() override = default;

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "CapabilityEventMonitor");

    node_->declare_parameter<std::string>(plugin_name_ + ".service_name", "/capabilities/get_event_snapshot");
    node_->declare_parameter<int>(plugin_name_ + ".service_wait_timeout_sec", -1);
    node_->declare_parameter<int>(plugin_name_ + ".max_events", 100);

    service_name_ = node_->get_parameter(plugin_name_ + ".service_name").as_string();
    service_wait_timeout_sec_ = node_->get_parameter(plugin_name_ + ".service_wait_timeout_sec").as_int();
    const int max_events = node_->get_parameter(plugin_name_ + ".max_events").as_int();
    max_events_ = max_events > 0 ? static_cast<uint32_t>(max_events) : 100U;

    snapshot_client_ = node_->create_client<GetEventSnapshot>(service_name_);
  }

  void start() override
  {
    monitoring_active_ = true;
    monitor_thread_ = std::thread(&CapabilityEventMonitor::monitoring_function, this);
  }

  void stop() override
  {
    monitoring_active_ = false;
    buffer_cv_.notify_all();

    if (monitor_thread_.joinable()) {
      monitor_thread_.join();
    }
  }

  std::any getData() override
  {
    std::lock_guard<std::mutex> results_lock(results_mutex_);
    auto snapshot = results_;
    results_.clear();
    return snapshot;
  }

protected:
  void monitoring_function() override
  {
    const double safe_frequency = monitor_frequency_ > 0.0 ? monitor_frequency_ : 1.0;
    const auto period = std::chrono::milliseconds(static_cast<int>(1000.0 / safe_frequency));

    while (monitoring_active_) {
      try {
        if (!waitForService()) {
          std::this_thread::sleep_for(period);
          continue;
        }

        const auto response = requestSnapshot();
        if (response.latest_sequence > last_capability_event_sequence_) {
          last_capability_event_sequence_ = response.latest_sequence;
        }

        if (response.events.empty()) {
          std::this_thread::sleep_for(period);
          continue;
        }

        resetCapabilityCycleFlags();
        for (const auto& event_msg : response.events) {
          applyCapabilityEvent(event_msg);
        }

        auto statuses = buildCapabilitySystemStatuses();
        std::lock_guard<std::mutex> results_lock(results_mutex_);
        results_ = std::move(statuses);
      } catch (const std::exception& ex) {
        if (monitoring_active_) {
          RCLCPP_WARN(node_->get_logger(), "CapabilityEventMonitor polling failed: %s", ex.what());
        }
      }

      std::this_thread::sleep_for(period);
    }
  }

private:
  struct CapabilityRunRecordState
  {
    fabric::status::Time trigger_time;
    bool has_trigger{false};
    fabric::status::Time terminal_time;
    bool has_terminal{false};
    std::string outcome_label;
    std::string terminal_description;
    bool terminal_inferred{false};
  };

  struct CapabilityTelemetryState
  {
    std::string plan_id;
    fabric::status::CapabilityStatus status;
    std::vector<CapabilityRunRecordState> run_records;
  };

  struct CapabilityEventSnapshot
  {
    uint64_t latest_sequence{0};
    bool truncated{false};
    std::vector<capabilities2_msgs::msg::CapabilityEventStamped> events;
  };

  bool waitForService()
  {
    if (service_wait_timeout_sec_ < 0) {
      while (monitoring_active_ && !snapshot_client_->wait_for_service(std::chrono::seconds(1))) {
        RCLCPP_INFO(node_->get_logger(), "Waiting for capability snapshot service: %s", service_name_.c_str());
      }
      return monitoring_active_;
    }

    return snapshot_client_->wait_for_service(std::chrono::seconds(service_wait_timeout_sec_));
  }

  CapabilityEventSnapshot requestSnapshot()
  {
    auto request = std::make_shared<GetEventSnapshot::Request>();
    request->after_sequence = last_capability_event_sequence_;
    request->max_events = max_events_;

    CapabilityEventSnapshot snapshot;
    auto future = snapshot_client_->async_send_request(request);
    while (monitoring_active_ && rclcpp::ok(node_->get_node_base_interface()->get_context())) {
      if (future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
        const auto response = future.get();
        snapshot.latest_sequence = response->latest_sequence;
        snapshot.truncated = response->truncated;
        snapshot.events = response->events;
        return snapshot;
      }
    }
    snapshot_client_->remove_pending_request(future);
    return snapshot;
  }

  static std::string capabilityTrackingKey(
    const std::string& capability_name,
    const std::string& plan_id,
    const std::string& instance_id)
  {
    return plan_id + "/" + instance_id + "/" + capability_name;
  }

  static std::string outcomeLabelForEvent(const uint8_t code)
  {
    switch (code) {
      case capabilities2_msgs::msg::CapabilityEventCode::SUCCEEDED:
        return "succeeded";
      case capabilities2_msgs::msg::CapabilityEventCode::FAILED:
        return "failed";
      case capabilities2_msgs::msg::CapabilityEventCode::TERMINATED:
        return "cancelled";
      default:
        return "unknown";
    }
  }

  static CapabilityRunRecordState* findPendingRunRecord(CapabilityTelemetryState& state)
  {
    for (auto& run_record : state.run_records) {
      if (run_record.has_trigger && !run_record.has_terminal) {
        return &run_record;
      }
    }
    return nullptr;
  }

  static CapabilityRunRecordState* findLatestInferredTerminalRun(CapabilityTelemetryState& state)
  {
    for (auto it = state.run_records.rbegin(); it != state.run_records.rend(); ++it) {
      if (it->has_trigger && it->has_terminal && it->terminal_inferred) {
        return &(*it);
      }
    }
    return nullptr;
  }

  static void assignTerminal(
    CapabilityRunRecordState& run_record,
    const fabric::status::Time& terminal_time,
    const std::string& outcome_label,
    const std::string& terminal_description,
    bool inferred)
  {
    run_record.terminal_time = terminal_time;
    run_record.has_terminal = true;
    run_record.outcome_label = outcome_label;
    run_record.terminal_description = terminal_description;
    run_record.terminal_inferred = inferred;
  }

  static void recordTriggeredRun(
    CapabilityTelemetryState& state,
    const fabric::status::Time& trigger_time)
  {
    CapabilityRunRecordState run_record;
    run_record.trigger_time = trigger_time;
    run_record.has_trigger = true;
    state.run_records.push_back(run_record);
  }

  static void recordTerminalRun(
    CapabilityTelemetryState& state,
    const fabric::status::Time& terminal_time,
    uint8_t code,
    const std::string& description)
  {
    auto* run_record = findPendingRunRecord(state);
    if (run_record != nullptr) {
      assignTerminal(*run_record, terminal_time, outcomeLabelForEvent(code), description, false);
      return;
    }

    run_record = findLatestInferredTerminalRun(state);
    if (run_record != nullptr) {
      assignTerminal(*run_record, terminal_time, outcomeLabelForEvent(code), description, false);
      return;
    }

    CapabilityRunRecordState orphan_terminal;
    assignTerminal(orphan_terminal, terminal_time, outcomeLabelForEvent(code), description, false);
    state.run_records.push_back(orphan_terminal);
  }

  static void inferTerminalRun(
    CapabilityTelemetryState& state,
    const fabric::status::Time& terminal_time,
    const std::string& outcome_label,
    const std::string& terminal_description)
  {
    auto* run_record = findPendingRunRecord(state);
    if (run_record == nullptr) {
      return;
    }

    assignTerminal(*run_record, terminal_time, outcome_label, terminal_description, true);
  }

  static void refreshCapabilityRunEvidence(CapabilityTelemetryState& state)
  {
    auto& run_evidence = state.status.run_evidence;
    run_evidence.runs_started = 0;
    run_evidence.runs_completed = 0;
    run_evidence.runs_succeeded = 0;
    run_evidence.runs_failed = 0;
    run_evidence.runs_aborted = 0;
    run_evidence.runs_timed_out = 0;
    run_evidence.last_outcome_time = fabric::status::Time();
    run_evidence.last_outcome_label.clear();

    for (const auto& run_record_state : state.run_records) {
      if (run_record_state.has_trigger) {
        ++run_evidence.runs_started;
      }

      if (!(run_record_state.has_trigger && run_record_state.has_terminal)) {
        continue;
      }

      ++run_evidence.runs_completed;
      if (run_record_state.outcome_label == "succeeded") {
        ++run_evidence.runs_succeeded;
      } else if (run_record_state.outcome_label == "failed") {
        ++run_evidence.runs_failed;
      } else if (run_record_state.outcome_label == "timed_out") {
        ++run_evidence.runs_timed_out;
      } else {
        ++run_evidence.runs_aborted;
      }

      if ((run_record_state.terminal_time.sec > run_evidence.last_outcome_time.sec) ||
        (run_record_state.terminal_time.sec == run_evidence.last_outcome_time.sec &&
        run_record_state.terminal_time.nanosec > run_evidence.last_outcome_time.nanosec))
      {
        run_evidence.last_outcome_time = run_record_state.terminal_time;
        run_evidence.last_outcome_label = run_record_state.outcome_label;
      }
    }
  }

  static fabric::status::Time toStatusTime(const builtin_interfaces::msg::Time& time)
  {
    return fabric::status::Time{time.sec, time.nanosec};
  }

  std::vector<SystemStatus> buildCapabilitySystemStatuses() const
  {
    std::unordered_map<std::string, SystemStatus> statuses_by_plan;

    for (const auto& entry : capability_telemetry_by_key_) {
      const auto& state = entry.second;
      auto& status = statuses_by_plan[state.plan_id];
      status.plan_id = state.plan_id;
      status.header.stamp = toStatusTime(node_->now());
      status.capabilities.push_back(state.status);
    }

    std::vector<SystemStatus> statuses;
    statuses.reserve(statuses_by_plan.size());
    for (auto& entry : statuses_by_plan) {
      statuses.push_back(std::move(entry.second));
    }

    return statuses;
  }

  void resetCapabilityCycleFlags()
  {
    for (auto& entry : capability_telemetry_by_key_) {
      entry.second.status.lifecycle.started_this_cycle = false;
      entry.second.status.lifecycle.stopped_this_cycle = false;
    }
  }

  void applyCapabilityEvent(const capabilities2_msgs::msg::CapabilityEventStamped& event_msg)
  {
    const auto& source = event_msg.event.connection.source;
    if (source.capability.empty()) {
      return;
    }

    const std::string plan_id = event_msg.event.connection.ownership_id;
    const std::string instance_id =
      source.instance_id.empty() ? event_msg.event.trigger_id : source.instance_id;
    const std::string tracking_key = capabilityTrackingKey(source.capability, plan_id, instance_id);

    CapabilityTelemetryState& state = capability_telemetry_by_key_[tracking_key];
    state.plan_id = plan_id;
    state.status.capability_name = source.capability;
    state.status.instance_id = instance_id;
    state.status.has_lifecycle = true;
    state.status.has_run_evidence = true;

    auto& lifecycle = state.status.lifecycle;
    const auto stamp = toStatusTime(event_msg.header.stamp);
    const uint8_t code = event_msg.event.code.code;
    const std::string description = event_msg.event.description.empty() ?
      std::string("event code ") + std::to_string(static_cast<int>(code)) :
      event_msg.event.description;

    switch (code) {
      case capabilities2_msgs::msg::CapabilityEventCode::STARTED:
        lifecycle.has_start = true;
        lifecycle.started_this_cycle = true;
        lifecycle.is_active = true;
        lifecycle.last_start_time = stamp;
        break;
      case capabilities2_msgs::msg::CapabilityEventCode::STOPPED:
        lifecycle.has_stop = true;
        lifecycle.stopped_this_cycle = true;
        lifecycle.is_active = false;
        lifecycle.last_stop_time = stamp;
        inferTerminalRun(state, stamp, "cancelled", "inferred terminal from STOPPED: " + description);
        break;
      case capabilities2_msgs::msg::CapabilityEventCode::FAILED:
      case capabilities2_msgs::msg::CapabilityEventCode::SUCCEEDED:
      case capabilities2_msgs::msg::CapabilityEventCode::TERMINATED:
        lifecycle.is_active = false;
        recordTerminalRun(state, stamp, code, description);
        break;
      case capabilities2_msgs::msg::CapabilityEventCode::DISCONNECTED:
        lifecycle.is_active = false;
        inferTerminalRun(state, stamp, "cancelled", "inferred terminal from DISCONNECTED: " + description);
        break;
      case capabilities2_msgs::msg::CapabilityEventCode::TRIGGERED:
        recordTriggeredRun(state, stamp);
        break;
      case capabilities2_msgs::msg::CapabilityEventCode::ERROR:
        lifecycle.is_active = false;
        inferTerminalRun(state, stamp, "failed", "inferred terminal from ERROR: " + description);
        break;
      case capabilities2_msgs::msg::CapabilityEventCode::UNDEFINED:
        lifecycle.is_active = false;
        inferTerminalRun(state, stamp, "unknown", "inferred terminal from UNDEFINED: " + description);
        break;
      default:
        break;
    }

    refreshCapabilityRunEvidence(state);
  }

  std::string service_name_;
  int service_wait_timeout_sec_{-1};
  uint32_t max_events_{100};
  uint64_t last_capability_event_sequence_{0};
  GetEventSnapshotClient::SharedPtr snapshot_client_;
  std::unordered_map<std::string, CapabilityTelemetryState> capability_telemetry_by_key_;
  std::vector<SystemStatus> results_;
  std::mutex results_mutex_;
};

}  // namespace fabric