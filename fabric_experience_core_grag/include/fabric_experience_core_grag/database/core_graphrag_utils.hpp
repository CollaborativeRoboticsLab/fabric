#pragma once

#include <cctype>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include <fabric_base/utils/experience_structs.hpp>
#include <fabric_base/utils/status_structs.hpp>
#include <fabric_experience_core_grag/database/structs.hpp>

namespace fabric::experience::database::core_graphrag
{
using EstimatorEvidence = ::fabric::experience::EstimatorEvidence;
using SystemStatus = ::fabric::status::SystemStatus;
using BehaviourStatus = ::fabric::status::CapabilityStatus;
using Time = ::fabric::status::Time;

inline EstimatorEvidence estimatorEvidenceFromSummary(const SkillStatisticsSummary& summary)
{
  EstimatorEvidence evidence;
  evidence.runs_started = summary.runs_started;
  evidence.runs_completed = summary.runs_completed;
  evidence.runs_succeeded = summary.runs_succeeded;
  evidence.runs_failed = summary.runs_failed;
  evidence.runs_aborted = summary.runs_aborted;
  evidence.runs_timed_out = summary.runs_timed_out;
  evidence.runtime_sample_count = summary.runtime_sample_count;
  evidence.runtime_sum_seconds = summary.runtime_sum_seconds;
  evidence.runtime_sum_squares_seconds = summary.runtime_sum_squares_seconds;
  evidence.energy_sample_count = summary.energy_sample_count;
  evidence.energy_sum_delta_percentage = summary.energy_sum_delta_percentage;
  evidence.energy_sum_squares_delta_percentage = summary.energy_sum_squares_delta_percentage;
  evidence.warning_count = summary.warning_count;
  evidence.error_count = summary.error_count;
  evidence.fault_exposure_count = summary.fault_exposure_count;
  evidence.resource_missing_dimensions = summary.resource_missing_dimensions;
  return evidence;
}

inline std::string fallbackLevelToString(StatisticsFallbackLevel level)
{
  switch (level)
  {
    case StatisticsFallbackLevel::EXACT_CONTEXT:
      return "exact_context";
    case StatisticsFallbackLevel::CONTEXT_CLASS:
      return "context_class";
    case StatisticsFallbackLevel::SKILL_AGGREGATE:
      return "skill_aggregate";
    case StatisticsFallbackLevel::SEMANTIC_RELATION:
      return "semantic_relation";
    case StatisticsFallbackLevel::WINNING_PLAN_PRIOR:
      return "winning_plan_prior";
    case StatisticsFallbackLevel::PRIOR_DEFAULT:
    default:
      return "prior_default";
  }
}

inline bool is_finalized(const SystemStatus& status, const BehaviourStatus& capability)
{
  constexpr uint8_t FABRIC_COMPLETED = 13;
  constexpr uint8_t FABRIC_CANCELLED = 14;

  return status.capability_plan_state == FABRIC_COMPLETED || status.capability_plan_state == FABRIC_CANCELLED ||
         (capability.has_run_evidence && capability.run_evidence.runs_completed > 0 &&
          !capability.run_evidence.last_outcome_label.empty());
}

inline std::string normalize_id_segment(const std::string& value)
{
  std::string normalized;
  normalized.reserve(value.size());

  for (const unsigned char ch : value)
  {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.')
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    else
      normalized.push_back('_');
  }

  return normalized.empty() ? "unknown" : normalized;
}

inline std::string missing_field_flags(const SystemStatus& status, const BehaviourStatus& capability)
{
  std::vector<std::string> flags;

  if (status.plan_id.empty())
    flags.push_back("plan_id");
  if (capability.instance_id.empty())
    flags.push_back("instance_id");
  if (capability.capability_name.empty())
    flags.push_back("skill_name");
  if (!capability.has_lifecycle)
    flags.push_back("lifecycle");
  if (!capability.has_battery_usage)
    flags.push_back("battery_usage");
  if (!capability.has_run_evidence)
    flags.push_back("run_evidence");
  flags.push_back("resource_cpu");
  flags.push_back("resource_memory");
  flags.push_back("resource_network");
  flags.push_back("resource_storage");
  flags.push_back("resource_actuator");

  std::ostringstream out;
  for (size_t index = 0; index < flags.size(); ++index)
  {
    if (index > 0)
      out << ',';
    out << flags[index];
  }

  return out.str();
}

inline double to_seconds(const Time& time)
{
  return static_cast<double>(time.sec) + static_cast<double>(time.nanosec) / 1000000000.0;
}

inline void accumulate_interval_evidence(const BehaviourStatus& capability, SkillRunStatistics& data)
{
  if (capability.has_battery_usage)
  {
    for (const auto& interval : capability.battery_usage.run_intervals)
    {
      const double energy_delta = std::abs(interval.delta_percentage);
      if (std::isfinite(energy_delta))
      {
        ++data.energy_sample_count;
        data.energy_sum_delta_percentage += energy_delta;
        data.energy_sum_squares_delta_percentage += energy_delta * energy_delta;
      }
    }
  }

  // Runtime is derived from lifecycle timestamps; battery intervals are only used for energy.
  if (capability.has_lifecycle && capability.lifecycle.has_start && capability.lifecycle.has_stop)
  {
    const double start_seconds = to_seconds(capability.lifecycle.last_start_time);
    const double stop_seconds = to_seconds(capability.lifecycle.last_stop_time);
    const double runtime_seconds = stop_seconds - start_seconds;
    if (runtime_seconds > 0.0 && std::isfinite(runtime_seconds))
    {
      ++data.runtime_sample_count;
      data.runtime_sum_seconds += runtime_seconds;
      data.runtime_sum_squares_seconds += runtime_seconds * runtime_seconds;
    }
  }
}

inline void accumulate_fault_evidence(const BehaviourStatus& capability, SkillRunStatistics& data)
{
  constexpr uint8_t LOG_WARNING = 1;
  constexpr uint8_t LOG_ERROR = 2;

  data.warning_count = 0;
  data.error_count = 0;
  data.fault_exposure_count = capability.run_evidence.runs_started;

  for (const auto& record : capability.log_records)
  {
    if (record.label == LOG_WARNING)
      ++data.warning_count;
    else if (record.label == LOG_ERROR)
      ++data.error_count;
  }
}
}  // namespace fabric::experience::database::core_graphrag
