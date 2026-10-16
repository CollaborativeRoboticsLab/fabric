#pragma once

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>

#include <fabric_base/core_base.hpp>
#include <fabric_base/utils/experience_structs.hpp>
#include <fabric_base/utils/planning_structs.hpp>
#include <fabric_base/utils/status_structs.hpp>

namespace fabric::server::feedback
{
using PlanningRequest = ::fabric::planning::Request;
using PlanningResult = ::fabric::planning::Result;
using CandidatePlan = ::fabric::planning::CandidatePlan;
using CandidatePlanScore = ::fabric::planning::CandidatePlanScore;
using PendingGeneratedPlan = ::fabric::planning::PendingGeneratedPlan;
using ValidationFindingLevel = ::fabric::planning::ValidationFindingLevel;
using PlanFeedbackRecord = ::fabric::experience::PlanFeedbackRecord;
using ScoringEpochRecord = ::fabric::experience::ScoringEpochRecord;
using CandidatePlanSkillAttribution = ::fabric::experience::CandidatePlanSkillAttribution;
using CandidatePlanEdgeAttribution = ::fabric::experience::CandidatePlanEdgeAttribution;
using PlanNodeScoreExplanationRecord = ::fabric::experience::PlanNodeScoreExplanationRecord;
using PlanNodeScoreExplanationQuery = ::fabric::experience::PlanNodeScoreExplanationQuery;
using SelectedPlanScoreExplanationBundle = ::fabric::experience::SelectedPlanScoreExplanationBundle;
using SystemStatus = ::fabric::status::SystemStatus;

inline int countValidationFindings(const CandidatePlan& candidate, const ::fabric::planning::ValidationFindingLevel level)
{
  return static_cast<int>(std::count_if(candidate.validation.findings.begin(), candidate.validation.findings.end(),
                                        [level](const auto& finding) { return finding.level == level; }));
}

inline std::string summarizePersistedCandidateReason(const CandidatePlan& candidate)
{
  std::ostringstream out;

  if (!candidate.validation.summary.empty())
    out << "validation: " << candidate.validation.summary;

  const size_t limit = std::min<size_t>(3, candidate.validation.findings.size());
  if (limit > 0)
  {
    if (!out.str().empty())
      out << "; ";
    out << "findings: ";
    for (size_t index = 0; index < limit; ++index)
    {
      if (index > 0)
        out << " | ";
      const auto& finding = candidate.validation.findings[index];
      out << ::fabric::planning::validationFindingLevelLabel(finding.level) << ": " << finding.message;
    }
  }

  if (!candidate.score.reason.empty())
  {
    if (!out.str().empty())
      out << "; ";
    out << "scoring: " << candidate.score.reason;
  }

  const std::string summary = out.str();
  return summary.empty() ? std::string("candidate scored without validation findings") : summary;
}

inline std::string compactText(const std::string& text, const size_t limit)
{
  if (text.size() <= limit)
    return text;
  return text.substr(0, limit) + "...";
}

inline std::string summarizeSemanticItem(const ::fabric::experience::SemanticContextItem& item)
{
  std::ostringstream out;
  out << item.name;
  if (!item.provider.empty())
    out << " (" << item.provider << ')';
  if (!item.edge_strength_class.empty())
    out << " <" << item.edge_strength_class << '>';
  if (!item.relationship.empty())
    out << " [" << item.relationship << ']';
  if (!item.source.empty() || !item.target.empty())
    out << " {" << item.source << " -> " << item.target << '}';
  if (!item.description.empty())
    out << ": " << compactText(item.description, 120);
  return out.str();
}

inline std::vector<std::string> summarizeSemanticItems(
  const std::vector<::fabric::experience::SemanticContextItem>& items, const size_t limit = 4)
{
  std::vector<std::string> summary;
  const size_t bounded_limit = std::min(limit, items.size());
  summary.reserve(bounded_limit);
  for (size_t index = 0; index < bounded_limit; ++index)
    summary.push_back(summarizeSemanticItem(items[index]));
  return summary;
}

inline std::string generatePlanningRequestId(const rclcpp::Time& now)
{
  return "planning_request_" + std::to_string(now.nanoseconds());
}

inline bool isTerminalPlanState(const uint8_t plan_state)
{
  return plan_state == 13 || plan_state == 14;
}

inline double toSeconds(const ::fabric::status::Time& time)
{
  return static_cast<double>(time.sec) + static_cast<double>(time.nanosec) / 1000000000.0;
}

inline double computePlanRuntimeSeconds(const ::fabric::status::SystemStatus& status)
{
  bool have_start = false;
  bool have_stop = false;
  double earliest_start = 0.0;
  double latest_stop = 0.0;
  for (const auto& capability : status.capabilities)
  {
    if (capability.has_lifecycle && capability.lifecycle.has_start)
    {
      const double start = toSeconds(capability.lifecycle.last_start_time);
      earliest_start = have_start ? std::min(earliest_start, start) : start;
      have_start = true;
    }
    if (capability.has_lifecycle && capability.lifecycle.has_stop)
    {
      const double stop = toSeconds(capability.lifecycle.last_stop_time);
      latest_stop = have_stop ? std::max(latest_stop, stop) : stop;
      have_stop = true;
    }
  }
  return (have_start && have_stop && latest_stop >= earliest_start) ? (latest_stop - earliest_start) : 0.0;
}

inline double computePlanEnergyDelta(const ::fabric::status::SystemStatus& status)
{
  double total = 0.0;
  for (const auto& capability : status.capabilities)
  {
    if (!capability.has_battery_usage)
      continue;
    for (const auto& interval : capability.battery_usage.run_intervals)
      total += std::abs(interval.delta_percentage);
  }
  return total;
}

inline int computePlanLogCount(const ::fabric::status::SystemStatus& status, const uint8_t label)
{
  int count = 0;
  for (const auto& capability : status.capabilities)
    for (const auto& record : capability.log_records)
      if (record.label == label)
        ++count;
  return count;
}

inline std::string combineCalibrationSummary(
  const std::string& notes, const ::fabric::experience::PlanFeedbackCalibrationSummary& summary)
{
  std::ostringstream out;
  if (!notes.empty())
    out << notes;

  if (!notes.empty() && summary.sample_count > 0)
    out << "\n";

  if (summary.sample_count <= 0)
    return out.str();

  out << std::fixed << std::setprecision(3);
  out << "live_feedback: calibration_version=" << summary.calibration_version << ", samples=" << summary.sample_count
      << ", successes=" << summary.success_count << ", failures=" << summary.failure_count
      << ", observed_success_rate=" << summary.observed_success_rate
      << ", mean_predicted_success=" << summary.mean_predicted_success
      << ", brier_score=" << summary.brier_score << ", mean_runtime_abs_error_seconds="
      << summary.mean_runtime_absolute_error_seconds << ", mean_energy_abs_error_percentage="
      << summary.mean_energy_absolute_error_percentage << ", mean_expected_fault_burden="
      << summary.mean_expected_fault_burden << ", mean_warning_count=" << summary.mean_warning_count
      << ", mean_error_count=" << summary.mean_error_count << ", mean_uncertainty=" << summary.mean_uncertainty
      << ", mean_uncertainty_on_success=" << summary.mean_uncertainty_on_success
      << ", mean_uncertainty_on_failure=" << summary.mean_uncertainty_on_failure;
  return out.str();
}

inline std::string combineSelectionBiasSummary(
  const std::string& notes, const ::fabric::experience::PlanFeedbackSelectionBiasSummary& summary)
{
  std::ostringstream out;
  if (!notes.empty())
    out << notes;

  if (!notes.empty() && summary.total_candidate_count > 0)
    out << "\n";

  if (summary.total_candidate_count <= 0)
    return out.str();

  out << std::fixed << std::setprecision(3);
  out << "live_selection_bias: calibration_version=" << summary.calibration_version
      << ", planning_requests=" << summary.planning_request_count << ", total_candidates=" << summary.total_candidate_count
      << ", selected_candidates=" << summary.selected_candidate_count << ", unselected_candidates="
      << summary.unselected_candidate_count << ", feasible_candidates=" << summary.feasible_candidate_count
      << ", selected_feasible_candidates=" << summary.selected_feasible_count
      << ", mean_candidates_per_request=" << summary.mean_candidates_per_request
      << ", selected_execution_rate=" << summary.selected_execution_rate << ", mean_selected_rapfs="
      << summary.mean_selected_rapfs << ", mean_unselected_rapfs=" << summary.mean_unselected_rapfs
      << ", mean_selected_success_estimate=" << summary.mean_selected_success_estimate
      << ", mean_unselected_success_estimate=" << summary.mean_unselected_success_estimate;
  return out.str();
}

inline SelectedPlanScoreExplanationBundle buildScoreExplanationBundle(const PlanningRequest& request,
                                                                      const PlanningResult& planning_result)
{
  SelectedPlanScoreExplanationBundle bundle;
  const CandidatePlan* candidate = nullptr;
  for (const auto& item : planning_result.candidates)
  {
    if (item.candidate_plan_id == planning_result.selected_candidate_plan_id)
    {
      candidate = &item;
      break;
    }
  }

  if (candidate == nullptr && !planning_result.candidates.empty())
    candidate = &planning_result.candidates.front();
  if (candidate == nullptr)
    return bundle;
  bundle.feedback = PlanFeedbackRecord{ request.planning_request_id,
                                        candidate->candidate_plan_id,
                                        candidate->graph_hash,
                                        candidate->candidate_plan_id == planning_result.selected_candidate_plan_id,
                                        candidate->score.supported,
                                        candidate->score.feasible,
                                        candidate->score.sequential_graph,
                                        summarizePersistedCandidateReason(*candidate),
                                        candidate->score.terminal_success_estimate,
                                        candidate->score.expected_runtime_seconds,
                                        candidate->score.expected_energy_delta_percentage,
                                        candidate->score.expected_fault_burden,
                                        candidate->score.expected_resource_demand,
                                        candidate->score.expected_uncertainty,
                                        candidate->score.final_rapfs,
                                        "",
                                        false,
                                        0.0,
                                        0.0,
                                        countValidationFindings(*candidate, ::fabric::planning::ValidationFindingLevel::Warning) +
                                          countValidationFindings(*candidate, ::fabric::planning::ValidationFindingLevel::Penalty),
                                        countValidationFindings(*candidate, ::fabric::planning::ValidationFindingLevel::Reject),
                                        "",
                                        candidate->score.score_components,
                                        candidate->score.calibration_version,
                                        candidate->score.normalizer_version,
                                        candidate->score.estimator_version,
                                        candidate->score.weight_version,
                                        request.recorded_at_sec,
                                        request.recorded_at_nanosec,
                                        "score-only service" };

  bundle.node_explanations.reserve(candidate->nodes.size());
  for (const auto& node : candidate->nodes)
  {
    bundle.node_explanations.push_back(
      ::fabric::experience::PlanNodeScoreExplanationRecord{ request.planning_request_id,
                                                            candidate->candidate_plan_id,
                                                            candidate->graph_hash,
                                                            node.candidate_node_id,
                                                            node.capability_name,
                                                            node.capability_id,
                                                            node.context_key,
                                                            node.context_class,
                                                            node.resolution.selected_skill_id,
                                                            node.resolution.selected_key,
                                                            node.resolution.estimator_window,
                                                            node.resolution.fallback_level,
                                                            node.resolution.proxy_source,
                                                            node.resolution.reason,
                                                            node.semantic_context.familiarity,
                                                            summarizeSemanticItems(node.semantic_context.relevant_capabilities),
                                                            summarizeSemanticItems(node.semantic_context.related_entities),
                                                            summarizeSemanticItems(node.semantic_context.dependencies),
                                                            summarizeSemanticItems(node.semantic_context.plan_patterns),
                                                            summarizeSemanticItems(node.semantic_context.fault_concepts),
                                                            summarizeSemanticItems(node.semantic_context.community_summaries),
                                                            node.estimator_outputs,
                                                            request.recorded_at_sec,
                                                            request.recorded_at_nanosec,
                                                            "score-only service" });
  }

  return bundle;
}

inline void recordSelectedCandidateArtifacts(
  ::fabric::CoreBase& core_plugin,
  const PlanningRequest& request,
  const PlanningResult& planning_result)
{
  for (const auto& candidate : planning_result.candidates)
  {
    const bool selected = candidate.candidate_plan_id == planning_result.selected_candidate_plan_id;
    for (const auto& node : candidate.nodes)
    {
      core_plugin.recordCandidatePlanSkillAttribution(CandidatePlanSkillAttribution{
        request.planning_request_id,
        candidate.candidate_plan_id,
        candidate.graph_hash,
        node.candidate_node_id,
        "",
        std::to_string(node.runtime_instance_id),
        node.capability_name,
        node.capability_id,
        node.context_key,
        node.context_class,
        node.resolution.selected_skill_id,
        node.resolution.selected_key,
        node.resolution.estimator_window,
        node.resolution.fallback_level,
        node.resolution.proxy_source,
        selected,
        false,
        "",
        request.recorded_at_sec,
        request.recorded_at_nanosec,
        node.resolution.reason,
      });

      if (!selected)
        continue;

      core_plugin.recordPlanNodeScoreExplanation(PlanNodeScoreExplanationRecord{
        request.planning_request_id,
        candidate.candidate_plan_id,
        candidate.graph_hash,
        node.candidate_node_id,
        node.capability_name,
        node.capability_id,
        node.context_key,
        node.context_class,
        node.resolution.selected_skill_id,
        node.resolution.selected_key,
        node.resolution.estimator_window,
        node.resolution.fallback_level,
        node.resolution.proxy_source,
        node.resolution.reason,
        node.semantic_context.familiarity,
        summarizeSemanticItems(node.semantic_context.relevant_capabilities),
        summarizeSemanticItems(node.semantic_context.related_entities),
        summarizeSemanticItems(node.semantic_context.dependencies),
        summarizeSemanticItems(node.semantic_context.plan_patterns),
        summarizeSemanticItems(node.semantic_context.fault_concepts),
        summarizeSemanticItems(node.semantic_context.community_summaries),
        node.estimator_outputs,
        request.recorded_at_sec,
        request.recorded_at_nanosec,
        "selected node score explanation",
      });
    }

    for (const auto& edge : candidate.edges)
    {
      core_plugin.recordCandidatePlanEdgeAttribution(CandidatePlanEdgeAttribution{
        request.planning_request_id,
        candidate.candidate_plan_id,
        candidate.graph_hash,
        edge.source_node_id,
        edge.target_node_id,
        edge.source_capability_id,
        edge.target_capability_id,
        edge.edge_label,
        selected,
        false,
        "",
        request.recorded_at_sec,
        request.recorded_at_nanosec,
        "selected candidate edge attribution",
      });
    }

    const int validation_warning_count =
      countValidationFindings(candidate, ValidationFindingLevel::Warning) +
      countValidationFindings(candidate, ValidationFindingLevel::Penalty);
    const int validation_error_count = countValidationFindings(
      candidate,
      ValidationFindingLevel::Reject);

    core_plugin.recordPlanFeedback(PlanFeedbackRecord{
      request.planning_request_id,
      candidate.candidate_plan_id,
      candidate.graph_hash,
      selected,
      candidate.score.supported,
      candidate.score.feasible,
      candidate.score.sequential_graph,
      summarizePersistedCandidateReason(candidate),
      candidate.score.terminal_success_estimate,
      candidate.score.expected_runtime_seconds,
      candidate.score.expected_energy_delta_percentage,
      candidate.score.expected_fault_burden,
      candidate.score.expected_resource_demand,
      candidate.score.expected_uncertainty,
      candidate.score.final_rapfs,
      "",
      false,
      0.0,
      0.0,
      validation_warning_count,
      validation_error_count,
      "",
      candidate.score.score_components,
      candidate.score.calibration_version,
      candidate.score.normalizer_version,
      candidate.score.estimator_version,
      candidate.score.weight_version,
      request.recorded_at_sec,
      request.recorded_at_nanosec,
      selected ? "selected candidate plan score" : "unselected candidate plan score",
    });
  }
}

inline void recordTerminalPlanFeedback(
  ::fabric::CoreBase& core_plugin,
  const PendingGeneratedPlan& pending,
  const SystemStatus& status)
{
  const bool terminal_success = status.capability_plan_state == 13;
  const std::string terminal_outcome = terminal_success ? "completed" : "cancelled";
  const double runtime_seconds = computePlanRuntimeSeconds(status);
  const double energy_delta_percentage = computePlanEnergyDelta(status);
  const int warning_count = computePlanLogCount(status, 1);
  const int error_count = computePlanLogCount(status, 2);

  core_plugin.recordPlanFeedback(PlanFeedbackRecord{
    pending.planning_request_id,
    pending.candidate_plan_id,
    pending.graph_hash,
    true,
    pending.candidate.score.supported,
    pending.candidate.score.feasible,
    pending.candidate.score.sequential_graph,
    pending.candidate.score.reason,
    pending.candidate.score.terminal_success_estimate,
    pending.candidate.score.expected_runtime_seconds,
    pending.candidate.score.expected_energy_delta_percentage,
    pending.candidate.score.expected_fault_burden,
    pending.candidate.score.expected_resource_demand,
    pending.candidate.score.expected_uncertainty,
    pending.candidate.score.final_rapfs,
    terminal_outcome,
    terminal_success,
    runtime_seconds,
    energy_delta_percentage,
    warning_count,
    error_count,
    "",
    pending.candidate.score.score_components,
    pending.candidate.score.calibration_version,
    pending.candidate.score.normalizer_version,
    pending.candidate.score.estimator_version,
    pending.candidate.score.weight_version,
    status.header.stamp.sec,
    status.header.stamp.nanosec,
    "terminal feedback inferred from supervisor SystemStatus",
  });

  for (const auto& node : pending.candidate.nodes)
  {
    core_plugin.recordCandidatePlanSkillAttribution(CandidatePlanSkillAttribution{
      pending.planning_request_id,
      pending.candidate_plan_id,
      pending.graph_hash,
      node.candidate_node_id,
      status.plan_id,
      std::to_string(node.runtime_instance_id),
      node.capability_name,
      node.capability_id,
      node.context_key,
      node.context_class,
      node.resolution.selected_skill_id,
      node.resolution.selected_key,
      node.resolution.estimator_window,
      node.resolution.fallback_level,
      node.resolution.proxy_source,
      true,
      terminal_success,
      terminal_outcome,
      status.header.stamp.sec,
      status.header.stamp.nanosec,
      "terminal node attribution update",
    });
  }

  for (const auto& edge : pending.candidate.edges)
  {
    core_plugin.recordCandidatePlanEdgeAttribution(CandidatePlanEdgeAttribution{
      pending.planning_request_id,
      pending.candidate_plan_id,
      pending.graph_hash,
      edge.source_node_id,
      edge.target_node_id,
      edge.source_capability_id,
      edge.target_capability_id,
      edge.edge_label,
      true,
      terminal_success,
      terminal_outcome,
      status.header.stamp.sec,
      status.header.stamp.nanosec,
      "terminal edge attribution update",
    });
  }
}

struct UserFeedbackHooks
{
  std::function<rclcpp::Time()> now;
  std::function<std::optional<SelectedPlanScoreExplanationBundle>(const PlanNodeScoreExplanationQuery&)> read_selected_plan_score_explanation_bundle;
  std::function<bool(const PlanFeedbackRecord&)> record_plan_feedback;
  std::function<bool(const CandidatePlanSkillAttribution&)> record_candidate_plan_skill_attribution;
  std::function<bool(const ScoringEpochRecord&)> record_scoring_epoch;
};

struct UserFeedbackContext
{
  rclcpp::Logger logger = rclcpp::get_logger("fabric_server.feedback");
  std::string user_feedback_root_dir;
  std::mutex* file_mutex = nullptr;
  bool consume_feedback = false;
  UserFeedbackHooks hooks;
};

inline rclcpp::Time currentFeedbackTime(const UserFeedbackContext& context)
{
  if (context.hooks.now)
    return context.hooks.now();
  return rclcpp::Clock(RCL_SYSTEM_TIME).now();
}

inline std::string sanitizeFeedbackPathComponent(const std::string& value)
{
  std::string sanitized;
  sanitized.reserve(value.size());

  for (const unsigned char ch : value)
  {
    if (std::isalnum(ch) || ch == '_' || ch == '-')
      sanitized.push_back(static_cast<char>(ch));
    else
      sanitized.push_back('_');
  }

  return sanitized.empty() ? std::string("execution") : sanitized;
}

inline std::filesystem::path resolveUserFeedbackRootDir(const UserFeedbackContext& context)
{
  if (!context.user_feedback_root_dir.empty())
    return context.user_feedback_root_dir;

  return std::filesystem::current_path() / "user_feedback";
}

inline std::filesystem::path resolveUserFeedbackFilePath(const UserFeedbackContext& context, const PlanningRequest& request)
{
  const std::string execution_key = sanitizeFeedbackPathComponent(request.uuid.empty() ? request.planning_request_id : request.uuid);
  return resolveUserFeedbackRootDir(context) / execution_key / "user_feedback.json";
}

inline std::filesystem::path resolveGeneratedPlansFilePath(const UserFeedbackContext& context, const PlanningRequest& request)
{
  const std::string execution_key = sanitizeFeedbackPathComponent(request.uuid.empty() ? request.planning_request_id : request.uuid);
  return resolveUserFeedbackRootDir(context) / execution_key / "generated_plans.json";
}

inline bool loadUserFeedbackDocument(const UserFeedbackContext& context, const std::filesystem::path& file_path, nlohmann::json& document)
{
  document = nlohmann::json::object();

  if (!std::filesystem::exists(file_path))
    return true;

  std::ifstream input(file_path);
  if (!input.is_open())
  {
    RCLCPP_WARN(context.logger, "Failed to open user feedback file for reading: %s", file_path.c_str());
    return false;
  }

  try
  {
    input >> document;
  }
  catch (const std::exception& ex)
  {
    RCLCPP_WARN(context.logger, "Failed to parse user feedback file %s: %s", file_path.c_str(), ex.what());
    return false;
  }

  if (!document.is_object())
  {
    RCLCPP_WARN(context.logger, "User feedback file %s must contain a JSON object", file_path.c_str());
    return false;
  }

  return true;
}

inline bool writeUserFeedbackDocument(const UserFeedbackContext& context, const std::filesystem::path& file_path, const nlohmann::json& document)
{
  std::error_code ec;
  std::filesystem::create_directories(file_path.parent_path(), ec);
  if (ec)
  {
    RCLCPP_WARN(context.logger, "Failed to create user feedback directory %s: %s", file_path.parent_path().c_str(), ec.message().c_str());
    return false;
  }

  const auto temp_path = file_path.string() + ".tmp";
  std::ofstream output(temp_path, std::ios::trunc);
  if (!output.is_open())
  {
    RCLCPP_WARN(context.logger, "Failed to open user feedback file for writing: %s", temp_path.c_str());
    return false;
  }

  output << std::setw(2) << document << '\n';
  output.close();

  if (!output.good())
  {
    RCLCPP_WARN(context.logger, "Failed to flush user feedback file: %s", temp_path.c_str());
    return false;
  }

  std::filesystem::remove(file_path, ec);
  ec.clear();
  std::filesystem::rename(temp_path, file_path, ec);
  if (ec)
  {
    std::filesystem::remove(temp_path);
    RCLCPP_WARN(context.logger, "Failed to replace user feedback file %s: %s", file_path.c_str(), ec.message().c_str());
    return false;
  }

  return true;
}

inline int nextUserFeedbackStageIndex(const nlohmann::json& document)
{
  int highest_index = 0;
  for (auto it = document.begin(); it != document.end(); ++it)
  {
    try
    {
      highest_index = std::max(highest_index, std::stoi(it.key()));
    }
    catch (const std::exception&)
    {
    }
  }
  return highest_index + 1;
}

inline nlohmann::json finiteJsonNumberOrNull(const double value)
{
  return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json(nullptr);
}

inline nlohmann::json parseScoreComponentsJson(const std::string& score_components)
{
  if (score_components.empty())
    return nlohmann::json::object();

  try
  {
    return nlohmann::json::parse(score_components);
  }
  catch (const std::exception&)
  {
    return nlohmann::json{ { "raw", score_components }, { "parse_error", true } };
  }
}

inline nlohmann::json planScoreToJson(const CandidatePlanScore& score)
{
  return nlohmann::json{ { "scoring_supported", score.supported },
                         { "feasible", score.feasible },
                         { "sequential_graph", score.sequential_graph },
                         { "feasibility_reason", score.reason },
                         { "terminal_success_estimate", finiteJsonNumberOrNull(score.terminal_success_estimate) },
                         { "expected_runtime_seconds", finiteJsonNumberOrNull(score.expected_runtime_seconds) },
                         { "expected_energy_delta_percentage", finiteJsonNumberOrNull(score.expected_energy_delta_percentage) },
                         { "expected_fault_burden", finiteJsonNumberOrNull(score.expected_fault_burden) },
                         { "expected_resource_demand", finiteJsonNumberOrNull(score.expected_resource_demand) },
                         { "expected_uncertainty", finiteJsonNumberOrNull(score.expected_uncertainty) },
                         { "final_rapfs", finiteJsonNumberOrNull(score.final_rapfs) },
                         { "normalized_scores", score.normalized_scores },
                         { "weighted_scores", score.weighted_scores },
                         { "score_weights", score.score_weights },
                         { "score_components", parseScoreComponentsJson(score.score_components) },
                         { "calibration_version", score.calibration_version },
                         { "normalizer_version", score.normalizer_version },
                         { "estimator_version", score.estimator_version },
                         { "weight_version", score.weight_version } };
}

inline std::string buildFeedbackVersionKey(const PlanFeedbackRecord& feedback)
{
  return feedback.calibration_version + "|" + feedback.normalizer_version + "|" + feedback.estimator_version + "|" + feedback.weight_version;
}

inline std::string normalizeUserFeedbackOutcome(const std::string& value)
{
  std::string normalized;
  normalized.reserve(value.size());
  for (const unsigned char ch : value)
  {
    if (std::isalnum(ch))
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    else if (ch == '_' || ch == '-' || std::isspace(ch))
      normalized.push_back('_');
  }

  while (!normalized.empty() && normalized.front() == '_')
    normalized.erase(normalized.begin());
  while (!normalized.empty() && normalized.back() == '_')
    normalized.pop_back();

  return normalized;
}

inline std::optional<bool> userFeedbackSuccessOverride(const nlohmann::json& entry)
{
  const auto it = entry.find("feedback_success");
  if (it == entry.end() || !it->is_boolean())
    return std::nullopt;
  return it->get<bool>();
}

inline std::vector<std::string> readStringListField(const nlohmann::json& entry, const char* key)
{
  std::vector<std::string> values;
  const auto it = entry.find(key);
  if (it == entry.end() || !it->is_array())
    return values;

  for (const auto& item : *it)
  {
    if (item.is_string())
      values.push_back(item.get<std::string>());
  }
  return values;
}

inline std::string buildUserFeedbackConsumptionSignature(const nlohmann::json& entry)
{
  nlohmann::json signature = { { "outcome", entry.value("outcome", std::string()) },
                               { "feedback", entry.value("feedback", std::string()) },
                               { "feedback_success", nullptr },
                               { "feedback_failed_candidate_node_ids", readStringListField(entry, "feedback_failed_candidate_node_ids") },
                               { "feedback_failed_skill_names", readStringListField(entry, "feedback_failed_skill_names") } };

  const auto feedback_success = userFeedbackSuccessOverride(entry);
  if (feedback_success.has_value())
    signature["feedback_success"] = *feedback_success;

  return signature.dump();
}

inline bool hasStructuredUserFeedback(const nlohmann::json& entry)
{
  if (!entry.value("outcome", std::string()).empty() || !entry.value("feedback", std::string()).empty())
    return true;
  if (userFeedbackSuccessOverride(entry).has_value())
    return true;
  if (!readStringListField(entry, "feedback_failed_candidate_node_ids").empty())
    return true;
  if (!readStringListField(entry, "feedback_failed_skill_names").empty())
    return true;
  return false;
}

inline std::string joinStringVector(const std::vector<std::string>& values, const char* delimiter)
{
  std::ostringstream stream;
  for (size_t index = 0; index < values.size(); ++index)
  {
    if (index > 0)
      stream << delimiter;
    stream << values[index];
  }
  return stream.str();
}

inline std::string terminalOutcomeFromSuccess(const bool success)
{
  return success ? "success" : "failure";
}

inline bool tryApplyUserFeedbackOutcome(PlanFeedbackRecord& feedback, const std::string& user_outcome)
{
  const std::string normalized = normalizeUserFeedbackOutcome(user_outcome);
  if (normalized.empty())
    return false;

  if (normalized == "success" || normalized == "succeeded" || normalized == "completed")
  {
    feedback.terminal_outcome = "success";
    feedback.terminal_success = true;
    return true;
  }

  if (normalized == "partial_success" || normalized == "partial" || normalized == "partially_succeeded")
  {
    feedback.terminal_outcome = "partial_success";
    feedback.terminal_success = false;
    return true;
  }

  if (normalized == "failure" || normalized == "failed")
  {
    feedback.terminal_outcome = "failure";
    feedback.terminal_success = false;
    return true;
  }

  return false;
}

inline bool hasConsumptionHooks(const UserFeedbackContext& context)
{
  return static_cast<bool>(context.hooks.read_selected_plan_score_explanation_bundle) && static_cast<bool>(context.hooks.record_plan_feedback) &&
         static_cast<bool>(context.hooks.record_candidate_plan_skill_attribution) && static_cast<bool>(context.hooks.record_scoring_epoch);
}

inline void recordConsumedFeedbackEpochs(const UserFeedbackContext& context,
                                         const std::unordered_map<std::string, PlanFeedbackRecord>& consumed_versions, const size_t consumed_count)
{
  if (consumed_versions.empty() || !context.hooks.record_scoring_epoch)
    return;

  const auto now = currentFeedbackTime(context);
  const int32_t recorded_at_sec = static_cast<int32_t>(now.seconds());
  const uint32_t recorded_at_nanosec = static_cast<uint32_t>(now.nanoseconds() % 1000000000ULL);

  for (const auto& [version_key, feedback] : consumed_versions)
  {
    (void)version_key;

    ScoringEpochRecord record;
    record.epoch_id = "feedback_ingest_" + std::to_string(now.nanoseconds()) + "_" + sanitizeFeedbackPathComponent(feedback.calibration_version) +
                      "_" + sanitizeFeedbackPathComponent(feedback.normalizer_version) + "_" +
                      sanitizeFeedbackPathComponent(feedback.estimator_version) + "_" + sanitizeFeedbackPathComponent(feedback.weight_version);
    record.strategy_name = "user_feedback_ingest";
    record.calibration_version = feedback.calibration_version;
    record.normalizer_version = feedback.normalizer_version;
    record.estimator_version = feedback.estimator_version;
    record.weight_version = feedback.weight_version;
    record.uncertainty_weight_locked = true;
    record.calibration_summary = "feedback consume run imported " + std::to_string(consumed_count) + " updated user feedback entries";
    record.selection_bias_summary = "feedback consume run snapshot after user feedback import";
    record.active = false;
    record.start_sec = recorded_at_sec;
    record.start_nanosec = recorded_at_nanosec;
    record.end_sec = recorded_at_sec;
    record.end_nanosec = recorded_at_nanosec;
    record.recorded_at_sec = recorded_at_sec;
    record.recorded_at_nanosec = recorded_at_nanosec;
    record.provenance = "fabric_server consume_feedback";

    if (!context.hooks.record_scoring_epoch(record))
    {
      RCLCPP_WARN(context.logger, "Failed to record scoring epoch snapshot for consumed feedback epoch_id=%s", record.epoch_id.c_str());
    }
  }
}

inline void persistSelectedPlanUserFeedback(const UserFeedbackContext& context, const PlanningRequest& request, const PlanningResult& planning_result)
{
  const CandidatePlan* selected_candidate = nullptr;
  for (const auto& candidate : planning_result.candidates)
  {
    if (candidate.candidate_plan_id == planning_result.selected_candidate_plan_id)
    {
      selected_candidate = &candidate;
      break;
    }
  }

  if (selected_candidate == nullptr)
    return;

  const auto file_path = resolveUserFeedbackFilePath(context, request);

  std::unique_lock<std::mutex> lock;
  if (context.file_mutex != nullptr)
    lock = std::unique_lock<std::mutex>(*context.file_mutex);

  nlohmann::json document;
  if (!loadUserFeedbackDocument(context, file_path, document))
    return;

  const std::vector<std::string> score_names = { "success", "time", "energy", "fault", "resource", "uncertainty" };
  nlohmann::json candidate_node_ids = nlohmann::json::array();
  nlohmann::json candidate_skill_names = nlohmann::json::array();
  for (const auto& node : selected_candidate->nodes)
  {
    candidate_node_ids.push_back(node.candidate_node_id);
    candidate_skill_names.push_back(node.resolution.skill_name);
  }
  nlohmann::json expected_scores =
      nlohmann::json::array({ selected_candidate->score.terminal_success_estimate, selected_candidate->score.expected_runtime_seconds,
                              selected_candidate->score.expected_energy_delta_percentage, selected_candidate->score.expected_fault_burden,
                              selected_candidate->score.expected_resource_demand, selected_candidate->score.expected_uncertainty });
  const nlohmann::json normalized_scores = selected_candidate->score.normalized_scores;
  const nlohmann::json weighted_scores = selected_candidate->score.weighted_scores;
  const nlohmann::json score_weights = selected_candidate->score.score_weights;

  const std::string stage_key = std::to_string(nextUserFeedbackStageIndex(document));
  document[stage_key] = { { "plan", selected_candidate->plan },
                          { "reasoning", selected_candidate->reasoning },
                          { "scoring_supported", selected_candidate->score.supported },
                          { "feasible", selected_candidate->score.feasible },
                          { "sequential_graph", selected_candidate->score.sequential_graph },
                          { "feasibility_reason", selected_candidate->score.reason },
                          { "score_name_list", score_names },
                          { "expected_scores", expected_scores },
                          { "normalized_scores", normalized_scores },
                          { "weighted_scores", weighted_scores },
                          { "score_weights", score_weights },
                          { "final_rapfs", finiteJsonNumberOrNull(selected_candidate->score.final_rapfs) },
                          { "score_components", parseScoreComponentsJson(selected_candidate->score.score_components) },
                          { "calibration_version", selected_candidate->score.calibration_version },
                          { "normalizer_version", selected_candidate->score.normalizer_version },
                          { "estimator_version", selected_candidate->score.estimator_version },
                          { "weight_version", selected_candidate->score.weight_version },
                          { "candidate_node_id_list", candidate_node_ids },
                          { "candidate_skill_name_list", candidate_skill_names },
                          { "feedback_success", nullptr },
                          { "feedback_failed_candidate_node_ids", nlohmann::json::array() },
                          { "feedback_failed_skill_names", nlohmann::json::array() },
                          { "outcome", "" },
                          { "feedback", "" },
                          { "outcome_consumed", false },
                          { "last_consumed_outcome", "" },
                          { "feedback_consumed", false },
                          { "last_consumed_feedback", "" },
                          { "last_consumed_signature", "" },
                          { "feedback_consumed_at_sec", 0 },
                          { "feedback_consumed_at_nanosec", 0 },
                          { "planning_request_id", request.planning_request_id },
                          { "candidate_plan_id", selected_candidate->candidate_plan_id },
                          { "graph_hash", selected_candidate->graph_hash },
                          { "execution_uuid", request.uuid },
                          { "recorded_at_sec", request.recorded_at_sec },
                          { "recorded_at_nanosec", request.recorded_at_nanosec } };

  if (writeUserFeedbackDocument(context, file_path, document))
  {
    RCLCPP_INFO(context.logger, "Persisted selected-plan feedback stub at %s (execution_uuid=%s, planning_request_id=%s, stage=%s)",
                file_path.c_str(), request.uuid.c_str(), request.planning_request_id.c_str(), stage_key.c_str());
  }
}

inline void persistGeneratedPlansArtifact(const UserFeedbackContext& context, const PlanningRequest& request,
                                          const std::vector<PlanningResult>& generation_results)
{
  if (generation_results.empty())
    return;

  const auto file_path = resolveGeneratedPlansFilePath(context, request);

  std::unique_lock<std::mutex> lock;
  if (context.file_mutex != nullptr)
    lock = std::unique_lock<std::mutex>(*context.file_mutex);

  nlohmann::json document;
  if (!loadUserFeedbackDocument(context, file_path, document))
    return;

  const int iteration_offset = nextUserFeedbackStageIndex(document) - 1;
  for (size_t iteration_index = 0; iteration_index < generation_results.size(); ++iteration_index)
  {
    const auto& planning_result = generation_results[iteration_index];
    const std::string iteration_key = "iteration_" + std::to_string(iteration_offset + iteration_index + 1);
    nlohmann::json iteration = nlohmann::json::object();
    iteration["selected_candidate_plan_id"] = planning_result.selected_candidate_plan_id;
    iteration["selected_reasoning"] = planning_result.selected_reasoning;
    iteration["candidate_count"] = planning_result.candidates.size();

    for (size_t candidate_index = 0; candidate_index < planning_result.candidates.size(); ++candidate_index)
    {
      const auto& candidate = planning_result.candidates[candidate_index];
      nlohmann::json candidate_node_ids = nlohmann::json::array();
      nlohmann::json candidate_skill_names = nlohmann::json::array();
      for (const auto& node : candidate.nodes)
      {
        candidate_node_ids.push_back(node.candidate_node_id);
        candidate_skill_names.push_back(node.resolution.skill_name);
      }

      nlohmann::json candidate_record = planScoreToJson(candidate.score);
      candidate_record["planning_request_id"] = request.planning_request_id;
      candidate_record["candidate_plan_id"] = candidate.candidate_plan_id;
      candidate_record["graph_hash"] = candidate.graph_hash;
      candidate_record["selected"] = candidate.candidate_plan_id == planning_result.selected_candidate_plan_id;
      candidate_record["ranking_index"] = candidate_index + 1;
      candidate_record["terminal_success"] = candidate.score.feasible;
      candidate_record["terminal_outcome"] = "";
      candidate_record["rejection_reason"] = summarizePersistedCandidateReason(candidate);
      candidate_record["plan"] = candidate.plan;
      candidate_record["reasoning"] = candidate.reasoning;
      candidate_record["candidate_node_id_list"] = candidate_node_ids;
      candidate_record["candidate_skill_name_list"] = candidate_skill_names;
      candidate_record["recorded_at_sec"] = request.recorded_at_sec;
      candidate_record["recorded_at_nanosec"] = request.recorded_at_nanosec;

      iteration["plan_" + std::to_string(candidate_index + 1)] = candidate_record;
    }

    document[iteration_key] = iteration;
  }

  if (writeUserFeedbackDocument(context, file_path, document))
  {
    RCLCPP_INFO(context.logger, "Persisted generated-plan artifact at %s (planning_request_id=%s)", file_path.c_str(),
                request.planning_request_id.c_str());
  }
}

inline bool applyUserFeedbackNodeOverrides(const UserFeedbackContext& context, const SelectedPlanScoreExplanationBundle& existing,
                                           const std::vector<std::string>& failed_candidate_node_ids,
                                           const std::vector<std::string>& failed_skill_names, const int32_t recorded_at_sec,
                                           const uint32_t recorded_at_nanosec)
{
  if (failed_candidate_node_ids.empty() && failed_skill_names.empty())
    return true;

  if (!context.hooks.record_candidate_plan_skill_attribution)
  {
    RCLCPP_WARN(context.logger, "Skipping user feedback node overrides because record_candidate_plan_skill_attribution hook is unset");
    return false;
  }

  std::unordered_set<std::string> failed_node_id_set(failed_candidate_node_ids.begin(), failed_candidate_node_ids.end());
  std::unordered_set<std::string> failed_skill_name_set(failed_skill_names.begin(), failed_skill_names.end());
  size_t applied_count = 0;

  for (const auto& node : existing.node_explanations)
  {
    if (failed_node_id_set.count(node.candidate_node_id) == 0 && failed_skill_name_set.count(node.skill_name) == 0)
      continue;

    const bool stored = context.hooks.record_candidate_plan_skill_attribution(CandidatePlanSkillAttribution{ existing.feedback.planning_request_id,
                                                                                                             existing.feedback.candidate_plan_id,
                                                                                                             existing.feedback.graph_hash,
                                                                                                             node.candidate_node_id,
                                                                                                             "",
                                                                                                             "",
                                                                                                             node.skill_name,
                                                                                                             node.skill_id,
                                                                                                             node.requested_context_key,
                                                                                                             node.requested_context_class,
                                                                                                             node.selected_skill_id,
                                                                                                             node.selected_key,
                                                                                                             node.estimator_window,
                                                                                                             node.fallback_level,
                                                                                                             node.proxy_source,
                                                                                                             true,
                                                                                                             false,
                                                                                                             "failure",
                                                                                                             recorded_at_sec,
                                                                                                             recorded_at_nanosec,
                                                                                                             "consumed user_feedback.json targeted "
                                                                                                             "node override" });
    if (!stored)
      return false;

    ++applied_count;
  }

  if (applied_count == 0)
  {
    RCLCPP_WARN(context.logger,
                "User feedback requested targeted runner penalty but matched no nodes for planning_request_id=%s candidate_plan_id=%s",
                existing.feedback.planning_request_id.c_str(), existing.feedback.candidate_plan_id.c_str());
  }
  else
  {
    RCLCPP_INFO(context.logger,
                "Applied %zu targeted runner feedback override(s) for planning_request_id=%s candidate_plan_id=%s failed_candidate_node_ids=[%s] "
                "failed_skill_names=[%s]",
                applied_count, existing.feedback.planning_request_id.c_str(), existing.feedback.candidate_plan_id.c_str(),
                joinStringVector(failed_candidate_node_ids, ",").c_str(), joinStringVector(failed_skill_names, ",").c_str());
  }

  return true;
}

inline bool mergePersistedUserFeedback(const UserFeedbackContext& context, const std::string& planning_request_id,
                                       const std::string& candidate_plan_id, const std::string& graph_hash, const std::string& user_outcome,
                                       const std::string& human_judgement, const std::optional<bool>& feedback_success,
                                       const std::vector<std::string>& failed_candidate_node_ids, const std::vector<std::string>& failed_skill_names,
                                       const std::string& provenance, PlanFeedbackRecord* merged_feedback = nullptr)
{
  if (!context.hooks.read_selected_plan_score_explanation_bundle || !context.hooks.record_plan_feedback)
  {
    RCLCPP_WARN(context.logger, "Skipping persisted user feedback merge because required read/record hooks are unset");
    return false;
  }

  const PlanNodeScoreExplanationQuery query{ planning_request_id, candidate_plan_id, graph_hash };
  const auto existing = context.hooks.read_selected_plan_score_explanation_bundle(query);
  if (!existing.has_value())
  {
    RCLCPP_WARN(context.logger, "Skipping user feedback for unknown selected plan planning_request_id=%s candidate_plan_id=%s graph_hash=%s",
                planning_request_id.c_str(), candidate_plan_id.c_str(), graph_hash.c_str());
    return false;
  }

  PlanFeedbackRecord feedback = existing->feedback;
  feedback.planning_request_id = planning_request_id;
  feedback.candidate_plan_id = candidate_plan_id;
  feedback.graph_hash = graph_hash;

  if (!user_outcome.empty() && !tryApplyUserFeedbackOutcome(feedback, user_outcome))
  {
    RCLCPP_WARN(context.logger, "Skipping user feedback with unsupported outcome '%s' for planning_request_id=%s", user_outcome.c_str(),
                planning_request_id.c_str());
    return false;
  }
  else if (user_outcome.empty() && feedback_success.has_value())
  {
    feedback.terminal_success = *feedback_success;
    feedback.terminal_outcome = terminalOutcomeFromSuccess(*feedback_success);
  }

  feedback.human_judgement = human_judgement;

  const auto stamp = currentFeedbackTime(context);
  feedback.recorded_at_sec = static_cast<int32_t>(stamp.seconds());
  feedback.recorded_at_nanosec = static_cast<uint32_t>(stamp.nanoseconds() % 1000000000ULL);
  feedback.provenance = provenance;

  const bool stored = context.hooks.record_plan_feedback(feedback);
  if (stored && !applyUserFeedbackNodeOverrides(context, *existing, failed_candidate_node_ids, failed_skill_names, feedback.recorded_at_sec,
                                                feedback.recorded_at_nanosec))
    return false;
  if (stored && merged_feedback != nullptr)
    *merged_feedback = feedback;
  return stored;
}

inline void consumePersistedUserFeedback(const UserFeedbackContext& context)
{
  if (!context.consume_feedback)
    return;

  if (!hasConsumptionHooks(context))
  {
    RCLCPP_WARN(context.logger, "consume_feedback enabled but one or more user feedback persistence hooks are unset");
    return;
  }

  const auto root_dir = resolveUserFeedbackRootDir(context);
  if (!std::filesystem::exists(root_dir))
  {
    RCLCPP_INFO(context.logger, "consume_feedback enabled but no user feedback directory exists at %s", root_dir.c_str());
    return;
  }

  size_t consumed_count = 0;
  size_t skipped_count = 0;
  std::unordered_map<std::string, PlanFeedbackRecord> consumed_versions;
  std::error_code ec;
  for (std::filesystem::recursive_directory_iterator it(root_dir, ec), end; it != end; it.increment(ec))
  {
    if (ec)
    {
      RCLCPP_WARN(context.logger, "Failed while walking user feedback directory %s: %s", root_dir.c_str(), ec.message().c_str());
      break;
    }

    if (!it->is_regular_file() || it->path().filename() != "user_feedback.json")
      continue;

    nlohmann::json document;
    if (!loadUserFeedbackDocument(context, it->path(), document))
    {
      ++skipped_count;
      continue;
    }

    bool document_changed = false;

    for (auto entry_it = document.begin(); entry_it != document.end(); ++entry_it)
    {
      if (!entry_it.value().is_object())
      {
        ++skipped_count;
        continue;
      }

      const std::string user_outcome = entry_it.value().value("outcome", std::string());
      const std::string human_judgement = entry_it.value().value("feedback", std::string());
      const std::optional<bool> feedback_success = userFeedbackSuccessOverride(entry_it.value());
      const std::vector<std::string> failed_candidate_node_ids = readStringListField(entry_it.value(), "feedback_failed_candidate_node_ids");
      const std::vector<std::string> failed_skill_names = readStringListField(entry_it.value(), "feedback_failed_skill_names");
      if (!hasStructuredUserFeedback(entry_it.value()))
        continue;

      const std::string consumed_signature = buildUserFeedbackConsumptionSignature(entry_it.value());
      const std::string last_consumed_signature = entry_it.value().value("last_consumed_signature", std::string());
      const std::string last_consumed_outcome = entry_it.value().value("last_consumed_outcome", std::string());
      const std::string last_consumed_feedback = entry_it.value().value("last_consumed_feedback", std::string());
      if ((!last_consumed_signature.empty() && consumed_signature == last_consumed_signature) ||
          (last_consumed_signature.empty() && user_outcome == last_consumed_outcome && human_judgement == last_consumed_feedback))
      {
        entry_it.value()["outcome_consumed"] = !user_outcome.empty();
        entry_it.value()["feedback_consumed"] = true;
        continue;
      }

      const std::string planning_request_id = entry_it.value().value("planning_request_id", std::string());
      const std::string candidate_plan_id = entry_it.value().value("candidate_plan_id", std::string());
      const std::string graph_hash = entry_it.value().value("graph_hash", std::string());
      if (planning_request_id.empty() || candidate_plan_id.empty() || graph_hash.empty())
      {
        ++skipped_count;
        continue;
      }

      PlanFeedbackRecord merged_feedback;
      if (mergePersistedUserFeedback(context, planning_request_id, candidate_plan_id, graph_hash, user_outcome, human_judgement, feedback_success,
                                     failed_candidate_node_ids, failed_skill_names, "consumed user_feedback.json", &merged_feedback))
      {
        ++consumed_count;
        consumed_versions[buildFeedbackVersionKey(merged_feedback)] = merged_feedback;

        if (feedback_success.has_value() || !failed_candidate_node_ids.empty() || !failed_skill_names.empty())
        {
          RCLCPP_INFO(context.logger,
                      "Consumed structured user feedback from %s stage=%s planning_request_id=%s candidate_plan_id=%s feedback_success=%s "
                      "failed_candidate_node_ids=[%s] failed_skill_names=[%s]",
                      it->path().c_str(), entry_it.key().c_str(), planning_request_id.c_str(), candidate_plan_id.c_str(),
                      feedback_success.has_value() ? (*feedback_success ? "true" : "false") : "unset",
                      joinStringVector(failed_candidate_node_ids, ",").c_str(), joinStringVector(failed_skill_names, ",").c_str());
        }

        const auto stamp = currentFeedbackTime(context);
        entry_it.value()["outcome_consumed"] = !user_outcome.empty();
        entry_it.value()["last_consumed_outcome"] = user_outcome;
        entry_it.value()["feedback_consumed"] = true;
        entry_it.value()["last_consumed_feedback"] = human_judgement;
        entry_it.value()["last_consumed_signature"] = consumed_signature;
        entry_it.value()["feedback_consumed_at_sec"] = static_cast<int32_t>(stamp.seconds());
        entry_it.value()["feedback_consumed_at_nanosec"] = static_cast<uint32_t>(stamp.nanoseconds() % 1000000000ULL);
        document_changed = true;
      }
      else
      {
        ++skipped_count;
      }
    }

    if (document_changed && !writeUserFeedbackDocument(context, it->path(), document))
      ++skipped_count;
  }

  recordConsumedFeedbackEpochs(context, consumed_versions, consumed_count);

  RCLCPP_INFO(context.logger, "Consumed %zu user feedback entries from %s (%zu skipped)", consumed_count, root_dir.c_str(), skipped_count);
}

}  // namespace fabric::server::feedback
