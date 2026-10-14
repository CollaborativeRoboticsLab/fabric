#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fabric
{

namespace status
{
struct Time
{
  int32_t sec = 0;
  uint32_t nanosec = 0;
};

struct Header
{
  uint32_t seq = 0;
  Time stamp;
  std::string frame_id;
};

struct BatteryRunInterval
{
  Time start_time;
  Time stop_time;
  double start_percentage = 0.0;
  double stop_percentage = 0.0;
  double delta_percentage = 0.0;
};

struct BatteryUsage
{
  double last_start_percentage = 0.0;
  double last_stop_percentage = 0.0;
  double last_delta_percentage = 0.0;
  double cumulative_delta_percentage = 0.0;
  int32_t runs = 0;
  std::vector<BatteryRunInterval> run_intervals;
};

struct RunEvidence
{
  int32_t runs_started = 0;
  int32_t runs_completed = 0;
  int32_t runs_succeeded = 0;
  int32_t runs_failed = 0;
  int32_t runs_aborted = 0;
  int32_t runs_timed_out = 0;
  Time last_outcome_time;
  std::string last_outcome_label;
};

struct ExecutionLifecycle
{
  Time last_start_time;
  Time last_stop_time;
  bool has_start = false;
  bool has_stop = false;
  bool is_active = false;
  bool started_this_cycle = false;
  bool stopped_this_cycle = false;
};

struct FabricNodeLifecycle
{
  std::string name;
  Time first_seen;
  Time last_seen;
  bool has_first = false;
  bool has_last = false;
};

struct LogRecord
{
  std::string capability_name;
  Time timestamp;
  std::string source;
  std::string description;
  uint8_t label = 0;
};

struct CapabilityStatus
{
  std::string capability_name;
  std::string bond_id;
  std::string instance_id;
  bool has_lifecycle = false;
  bool has_battery_usage = false;
  bool has_run_evidence = false;
  ExecutionLifecycle lifecycle;
  BatteryUsage battery_usage;
  RunEvidence run_evidence;
  std::vector<FabricNodeLifecycle> related_nodes;
  std::vector<LogRecord> log_records;
};

struct SystemStatus
{
  Header header;
  std::string plan_id;
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
  std::string bond_id;
  uint8_t capability_plan_state = 0;
  std::vector<CapabilityStatus> capabilities;
};
}  // namespace status

}  // namespace fabric