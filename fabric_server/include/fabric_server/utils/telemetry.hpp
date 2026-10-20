#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fabric_base/utils/monitoring_structs.hpp>
#include <fabric_base/utils/status_structs.hpp>
#include <fabric_server/utils/status.hpp>

namespace fabric::server::telemetry
{
using SystemStatus = ::fabric::status::SystemStatus;
using TelemetryRecord = ::fabric::server::status::TelemetryRecord;

inline uint8_t labelForMonitoringStatus(const ::fabric::MonitoringStatus status)
{
  switch (status) {
    case ::fabric::MonitoringStatus::SUSPICIOUS:
      return 1;
    case ::fabric::MonitoringStatus::FAULTY:
      return 2;
    case ::fabric::MonitoringStatus::NORMAL:
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
    const std::map<std::string, ::fabric::ComponentStatus> & statuses,
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
  std::mutex telemetry_records_mutex_;
  std::deque<TelemetryRecord> telemetry_records_;
  std::mutex execution_status_mutex_;
  std::unordered_map<std::string, SystemStatus> execution_status_by_plan_;
  std::mutex capability_battery_mutex_;
  std::unordered_map<std::string, ::fabric::status::BatteryUsage> capability_battery_usage_by_key_;
};
}  // namespace fabric::server::telemetry
