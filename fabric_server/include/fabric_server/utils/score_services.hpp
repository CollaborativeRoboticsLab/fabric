#pragma once

#include <optional>
#include <string>

#include <fabric_base/utils/experience_structs.hpp>
#include <fabric_base/utils/planning_structs.hpp>

#include <fabric_msgs/srv/get_scoring_epoch.hpp>
#include <fabric_msgs/srv/get_plan_score_explanation.hpp>
#include <fabric_msgs/srv/record_plan_feedback.hpp>
#include <fabric_msgs/srv/record_scoring_epoch.hpp>
#include <fabric_msgs/srv/score_plan.hpp>

#include <fabric_server/utils/feedback.hpp>
#include <fabric_server/utils/score_explanation_adapter.hpp>

namespace fabric::server::score_services
{
using PlanningResult = ::fabric::planning::Result;
using PlanNodeScoreExplanationRecord =
  ::fabric::experience::PlanNodeScoreExplanationRecord;
using SelectedPlanScoreExplanationBundle =
  ::fabric::experience::SelectedPlanScoreExplanationBundle;
using PlanFeedbackRecord = ::fabric::experience::PlanFeedbackRecord;
using PlanNodeScoreExplanationQuery =
  ::fabric::experience::PlanNodeScoreExplanationQuery;
using ScoringEpochRecord = ::fabric::experience::ScoringEpochRecord;
using GetPlanScoreExplanation = ::fabric_msgs::srv::GetPlanScoreExplanation;
using GetScoringEpoch = ::fabric_msgs::srv::GetScoringEpoch;
using RecordPlanFeedback = ::fabric_msgs::srv::RecordPlanFeedback;
using RecordScoringEpoch = ::fabric_msgs::srv::RecordScoringEpoch;
using ScorePlan = ::fabric_msgs::srv::ScorePlan;

inline void setGetPlanScoreExplanationUnavailable(
  GetPlanScoreExplanation::Response & response)
{
  response.found = false;
  response.node_found = false;
  response.message = "fabric experience services are waiting for capabilities2 readiness";
}

inline void setGetPlanScoreExplanationNotFound(
  GetPlanScoreExplanation::Response & response)
{
  response.found = false;
  response.node_found = false;
  response.message = "selected plan score explanation not found";
}

inline void populateGetPlanScoreExplanationResponse(
  const SelectedPlanScoreExplanationBundle & bundle,
  const std::optional<PlanNodeScoreExplanationRecord> & node,
  GetPlanScoreExplanation::Response & response)
{
  response.found = true;
  response.node_found = false;
  response.message = "selected plan score explanation loaded";
  response.plan = fabric::server::adapter::toPlanScoreExplanationMsg(bundle);

  if (!node.has_value()) {
    return;
  }

  response.node_found = true;
  response.node_explanation =
    fabric::server::adapter::toPlanNodeScoreExplanationMsg(node.value());
}

inline void setGetPlanScoreExplanationNodeMissing(
  GetPlanScoreExplanation::Response & response)
{
  response.node_found = false;
  response.message =
    "selected plan explanation loaded, requested node explanation not found";
}

inline void setScorePlanUnavailable(ScorePlan::Response & response)
{
  response.scored = false;
  response.node_found = false;
  response.message = "fabric experience services are waiting for capabilities2 readiness";
}

inline std::optional<std::string> validateScorePlanRequest(
  const ScorePlan::Request & request)
{
  if (request.task_description.empty()) {
    return "task_description is required";
  }
  if (request.plan_xml.empty()) {
    return "plan_xml is required";
  }
  return std::nullopt;
}

inline std::optional<PlanNodeScoreExplanationRecord> findNodeExplanation(
  const SelectedPlanScoreExplanationBundle & bundle,
  const std::string & candidate_node_id)
{
  if (candidate_node_id.empty()) {
    return std::nullopt;
  }

  for (const auto & node : bundle.node_explanations) {
    if (node.candidate_node_id == candidate_node_id) {
      return node;
    }
  }
  return std::nullopt;
}

inline void populateScorePlanResponse(
  const SelectedPlanScoreExplanationBundle & bundle,
  const PlanningResult & planning_result,
  const std::string & candidate_node_id,
  ScorePlan::Response & response)
{
  response.plan = fabric::server::adapter::toPlanScoreExplanationMsg(bundle);
  response.scored = !planning_result.candidates.empty();
  response.node_found = false;
  response.message = response.scored ? "plan scored" :
    "no scoreable candidate extracted from plan";

  const auto node = findNodeExplanation(bundle, candidate_node_id);
  if (!node.has_value()) {
    if (!candidate_node_id.empty()) {
      response.message = "plan scored, requested node explanation not found";
    }
    return;
  }

  response.node_found = true;
  response.node_explanation =
    fabric::server::adapter::toPlanNodeScoreExplanationMsg(node.value());
}

inline void setScorePlanFailure(
  ScorePlan::Response & response,
  const std::string & message)
{
  response.scored = false;
  response.node_found = false;
  response.message = message;
}

inline void setGetScoringEpochUnavailable(GetScoringEpoch::Response & response)
{
  response.found = false;
  response.message = "fabric experience services are waiting for capabilities2 readiness";
}

inline void setGetScoringEpochMissingId(GetScoringEpoch::Response & response)
{
  response.found = false;
  response.message = "epoch_id is required";
}

inline void setGetScoringEpochNotFound(GetScoringEpoch::Response & response)
{
  response.found = false;
  response.message = "scoring epoch not found";
}

template<typename CorePluginT>
inline void populateGetScoringEpochResponse(
  const ScoringEpochRecord & record,
  CorePluginT & core_plugin,
  GetScoringEpoch::Response & response)
{
  response.found = true;
  response.message = "scoring epoch loaded";
  response.epoch_id = record.epoch_id;
  response.strategy_name = record.strategy_name;
  response.calibration_version = record.calibration_version;
  response.normalizer_version = record.normalizer_version;
  response.estimator_version = record.estimator_version;
  response.weight_version = record.weight_version;
  response.uncertainty_weight_locked = record.uncertainty_weight_locked;
  response.calibration_summary = ::fabric::server::feedback::combineCalibrationSummary(
    record.calibration_summary,
    core_plugin.summarizePlanFeedbackCalibration(
      record.calibration_version,
      record.normalizer_version,
      record.estimator_version,
      record.weight_version));
  response.selection_bias_summary = ::fabric::server::feedback::combineSelectionBiasSummary(
    record.selection_bias_summary,
    core_plugin.summarizePlanFeedbackSelectionBias(
      record.calibration_version,
      record.normalizer_version,
      record.estimator_version,
      record.weight_version));
  response.active = record.active;
  response.start_sec = record.start_sec;
  response.start_nanosec = record.start_nanosec;
  response.end_sec = record.end_sec;
  response.end_nanosec = record.end_nanosec;
  response.recorded_at_sec = record.recorded_at_sec;
  response.recorded_at_nanosec = record.recorded_at_nanosec;
  response.provenance = record.provenance;
}

inline void setRecordScoringEpochUnavailable(
  RecordScoringEpoch::Response & response)
{
  response.stored = false;
  response.found_existing = false;
  response.message = "fabric experience services are waiting for capabilities2 readiness";
}

inline void setRecordScoringEpochMissingId(
  RecordScoringEpoch::Response & response)
{
  response.stored = false;
  response.found_existing = false;
  response.message = "epoch_id is required";
}

inline ScoringEpochRecord buildScoringEpochRecord(
  const std::optional<ScoringEpochRecord> & existing,
  const RecordScoringEpoch::Request & request,
  const int32_t recorded_at_sec,
  const uint32_t recorded_at_nanosec)
{
  ScoringEpochRecord record;
  if (existing.has_value()) {
    record = existing.value();
  }

  record.epoch_id = request.epoch_id;
  if (!request.strategy_name.empty()) {
    record.strategy_name = request.strategy_name;
  }
  if (!request.calibration_version.empty()) {
    record.calibration_version = request.calibration_version;
  }
  if (!request.normalizer_version.empty()) {
    record.normalizer_version = request.normalizer_version;
  }
  if (!request.estimator_version.empty()) {
    record.estimator_version = request.estimator_version;
  }
  if (!request.weight_version.empty()) {
    record.weight_version = request.weight_version;
  }
  record.uncertainty_weight_locked = request.uncertainty_weight_locked;
  if (!request.calibration_summary.empty()) {
    record.calibration_summary = request.calibration_summary;
  }
  if (!request.selection_bias_summary.empty()) {
    record.selection_bias_summary = request.selection_bias_summary;
  }
  record.active = request.active;
  if (request.start_sec != 0 || request.start_nanosec != 0) {
    record.start_sec = request.start_sec;
    record.start_nanosec = request.start_nanosec;
  }
  if (request.end_sec != 0 || request.end_nanosec != 0) {
    record.end_sec = request.end_sec;
    record.end_nanosec = request.end_nanosec;
  }
  record.recorded_at_sec = request.recorded_at_sec != 0 ?
    request.recorded_at_sec :
    recorded_at_sec;
  record.recorded_at_nanosec = request.recorded_at_nanosec != 0 ?
    request.recorded_at_nanosec :
    recorded_at_nanosec;
  record.provenance = !request.provenance.empty() ?
    request.provenance :
    "scoring epoch service";
  return record;
}

inline void populateRecordScoringEpochResponse(
  const bool found_existing,
  const bool stored,
  RecordScoringEpoch::Response & response)
{
  response.found_existing = found_existing;
  response.stored = stored;
  response.message = stored ? "scoring epoch stored" :
    "failed to store scoring epoch";
}

inline void setRecordPlanFeedbackUnavailable(
  RecordPlanFeedback::Response & response)
{
  response.stored = false;
  response.found_existing = false;
  response.message = "fabric experience services are waiting for capabilities2 readiness";
}

inline std::optional<std::string> validateRecordPlanFeedbackRequest(
  const RecordPlanFeedback::Request & request)
{
  if (request.planning_request_id.empty() ||
    request.candidate_plan_id.empty() ||
    request.graph_hash.empty())
  {
    return "planning_request_id, candidate_plan_id, and graph_hash are required";
  }

  if (request.terminal_outcome.empty()) {
    return "terminal_outcome is required";
  }

  return std::nullopt;
}

inline PlanNodeScoreExplanationQuery buildPlanFeedbackQuery(
  const RecordPlanFeedback::Request & request)
{
  return PlanNodeScoreExplanationQuery{
    request.planning_request_id,
    request.candidate_plan_id,
    request.graph_hash,
  };
}

inline PlanFeedbackRecord buildPlanFeedbackRecord(
  const std::optional<SelectedPlanScoreExplanationBundle> & existing,
  const RecordPlanFeedback::Request & request,
  const int32_t recorded_at_sec,
  const uint32_t recorded_at_nanosec)
{
  PlanFeedbackRecord feedback;
  if (existing.has_value()) {
    feedback = existing->feedback;
  }

  feedback.planning_request_id = request.planning_request_id;
  feedback.candidate_plan_id = request.candidate_plan_id;
  feedback.graph_hash = request.graph_hash;
  feedback.terminal_outcome = request.terminal_outcome;
  feedback.terminal_success = request.terminal_success;
  if (!request.human_judgement.empty()) {
    feedback.human_judgement = request.human_judgement;
  }
  if (!request.score_components.empty()) {
    feedback.score_components = request.score_components;
  }
  if (!request.calibration_version.empty()) {
    feedback.calibration_version = request.calibration_version;
  }
  if (!request.normalizer_version.empty()) {
    feedback.normalizer_version = request.normalizer_version;
  }
  if (!request.estimator_version.empty()) {
    feedback.estimator_version = request.estimator_version;
  }
  if (!request.weight_version.empty()) {
    feedback.weight_version = request.weight_version;
  }

  feedback.recorded_at_sec = request.recorded_at_sec != 0 ?
    request.recorded_at_sec :
    recorded_at_sec;
  feedback.recorded_at_nanosec = request.recorded_at_nanosec != 0 ?
    request.recorded_at_nanosec :
    recorded_at_nanosec;
  feedback.provenance = !request.provenance.empty() ?
    request.provenance :
    "external plan feedback service";
  return feedback;
}

inline void populateRecordPlanFeedbackResponse(
  const bool found_existing,
  const bool stored,
  RecordPlanFeedback::Response & response)
{
  response.found_existing = found_existing;
  response.stored = stored;
  response.message = stored ? "plan feedback stored" :
    "failed to store plan feedback";
}
template<typename CorePluginT>
inline void recordPlanFeedback(
  CorePluginT & core_plugin,
  const RecordPlanFeedback::Request & request,
  RecordPlanFeedback::Response & response,
  const int32_t recorded_at_sec,
  const uint32_t recorded_at_nanosec)
{
  if (const auto error = validateRecordPlanFeedbackRequest(request); error.has_value()) {
    response.stored = false;
    response.found_existing = false;
    response.message = error.value();
    return;
  }

  const auto existing = core_plugin.readSelectedPlanScoreExplanationBundle(
    buildPlanFeedbackQuery(request));
  const auto feedback = buildPlanFeedbackRecord(
    existing, request, recorded_at_sec, recorded_at_nanosec);
  populateRecordPlanFeedbackResponse(
    existing.has_value(), core_plugin.recordPlanFeedback(feedback), response);
}

}  // namespace fabric::server::score_services
