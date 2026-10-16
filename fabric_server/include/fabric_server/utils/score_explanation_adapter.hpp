#pragma once

#include <array>

#include <fabric_base/utils/experience_structs.hpp>
#include <fabric_msgs/msg/estimator_output.hpp>
#include <fabric_msgs/msg/plan_node_score_explanation.hpp>
#include <fabric_msgs/msg/plan_score_explanation.hpp>

namespace fabric::server::adapter
{

/**
 * @brief Convert an internal estimator result into a ROS message.
 *
 * @param output Internal estimator output.
 * @return fabric_msgs::msg::EstimatorOutput ROS message representation.
 */
inline fabric_msgs::msg::EstimatorOutput toEstimatorOutputMsg(const fabric::experience::EstimateResult& output)
{
  fabric_msgs::msg::EstimatorOutput msg;
  msg.name = output.name;
  msg.value = output.value;
  msg.available = output.available;
  msg.reason = output.reason;
  return msg;
}

/**
 * @brief Convert one node score-explanation record into a ROS message.
 *
 * @param record Internal node score-explanation record.
 * @return fabric_msgs::msg::PlanNodeScoreExplanation ROS message representation.
 */
inline fabric_msgs::msg::PlanNodeScoreExplanation toPlanNodeScoreExplanationMsg(
  const fabric::experience::PlanNodeScoreExplanationRecord& record)
{
  fabric_msgs::msg::PlanNodeScoreExplanation msg;
  msg.planning_request_id = record.planning_request_id;
  msg.candidate_plan_id = record.candidate_plan_id;
  msg.graph_hash = record.graph_hash;
  msg.candidate_node_id = record.candidate_node_id;
  msg.skill_name = record.skill_name;
  msg.skill_id = record.skill_id;
  msg.requested_context_key = record.requested_context_key;
  msg.requested_context_class = record.requested_context_class;
  msg.selected_skill_id = record.selected_skill_id;
  msg.selected_key = record.selected_key;
  msg.estimator_window = record.estimator_window;
  msg.fallback_level = record.fallback_level;
  msg.proxy_source = record.proxy_source;
  msg.reason = record.reason;
  msg.semantic_familiarity = record.semantic_familiarity;
  msg.semantic_relevant_capabilities = record.semantic_relevant_capabilities;
  msg.semantic_related_entities = record.semantic_related_entities;
  msg.semantic_dependencies = record.semantic_dependencies;
  msg.semantic_plan_patterns = record.semantic_plan_patterns;
  msg.semantic_fault_concepts = record.semantic_fault_concepts;
  msg.semantic_community_summaries = record.semantic_community_summaries;
  std::array<double, 6> feature_vector{ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
  std::array<bool, 6> feature_available{ false, false, false, false, false, false };
  msg.recorded_at_sec = record.recorded_at_sec;
  msg.recorded_at_nanosec = record.recorded_at_nanosec;
  msg.provenance = record.provenance;
  for (const auto& output : record.estimator_outputs)
  {
    if (output.name == "success_probability")
    {
      msg.success_probability = output.value;
      msg.success_probability_available = output.available;
      feature_vector[0] = output.value;
      feature_available[0] = output.available;
    }
    else if (output.name == "runtime_seconds")
    {
      msg.expected_runtime_seconds = output.value;
      msg.expected_runtime_seconds_available = output.available;
      feature_vector[1] = output.value;
      feature_available[1] = output.available;
    }
    else if (output.name == "energy_delta_percentage")
    {
      msg.expected_energy_delta_percentage = output.value;
      msg.expected_energy_delta_percentage_available = output.available;
      feature_vector[2] = output.value;
      feature_available[2] = output.available;
    }
    else if (output.name == "fault_burden")
    {
      msg.expected_fault_burden = output.value;
      msg.expected_fault_burden_available = output.available;
      feature_vector[3] = output.value;
      feature_available[3] = output.available;
    }
    else if (output.name == "resource_demand")
    {
      msg.expected_resource_demand = output.value;
      msg.expected_resource_demand_available = output.available;
      feature_vector[4] = output.value;
      feature_available[4] = output.available;
    }
    else if (output.name == "uncertainty")
    {
      msg.uncertainty = output.value;
      msg.uncertainty_available = output.available;
      feature_vector[5] = output.value;
      feature_available[5] = output.available;
    }
    msg.estimator_outputs.push_back(toEstimatorOutputMsg(output));
  }
  msg.feature_vector = feature_vector;
  msg.feature_available = feature_available;
  return msg;
}

/**
 * @brief Convert a selected-plan explanation bundle into a ROS message.
 *
 * @param bundle Internal selected-plan explanation bundle.
 * @return fabric_msgs::msg::PlanScoreExplanation ROS message representation.
 */
inline fabric_msgs::msg::PlanScoreExplanation toPlanScoreExplanationMsg(
  const fabric::experience::SelectedPlanScoreExplanationBundle& bundle)
{
  fabric_msgs::msg::PlanScoreExplanation msg;
  msg.planning_request_id = bundle.feedback.planning_request_id;
  msg.candidate_plan_id = bundle.feedback.candidate_plan_id;
  msg.graph_hash = bundle.feedback.graph_hash;
  msg.selected = bundle.feedback.selected;
  msg.scoring_supported = bundle.feedback.scoring_supported;
  msg.feasible = bundle.feedback.feasible;
  msg.sequential_graph = bundle.feedback.sequential_graph;
  msg.feasibility_reason = bundle.feedback.feasibility_reason;
  msg.terminal_success_estimate = bundle.feedback.terminal_success_estimate;
  msg.expected_runtime_seconds = bundle.feedback.expected_runtime_seconds;
  msg.expected_energy_delta_percentage = bundle.feedback.expected_energy_delta_percentage;
  msg.expected_fault_burden = bundle.feedback.expected_fault_burden;
  msg.expected_resource_demand = bundle.feedback.expected_resource_demand;
  msg.expected_uncertainty = bundle.feedback.expected_uncertainty;
  msg.final_rapfs = bundle.feedback.final_rapfs;
  msg.terminal_outcome = bundle.feedback.terminal_outcome;
  msg.terminal_success = bundle.feedback.terminal_success;
  msg.runtime_seconds = bundle.feedback.runtime_seconds;
  msg.energy_delta_percentage = bundle.feedback.energy_delta_percentage;
  msg.warning_count = bundle.feedback.warning_count;
  msg.error_count = bundle.feedback.error_count;
  msg.human_judgement = bundle.feedback.human_judgement;
  msg.score_components = bundle.feedback.score_components;
  msg.calibration_version = bundle.feedback.calibration_version;
  msg.normalizer_version = bundle.feedback.normalizer_version;
  msg.estimator_version = bundle.feedback.estimator_version;
  msg.weight_version = bundle.feedback.weight_version;
  msg.recorded_at_sec = bundle.feedback.recorded_at_sec;
  msg.recorded_at_nanosec = bundle.feedback.recorded_at_nanosec;
  msg.provenance = bundle.feedback.provenance;
  for (const auto& record : bundle.node_explanations)
    msg.node_explanations.push_back(toPlanNodeScoreExplanationMsg(record));
  return msg;
}

}  // namespace fabric::server::adapter