#pragma once

#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

#include <fabric_base/utils/plan_structs.hpp>
#include <fabric_base/utils/status_structs.hpp>
#include <fabric_msgs/msg/fabric_status.hpp>

namespace fabric::server::status
{
struct TelemetryRecord
{
  ::fabric::status::Time timestamp;
  std::string source;
  std::string description;
  uint8_t label{0};
};

inline int64_t timeToNanoseconds(const ::fabric::status::Time & time)
{
  return static_cast<int64_t>(time.sec) * 1000000000LL + static_cast<int64_t>(time.nanosec);
}

inline void enqueueSystemStatus(
  std::mutex & mutex,
  std::condition_variable & cv,
  std::deque<::fabric::status::SystemStatus> & records,
  ::fabric::status::SystemStatus status)
{
  {
    std::lock_guard<std::mutex> lock(mutex);
    records.push_back(std::move(status));
  }
  cv.notify_all();
}

inline bool waitPopSystemStatus(
  std::mutex & mutex,
  std::condition_variable & cv,
  std::deque<::fabric::status::SystemStatus> & records,
  const std::atomic<bool> & shutting_down,
  ::fabric::status::SystemStatus & status)
{
  std::unique_lock<std::mutex> lock(mutex);
  cv.wait(lock, [&records, &shutting_down]() {
      return !records.empty() || shutting_down.load();
  });

  if (shutting_down.load()) {
    return false;
  }

  status = std::move(records.front());
  records.pop_front();
  return true;
}

inline std::string summarizeSystemStatus(const ::fabric::status::SystemStatus & status)
{
  std::ostringstream out;
  out << "plan_id=" << status.plan_id
      << " planning_request_id=" << status.planning_request_id
      << " candidate_plan_id=" << status.candidate_plan_id
      << " graph_hash=" << status.graph_hash
      << " capabilities=" << status.capabilities.size();
  return out.str();
}

inline bool shouldExposeRejectedList(const ::fabric::plan::Plan & plan)
{
  return plan.status == ::fabric::plan::Status::VALIDATION_FAILED ||
         plan.status == ::fabric::plan::Status::PARSE_FAILED;
}

inline fabric_msgs::msg::FabricStatus toFabricStatusMsg(
  const ::fabric::plan::Plan & plan)
{
  fabric_msgs::msg::FabricStatus status_msg;

  status_msg.plan_id = plan.plan_id;
  status_msg.metadata.planning_request_id = plan.metadata.planning_request_id;
  status_msg.metadata.candidate_plan_id = plan.metadata.candidate_plan_id;
  status_msg.metadata.graph_hash = plan.metadata.graph_hash;

  switch (plan.status) {
    case ::fabric::plan::Status::UNKNOWN:
      status_msg.code = fabric_msgs::msg::FabricStatus::UNKNOWN;
      break;
    case ::fabric::plan::Status::QUEUED:
      status_msg.code = fabric_msgs::msg::FabricStatus::QUEUED;
      break;
    case ::fabric::plan::Status::PARSING:
      status_msg.code = fabric_msgs::msg::FabricStatus::PARSING;
      break;
    case ::fabric::plan::Status::PARSE_FAILED:
      status_msg.code = fabric_msgs::msg::FabricStatus::PARSE_FAILED;
      break;
    case ::fabric::plan::Status::VALIDATING:
      status_msg.code = fabric_msgs::msg::FabricStatus::VALIDATING;
      break;
    case ::fabric::plan::Status::VALIDATION_FAILED:
      status_msg.code = fabric_msgs::msg::FabricStatus::VALIDATION_FAILED;
      break;
    case ::fabric::plan::Status::BONDING:
      status_msg.code = fabric_msgs::msg::FabricStatus::BONDING;
      break;
    case ::fabric::plan::Status::BOND_FAILED:
      status_msg.code = fabric_msgs::msg::FabricStatus::BOND_FAILED;
      break;
    case ::fabric::plan::Status::CAPABILITY_STARTING:
      status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_STARTING;
      break;
    case ::fabric::plan::Status::CAPABILITY_START_FAILED:
      status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_START_FAILED;
      break;
    case ::fabric::plan::Status::CAPABILITY_CONNECTING:
      status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_CONNECTING;
      break;
    case ::fabric::plan::Status::CAPABILITY_CONNECT_FAILED:
      status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_CONNECT_FAILED;
      break;
    case ::fabric::plan::Status::RUNNING:
      status_msg.code = fabric_msgs::msg::FabricStatus::RUNNING;
      break;
    case ::fabric::plan::Status::COMPLETED:
      status_msg.code = fabric_msgs::msg::FabricStatus::COMPLETED;
      break;
    case ::fabric::plan::Status::CANCELLED:
      status_msg.code = fabric_msgs::msg::FabricStatus::CANCELLED;
      break;
    default:
      status_msg.code = fabric_msgs::msg::FabricStatus::UNKNOWN;
      break;
  }

  return status_msg;
}

inline void mergeExecutionStatus(
  ::fabric::status::SystemStatus & status,
  const ::fabric::status::SystemStatus & execution_status)
{
  if (execution_status.plan_id.empty()) {
    return;
  }

  status.plan_id = execution_status.plan_id;
  status.planning_request_id = execution_status.planning_request_id;
  status.candidate_plan_id = execution_status.candidate_plan_id;
  status.graph_hash = execution_status.graph_hash;
  status.bond_id = execution_status.bond_id;
  status.capability_plan_state = execution_status.capability_plan_state;
  status.header.stamp = execution_status.header.stamp;
}

enum class ShutdownSafetyState
{
  SAFE_TO_KILL,
  DO_NOT_KILL,
  UNKNOWN,
};

struct ShutdownSafetySummary
{
  ShutdownSafetyState state{ShutdownSafetyState::UNKNOWN};
  size_t tracked_capabilities{0};
  size_t active_capabilities{0};
  size_t incomplete_capabilities{0};
  size_t terminal_capabilities{0};
  std::string reason;
};

inline bool hasPublishableCapabilityEvidence(const ::fabric::status::CapabilityStatus & capability)
{
  const auto & lifecycle = capability.lifecycle;
  const auto & run = capability.run_evidence;

  return lifecycle.is_active || lifecycle.has_start || lifecycle.has_stop ||
         lifecycle.started_this_cycle || lifecycle.stopped_this_cycle ||
         run.runs_started > 0 || run.runs_completed > 0 || run.runs_succeeded > 0 ||
         run.runs_failed > 0 || run.runs_aborted > 0 || run.runs_timed_out > 0 ||
         !run.last_outcome_label.empty();
}

inline bool hasTerminalCapabilityEvidence(const ::fabric::status::CapabilityStatus & capability)
{
  const auto & lifecycle = capability.lifecycle;
  const auto & run = capability.run_evidence;

  return lifecycle.has_stop || lifecycle.stopped_this_cycle || run.runs_completed > 0 ||
         run.runs_succeeded > 0 || run.runs_failed > 0 || run.runs_aborted > 0 ||
         run.runs_timed_out > 0 || !run.last_outcome_label.empty();
}

inline bool hasIncompleteCapabilityEvidence(const ::fabric::status::CapabilityStatus & capability)
{
  const auto & lifecycle = capability.lifecycle;
  const auto & run = capability.run_evidence;

  if (lifecycle.is_active) {
    return true;
  }

  if (run.runs_started > run.runs_completed) {
    return true;
  }

  return (lifecycle.has_start || lifecycle.started_this_cycle) &&
         !hasTerminalCapabilityEvidence(capability);
}

inline ShutdownSafetySummary assessShutdownSafety(const ::fabric::status::SystemStatus & status)
{
  ShutdownSafetySummary summary;

  for (const auto & capability : status.capabilities) {
    if (!hasPublishableCapabilityEvidence(capability)) {
      continue;
    }

    ++summary.tracked_capabilities;
    if (capability.has_lifecycle && capability.lifecycle.is_active) {
      ++summary.active_capabilities;
    }
    if (hasIncompleteCapabilityEvidence(capability)) {
      ++summary.incomplete_capabilities;
    }
    if (hasTerminalCapabilityEvidence(capability)) {
      ++summary.terminal_capabilities;
    }
  }

  if (summary.tracked_capabilities == 0) {
    summary.state = ShutdownSafetyState::UNKNOWN;
    summary.reason = "no capability evidence observed yet";
    return summary;
  }

  if (summary.active_capabilities > 0) {
    summary.state = ShutdownSafetyState::DO_NOT_KILL;
    summary.reason = "one or more capabilities are still active";
    return summary;
  }

  if (summary.incomplete_capabilities > 0) {
    summary.state = ShutdownSafetyState::DO_NOT_KILL;
    summary.reason = "one or more capabilities have started without terminal evidence";
    return summary;
  }

  summary.state = ShutdownSafetyState::SAFE_TO_KILL;
  summary.reason = "all tracked capabilities are inactive and terminal or stopped";
  return summary;
}

inline const char * shutdownSafetyLabel(const ShutdownSafetyState state)
{
  switch (state) {
    case ShutdownSafetyState::SAFE_TO_KILL:
      return "SAFE_TO_KILL";
    case ShutdownSafetyState::DO_NOT_KILL:
      return "DO_NOT_KILL";
    case ShutdownSafetyState::UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

inline bool telemetryRecordAppliesToCapability(
  const TelemetryRecord & record,
  const ::fabric::status::CapabilityStatus & capability,
  const int64_t now_ns)
{
  if (!capability.has_lifecycle || !capability.lifecycle.has_start) {
    return false;
  }

  const int64_t record_ns = timeToNanoseconds(record.timestamp);
  const int64_t start_ns = timeToNanoseconds(capability.lifecycle.last_start_time);
  const int64_t end_ns = capability.lifecycle.has_stop ?
    timeToNanoseconds(capability.lifecycle.last_stop_time) : now_ns;

  return record_ns >= start_ns && record_ns <= end_ns;
}

inline void correlateTelemetryRecords(
  ::fabric::status::CapabilityStatus & capability,
  const std::deque<TelemetryRecord> & telemetry_records,
  const int64_t now_ns)
{
  std::unordered_map<std::string, ::fabric::status::FabricNodeLifecycle> related_nodes;

  for (const auto & record : telemetry_records) {
    if (!telemetryRecordAppliesToCapability(record, capability, now_ns)) {
      continue;
    }

    capability.log_records.push_back(::fabric::status::LogRecord{
        capability.capability_name,
        record.timestamp,
        record.source,
        record.description,
        record.label,
    });

    auto & node = related_nodes[record.source];
    node.name = record.source;
    if (!node.has_first ||
      timeToNanoseconds(record.timestamp) < timeToNanoseconds(node.first_seen))
    {
      node.first_seen = record.timestamp;
      node.has_first = true;
    }
    if (!node.has_last ||
      timeToNanoseconds(record.timestamp) > timeToNanoseconds(node.last_seen))
    {
      node.last_seen = record.timestamp;
      node.has_last = true;
    }
  }

  for (const auto & entry : related_nodes) {
    capability.related_nodes.push_back(entry.second);
  }
}

inline void updateCapabilityBatteryUsage(
  ::fabric::status::BatteryUsage & usage,
  const ::fabric::status::CapabilityStatus & capability,
  const double battery_percentage)
{
  if (!capability.has_lifecycle) {
    return;
  }

  const auto & lifecycle = capability.lifecycle;
  if (lifecycle.started_this_cycle) {
    usage.last_start_percentage = battery_percentage;
  }

  if (!lifecycle.stopped_this_cycle) {
    return;
  }

  usage.last_stop_percentage = battery_percentage;

  ::fabric::status::BatteryRunInterval interval;
  interval.start_time = lifecycle.last_start_time;
  interval.stop_time = lifecycle.last_stop_time;
  interval.start_percentage = usage.last_start_percentage;
  interval.stop_percentage = usage.last_stop_percentage;

  if (usage.last_start_percentage >= 0.0 && usage.last_stop_percentage >= 0.0) {
    const double delta = usage.last_start_percentage - usage.last_stop_percentage;
    interval.delta_percentage = delta;
    usage.last_delta_percentage = delta;
    if (delta > 0.0) {
      usage.cumulative_delta_percentage += delta;
    }
    usage.runs += 1;
  }

  usage.run_intervals.push_back(interval);
}
}  // namespace fabric::server::status
