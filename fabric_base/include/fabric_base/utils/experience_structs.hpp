#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace fabric
{
namespace experience
{

struct EstimatorEvidence
{
  int32_t runs_started = 0;
  int32_t runs_completed = 0;
  int32_t runs_succeeded = 0;
  int32_t runs_failed = 0;
  int32_t runs_aborted = 0;
  int32_t runs_timed_out = 0;
  int32_t runtime_sample_count = 0;
  double runtime_sum_seconds = 0.0;
  double runtime_sum_squares_seconds = 0.0;
  int32_t energy_sample_count = 0;
  double energy_sum_delta_percentage = 0.0;
  double energy_sum_squares_delta_percentage = 0.0;
  int32_t warning_count = 0;
  int32_t error_count = 0;
  int32_t fault_exposure_count = 0;
  std::string resource_missing_dimensions;
};

struct EstimateResult
{
  std::string name;
  double value = 0.0;
  bool available = false;
  std::string reason;
};

struct CandidateSkillRequest
{
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
  std::string candidate_node_id;
  std::string skill_name;
  std::string skill_id;
  std::string context_key;
  std::string context_class;
  bool allow_proxy_fallback = false;
};

struct CandidateSkillResolution
{
  std::string skill_name;
  std::string skill_id;
  std::string selected_skill_id;
  std::string requested_context_key;
  std::string requested_context_class;
  std::string selected_key = "prior/default";
  std::string estimator_window = "prior";
  std::string fallback_level = "prior_default";
  std::string proxy_source = "requested_skill";
  std::string reason;
  bool has_empirical_evidence = false;
  EstimatorEvidence evidence;
};

struct CandidatePlanSkillAttribution
{
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
  std::string candidate_node_id;
  std::string runtime_plan_id;
  std::string runtime_instance_id;
  std::string skill_name;
  std::string skill_id;
  std::string context_key;
  std::string context_class;
  std::string selected_skill_id;
  std::string selected_key;
  std::string estimator_window;
  std::string fallback_level;
  std::string proxy_source;
  bool selected = false;
  bool terminal_success = false;
  std::string terminal_outcome;
  int32_t recorded_at_sec = 0;
  uint32_t recorded_at_nanosec = 0;
  std::string provenance;
};

struct CandidatePlanEdgeAttribution
{
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
  std::string source_node_id;
  std::string target_node_id;
  std::string source_skill_id;
  std::string target_skill_id;
  std::string edge_label;
  bool selected = false;
  bool terminal_success = false;
  std::string terminal_outcome;
  int32_t recorded_at_sec = 0;
  uint32_t recorded_at_nanosec = 0;
  std::string provenance;
};

struct SemanticContextItem
{
  std::string id;
  std::string name;
  std::string provider;
  std::string description;
  std::string type;
  std::string relationship;
  std::string source;
  std::string target;
  std::string edge_type;
  std::string edge_strength_class;
  int level = 0;
  int cluster_id = 0;
  double relevance = 0.0;
  double structural_score = 0.0;
  int32_t matched_runtime_inputs = 0;
  int32_t total_runtime_inputs = 0;
  std::vector<std::string> matched_parameter_keys;
  std::vector<double> feature_vector;
};

struct SemanticContextBundle
{
  std::vector<SemanticContextItem> relevant_capabilities;
  std::vector<SemanticContextItem> related_entities;
  std::vector<SemanticContextItem> dependencies;
  std::vector<SemanticContextItem> plan_patterns;
  std::vector<SemanticContextItem> fault_concepts;
  std::vector<SemanticContextItem> community_summaries;
  double familiarity = 0.0;
};

struct PlanNodeScoreExplanationRecord
{
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
  std::string candidate_node_id;
  std::string skill_name;
  std::string skill_id;
  std::string requested_context_key;
  std::string requested_context_class;
  std::string selected_skill_id;
  std::string selected_key;
  std::string estimator_window;
  std::string fallback_level;
  std::string proxy_source;
  std::string reason;
  double semantic_familiarity = 0.0;
  std::vector<std::string> semantic_relevant_capabilities;
  std::vector<std::string> semantic_related_entities;
  std::vector<std::string> semantic_dependencies;
  std::vector<std::string> semantic_plan_patterns;
  std::vector<std::string> semantic_fault_concepts;
  std::vector<std::string> semantic_community_summaries;
  std::vector<EstimateResult> estimator_outputs;
  int32_t recorded_at_sec = 0;
  uint32_t recorded_at_nanosec = 0;
  std::string provenance;
};

struct PlanNodeScoreExplanationQuery
{
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
};

struct PlanNodeScoreExplanationNodeQuery
{
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
  std::string candidate_node_id;
};

struct PlanFeedbackRecord
{
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
  bool selected = true;
  bool scoring_supported = false;
  bool feasible = false;
  bool sequential_graph = false;
  std::string feasibility_reason;
  double terminal_success_estimate = 0.0;
  double expected_runtime_seconds = 0.0;
  double expected_energy_delta_percentage = 0.0;
  double expected_fault_burden = 0.0;
  double expected_resource_demand = 0.0;
  double expected_uncertainty = 0.0;
  double final_rapfs = 0.0;
  std::string terminal_outcome;
  bool terminal_success = false;
  double runtime_seconds = 0.0;
  double energy_delta_percentage = 0.0;
  int32_t warning_count = 0;
  int32_t error_count = 0;
  std::string human_judgement;
  std::string score_components;
  std::string calibration_version = "baseline";
  std::string normalizer_version = "baseline";
  std::string estimator_version = "baseline";
  std::string weight_version = "baseline";
  int32_t recorded_at_sec = 0;
  uint32_t recorded_at_nanosec = 0;
  std::string provenance;
};

struct ScoringEpochRecord
{
  std::string epoch_id;
  std::string strategy_name;
  std::string calibration_version;
  std::string normalizer_version = "baseline";
  std::string estimator_version = "baseline";
  std::string weight_version = "baseline";
  bool uncertainty_weight_locked = true;
  std::string calibration_summary;
  std::string selection_bias_summary;
  bool active = false;
  int32_t start_sec = 0;
  uint32_t start_nanosec = 0;
  int32_t end_sec = 0;
  uint32_t end_nanosec = 0;
  int32_t recorded_at_sec = 0;
  uint32_t recorded_at_nanosec = 0;
  std::string provenance;
};

struct PlanFeedbackCalibrationSummary
{
  std::string calibration_version;
  std::string normalizer_version;
  std::string estimator_version;
  std::string weight_version;
  int32_t sample_count = 0;
  int32_t success_count = 0;
  int32_t failure_count = 0;
  double mean_predicted_success = 0.0;
  double observed_success_rate = 0.0;
  double brier_score = 0.0;
  double mean_runtime_absolute_error_seconds = 0.0;
  double mean_energy_absolute_error_percentage = 0.0;
  double mean_expected_fault_burden = 0.0;
  double mean_warning_count = 0.0;
  double mean_error_count = 0.0;
  double mean_uncertainty = 0.0;
  double mean_uncertainty_on_success = 0.0;
  double mean_uncertainty_on_failure = 0.0;
};

struct PlanFeedbackSelectionBiasSummary
{
  std::string calibration_version;
  std::string normalizer_version;
  std::string estimator_version;
  std::string weight_version;
  int32_t planning_request_count = 0;
  int32_t total_candidate_count = 0;
  int32_t selected_candidate_count = 0;
  int32_t unselected_candidate_count = 0;
  int32_t feasible_candidate_count = 0;
  int32_t selected_feasible_count = 0;
  double mean_candidates_per_request = 0.0;
  double mean_selected_rapfs = 0.0;
  double mean_unselected_rapfs = 0.0;
  double mean_selected_success_estimate = 0.0;
  double mean_unselected_success_estimate = 0.0;
  double selected_execution_rate = 0.0;
};

struct SelectedPlanScoreExplanationBundle
{
  PlanFeedbackRecord feedback;
  std::vector<PlanNodeScoreExplanationRecord> node_explanations;
};
}// namespace experience
}// namespace fabric