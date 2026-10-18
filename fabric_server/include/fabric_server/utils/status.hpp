#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>

#include <fabric_base/utils/plan_structs.hpp>
#include <fabric_base/utils/status_structs.hpp>
#include <fabric_msgs/msg/fabric_status.hpp>
#include <supervisor_msgs/msg/system_status.hpp>

namespace fabric::server::status
{
inline ::fabric::status::Time toFabricTime(const builtin_interfaces::msg::Time & time)
{
  return ::fabric::status::Time{time.sec, time.nanosec};
}

inline ::fabric::status::ExecutionLifecycle toExecutionLifecycle(
  const supervisor_msgs::msg::CapabilityLifecycle & lifecycle)
{
  ::fabric::status::ExecutionLifecycle converted;
  converted.last_start_time = toFabricTime(lifecycle.last_start_time);
  converted.last_stop_time = toFabricTime(lifecycle.last_stop_time);
  converted.has_start = lifecycle.has_start;
  converted.has_stop = lifecycle.has_stop;
  converted.is_active = lifecycle.is_active;
  converted.started_this_cycle = lifecycle.started_this_cycle;
  converted.stopped_this_cycle = lifecycle.stopped_this_cycle;
  return converted;
}

inline ::fabric::status::BatteryUsage toBatteryUsage(
  const supervisor_msgs::msg::CapabilityBatteryUsage & battery)
{
  ::fabric::status::BatteryUsage converted;
  converted.last_start_percentage = battery.last_start_percentage;
  converted.last_stop_percentage = battery.last_stop_percentage;
  converted.last_delta_percentage = battery.last_delta_percentage;
  converted.cumulative_delta_percentage = battery.cumulative_delta_percentage;
  converted.runs = battery.runs;
  for (const auto & interval : battery.run_intervals) {
    ::fabric::status::BatteryRunInterval converted_interval;
    converted_interval.start_time = toFabricTime(interval.start_time);
    converted_interval.stop_time = toFabricTime(interval.stop_time);
    converted_interval.start_percentage = interval.start_percentage;
    converted_interval.stop_percentage = interval.stop_percentage;
    converted_interval.delta_percentage = interval.delta_percentage;
    converted.run_intervals.push_back(converted_interval);
  }
  return converted;
}

inline ::fabric::status::RunEvidence toRunEvidence(
  const supervisor_msgs::msg::CapabilityRunEvidence & run_evidence)
{
  ::fabric::status::RunEvidence converted;
  converted.runs_started = run_evidence.runs_started;
  converted.runs_completed = run_evidence.runs_completed;
  converted.runs_succeeded = run_evidence.runs_succeeded;
  converted.runs_failed = run_evidence.runs_failed;
  converted.runs_aborted = run_evidence.runs_aborted;
  converted.runs_timed_out = run_evidence.runs_timed_out;
  converted.last_outcome_time = toFabricTime(run_evidence.last_outcome_time);
  converted.last_outcome_label = run_evidence.last_outcome_label;
  return converted;
}

inline ::fabric::status::SystemStatus toSystemStatus(const supervisor_msgs::msg::SystemStatus & msg)
{
  ::fabric::status::SystemStatus converted;
  converted.header.stamp = toFabricTime(msg.header.stamp);
  converted.header.frame_id = msg.header.frame_id;
  converted.plan_id = msg.plan_id;
  converted.planning_request_id = msg.planning_request_id;
  converted.candidate_plan_id = msg.candidate_plan_id;
  converted.graph_hash = msg.graph_hash;
  converted.capability_plan_state = msg.fabric_status;

  for (const auto & capability_status : msg.statuses) {
    ::fabric::status::CapabilityStatus capability;
    capability.capability_name = capability_status.capability_name;
    capability.instance_id = capability_status.instance_id;
    capability.has_lifecycle = capability_status.has_lifecycle;
    capability.has_battery_usage = capability_status.has_battery_usage;
    capability.has_run_evidence = capability_status.has_run_evidence;

    if (capability.has_lifecycle) {
      capability.lifecycle = toExecutionLifecycle(capability_status.lifecycle);
    }
    if (capability.has_battery_usage) {
      capability.battery_usage = toBatteryUsage(capability_status.battery_usage);
    }
    if (capability.has_run_evidence) {
      capability.run_evidence = toRunEvidence(capability_status.run_evidence);
    }

    for (const auto & node : capability_status.related_nodes) {
      ::fabric::status::FabricNodeLifecycle converted_node;
      converted_node.name = node.name;
      converted_node.first_seen = toFabricTime(node.first_seen);
      converted_node.last_seen = toFabricTime(node.last_seen);
      converted_node.has_first = node.has_first;
      converted_node.has_last = node.has_last;
      capability.related_nodes.push_back(converted_node);
    }

    for (const auto & log : capability_status.logs) {
      ::fabric::status::LogRecord converted_log;
      converted_log.capability_name = log.capability_name;
      converted_log.timestamp = toFabricTime(log.timestamp);
      converted_log.source = log.source;
      converted_log.description = log.description;
      converted_log.label = log.label;
      capability.log_records.push_back(converted_log);
    }

    converted.capabilities.push_back(capability);
  }

  return converted;
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

inline std::string summarizeStopGuidance(const supervisor_msgs::msg::SystemStatus & msg)
{
  bool saw_capability_evidence = false;
  bool any_active = false;
  bool any_incomplete = false;

  for (const auto & capability_status : msg.statuses) {
    const bool has_lifecycle = capability_status.has_lifecycle;
    const bool has_run_evidence = capability_status.has_run_evidence;
    if (!has_lifecycle && !has_run_evidence) {
      continue;
    }

    saw_capability_evidence = true;
    any_active = any_active || (has_lifecycle && capability_status.lifecycle.is_active);
    any_incomplete = any_incomplete ||
      (has_run_evidence &&
      capability_status.run_evidence.runs_started >
      capability_status.run_evidence.runs_completed) ||
      (has_lifecycle && capability_status.lifecycle.has_start &&
      !capability_status.lifecycle.has_stop &&
      (!has_run_evidence || capability_status.run_evidence.runs_completed == 0));
  }

  if (any_active || any_incomplete) {
    return "DO NOT STOP";
  }
  if (saw_capability_evidence) {
    return "SAFE TO STOP";
  }
  return "UNKNOWN";
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
}  // namespace fabric::server::status
