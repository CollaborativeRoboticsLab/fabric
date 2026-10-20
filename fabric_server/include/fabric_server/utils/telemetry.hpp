#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <capabilities2_msgs/msg/launch_resource_list.hpp>
#include <fabric_base/utils/monitoring_structs.hpp>
#include <fabric_base/utils/status_structs.hpp>
#include <fabric_server/utils/status.hpp>

namespace fabric::server::telemetry
{
using SystemStatus = ::fabric::status::SystemStatus;
using TelemetryRecord = ::fabric::server::status::TelemetryRecord;

inline uint8_t labelForMonitoringStatus(const ::fabric::monitor::MonitoringStatus status)
{
  switch (status) {
    case ::fabric::monitor::MonitoringStatus::SUSPICIOUS:
      return 1;
    case ::fabric::monitor::MonitoringStatus::FAULTY:
      return 2;
    case ::fabric::monitor::MonitoringStatus::NORMAL:
    default:
      return 0;
  }
}

inline std::string capabilityExecutionKey(
  const std::string & plan_id,
  const ::fabric::status::CapabilityStatus & capability)
{
  return plan_id + "/" + capability.instance_id + "/" + capability.capability_name;
}

class Aggregator
{
public:
  void ingestResourceSnapshots(
    const std::vector<capabilities2_msgs::msg::LaunchResourceList> & snapshots)
  {
    if (snapshots.empty()) {
      return;
    }

    const auto & snapshot = snapshots.back();
    ::fabric::status::ResourceUsage usage;
    usage.header.stamp.sec = snapshot.header.stamp.sec;
    usage.header.stamp.nanosec = snapshot.header.stamp.nanosec;
    usage.header.frame_id = snapshot.header.frame_id;
    usage.active_launch_count = snapshot.active_launch_count;
    usage.main_process_count = snapshot.main_process_count;
    usage.child_process_count = snapshot.child_process_count;
    usage.resource_count = snapshot.resource_count;
    usage.total_cpu_usage_percent = snapshot.total_cpu_usage_percent;
    usage.total_peak_sampled_cpu_usage_percent = snapshot.total_peak_sampled_cpu_usage_percent;
    usage.total_time_above_80pct_peak_sampled_cpu_sec =
      snapshot.total_time_above_80pct_peak_sampled_cpu_sec;
    usage.total_memory_rss_bytes = snapshot.total_memory_rss_bytes;
    usage.total_peak_memory_rss_bytes = snapshot.total_peak_memory_rss_bytes;
    usage.total_time_above_90pct_peak_rss_sec = snapshot.total_time_above_90pct_peak_rss_sec;
    usage.total_memory_vms_bytes = snapshot.total_memory_vms_bytes;
    usage.total_peak_memory_vms_bytes = snapshot.total_peak_memory_vms_bytes;
    usage.total_time_above_90pct_peak_vms_sec = snapshot.total_time_above_90pct_peak_vms_sec;
    usage.resources.reserve(snapshot.resources.size());
    for (const auto & resource : snapshot.resources) {
      usage.resources.push_back(::fabric::status::ProcessResourceUsage{
          resource.key, resource.reference_name, resource.launch_id, resource.state,
          resource.resolved_launch_file_path, resource.process_name, resource.pid,
          resource.parent_pid, resource.is_main_process, resource.launch_uptime_sec,
          resource.process_uptime_sec, resource.cpu_usage_percent,
          resource.peak_sampled_cpu_usage_percent, resource.time_above_80pct_peak_sampled_cpu_sec,
          resource.memory_rss_bytes, resource.peak_memory_rss_bytes,
          resource.time_above_90pct_peak_rss_sec, resource.memory_vms_bytes,
          resource.peak_memory_vms_bytes, resource.time_above_90pct_peak_vms_sec});
    }

    std::lock_guard<std::mutex> lock(resource_usage_mutex_);
    resource_usage_ = std::move(usage);
    has_resource_usage_ = true;
  }

  void configureRetentionSeconds(const int retention_seconds)
  {
    retention_seconds_ = retention_seconds > 0 ? retention_seconds : 300;
  }

  void rememberTelemetryRecord(TelemetryRecord record, const int64_t now_ns)
  {
    std::lock_guard<std::mutex> lock(telemetry_records_mutex_);
    telemetry_records_.push_back(std::move(record));
    pruneTelemetryRecordsLocked(now_ns);
  }

  void ingestMonitorStatuses(
    const std::string & monitor_name,
    const std::map<std::string, ::fabric::monitor::ComponentStatus> & statuses,
    const int64_t now_ns)
  {
    for (const auto & entry : statuses) {
      const auto label = labelForMonitoringStatus(entry.second.status);
      if (label == 0 || entry.second.description.empty()) {
        continue;
      }

      rememberTelemetryRecord(TelemetryRecord{
          entry.second.timestamp,
          entry.first,
          monitor_name + ": " + entry.second.description,
          label,
      }, now_ns);
    }
  }

  std::vector<SystemStatus> ingestSystemStatuses(
    std::vector<SystemStatus> statuses,
    const int64_t now_ns,
    const bool enable_battery_monitoring,
    const double battery_percentage)
  {
    std::vector<SystemStatus> capability_statuses;
    capability_statuses.reserve(statuses.size());

    for (auto & status : statuses) {
      if (status.capabilities.empty()) {
        cacheExecutionSystemStatus(std::move(status));
        continue;
      }

      enrichCapabilitySystemStatus(status, now_ns, enable_battery_monitoring, battery_percentage);
      maybeMergeExecutionStatus(status);
      {
        std::lock_guard<std::mutex> lock(resource_usage_mutex_);
        status.has_resource_usage = has_resource_usage_;
        status.resource_usage = resource_usage_;
      }
      capability_statuses.push_back(std::move(status));
    }

    return capability_statuses;
  }

  void maybeMergeExecutionStatus(SystemStatus & status)
  {
    if (status.plan_id.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(execution_status_mutex_);
    const auto it = execution_status_by_plan_.find(status.plan_id);
    if (it == execution_status_by_plan_.end()) {
      return;
    }

    ::fabric::server::status::mergeExecutionStatus(status, it->second);
  }

  void eraseExecutionStatus(const std::string & plan_id)
  {
    if (plan_id.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(execution_status_mutex_);
    execution_status_by_plan_.erase(plan_id);
  }

  void eraseCapabilityBatteryUsage(const std::string & plan_id)
  {
    if (plan_id.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(capability_battery_mutex_);
    for (auto it = capability_battery_usage_by_key_.begin();
      it != capability_battery_usage_by_key_.end(); )
    {
      if (it->first.rfind(plan_id + "/", 0) == 0) {
        it = capability_battery_usage_by_key_.erase(it);
      } else {
        ++it;
      }
    }
  }

private:
  void cacheExecutionSystemStatus(SystemStatus status)
  {
    if (status.plan_id.empty()) {
      return;
    }

    std::lock_guard<std::mutex> lock(execution_status_mutex_);
    execution_status_by_plan_[status.plan_id] = std::move(status);
  }

  void enrichCapabilitySystemStatus(
    SystemStatus & status,
    const int64_t now_ns,
    const bool enable_battery_monitoring,
    const double battery_percentage)
  {
    std::deque<TelemetryRecord> telemetry_records;
    {
      std::lock_guard<std::mutex> lock(telemetry_records_mutex_);
      pruneTelemetryRecordsLocked(now_ns);
      telemetry_records = telemetry_records_;
    }

    for (auto & capability : status.capabilities) {
      capability.related_nodes.clear();
      capability.log_records.clear();
      ::fabric::server::status::correlateTelemetryRecords(capability, telemetry_records, now_ns);
      maybeUpdateCapabilityBatteryUsage(
        status.plan_id, capability, enable_battery_monitoring, battery_percentage);
    }
  }

  void maybeUpdateCapabilityBatteryUsage(
    const std::string & plan_id,
    ::fabric::status::CapabilityStatus & capability,
    const bool enable_battery_monitoring,
    const double battery_percentage)
  {
    if (!enable_battery_monitoring) {
      capability.has_battery_usage = false;
      return;
    }

    capability.has_battery_usage = true;
    std::lock_guard<std::mutex> lock(capability_battery_mutex_);
    auto & usage = capability_battery_usage_by_key_[capabilityExecutionKey(plan_id, capability)];
    ::fabric::server::status::updateCapabilityBatteryUsage(usage, capability, battery_percentage);
    capability.battery_usage = usage;
  }

  void pruneTelemetryRecordsLocked(const int64_t now_ns)
  {
    const int64_t cutoff = now_ns - static_cast<int64_t>(retention_seconds_) * 1000000000LL;

    while (!telemetry_records_.empty() &&
      ::fabric::server::status::timeToNanoseconds(telemetry_records_.front().timestamp) < cutoff)
    {
      telemetry_records_.pop_front();
    }
  }

  int retention_seconds_{300};
  std::mutex resource_usage_mutex_;
  bool has_resource_usage_{false};
  ::fabric::status::ResourceUsage resource_usage_;
  std::mutex telemetry_records_mutex_;
  std::deque<TelemetryRecord> telemetry_records_;
  std::mutex execution_status_mutex_;
  std::unordered_map<std::string, SystemStatus> execution_status_by_plan_;
  std::mutex capability_battery_mutex_;
  std::unordered_map<std::string, ::fabric::status::BatteryUsage> capability_battery_usage_by_key_;
};
}  // namespace fabric::server::telemetry
