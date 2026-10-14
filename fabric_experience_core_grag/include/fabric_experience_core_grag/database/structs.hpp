#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace experience::database
{

/**
 * @brief Structure to hold document information.
 *
 * @param id Unique identifier.
 * @param text Text content of the document.
 */
struct DocumentInformation
{
  std::string id;
  std::string text;

  /**
   * @brief Construct a document-information record.
   *
   * @param id Unique document identifier.
   * @param text Document text content.
   */
  DocumentInformation(std::string id, std::string text) : id(id), text(text)
  {
  }

  /**
   * @brief Clear the document information.
   */
  void clear()
  {
    id = "";
    text = "";
  }
};

/**
 * @brief Enumeration for different node types.
 *
 * CAPABILITY: Represents a capability.
 * TASK_ENTITY: Represents task-related information such as objectives, constraints, and parameters.
 * CONTEXT_ENTITY: Represents objects, people, places, and conditions in the environment.
 * PLAN_PATTERN: Represents reusable execution structures such as sequences, parallel branches, and recovery paths.
 * FAULT_CONCEPT: Represents possible fault instances summarized from supervisor evidence.
 */
enum class NodeType
{
  CAPABILITY,
  TASK_ENTITY,
  CONTEXT_ENTITY,
  PLAN_PATTERN,
  FAULT_CONCEPT
};

/**
 * @brief Structure to hold node, edge, and community information for database storage.
 *
 * @param id Unique identifier.
 * @param name Name of the node and in capability nodes, this is the interface name.
 * @param provider For capability only, this is the capability provider.
 * @param descriptions For capability only, this is the descriptions of the capability.
 * @param type Type of the node.
 */
struct NodeInformation
{
  std::string id;
  std::string name;
  std::string provider;
  std::string descriptions;
  NodeType type;

  /**
   * @brief Check if the node information is valid.
   */
  bool is_valid() const
  {
    return !name.empty();
  }

  /**
   * @brief Reset node information to an empty default state.
   */
  void clear()
  {
    id = "";
    name = "";
    provider = "";
    descriptions = "";
    type = NodeType::CONTEXT_ENTITY;
  }
};

/**
 * @brief Structure to hold edge information for database storage.
 *
 * @param id Unique identifier.
 * @param relationship Description of the relationship.
 */
struct EdgeInformation
{
  std::string id;
  std::string source;
  std::string target;
  std::string relationship;
  std::string edge_type;
  std::string edge_strength_class;
  double structural_score = 0.0;
  int32_t matched_runtime_inputs = 0;
  int32_t total_runtime_inputs = 0;
  std::vector<std::string> matched_parameter_keys;
  /**
   * @brief Generate a unique key for the edge.
   * @return Unique edge key as a string.
   */
  std::string generate_key()
  {
    return (source < target) ? (source + "|" + target) : (target + "|" + source);
  }

  /**
   * @brief Check whether the edge references both endpoints.
   *
   * @return bool True when both source and target are populated.
   */
  bool is_valid() const
  {
    return !source.empty() && !target.empty();
  }

  /**
   * @brief Reset edge information to an empty default state.
   */
  void clear()
  {
    id = "";
    source = "";
    target = "";
    relationship = "";
    edge_type = "";
    edge_strength_class = "";
    structural_score = 0.0;
    matched_runtime_inputs = 0;
    total_runtime_inputs = 0;
    matched_parameter_keys.clear();
  }
};

/**
 * @brief Structure to hold community information for database storage.
 *
 * @param id Unique identifier.
 * @param documents List of documents associated with the community.
 */
struct CommunityInformation
{
  std::string id;
  std::string description;

  /**
   * @brief Reset community information to an empty default state.
   */
  void clear()
  {
    id = "";
    description = "";
  }
};

/**
 * @brief Capability execution statistics record from supervisor status.
 */
struct SkillRunStatistics
{
  std::string execution_window_id;
  std::string plan_id;
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
  std::string bond_id;
  std::string instance_id;
  std::string skill_id;
  std::string skill_name;
  uint8_t fabric_status;
  bool finalized;
  bool is_active;
  int32_t start_sec;
  uint32_t start_nanosec;
  int32_t stop_sec;
  uint32_t stop_nanosec;
  int32_t runs_started;
  int32_t runs_completed;
  int32_t runs_succeeded;
  int32_t runs_failed;
  int32_t runs_aborted;
  int32_t runs_timed_out;
  int32_t runtime_sample_count;
  double runtime_sum_seconds;
  double runtime_sum_squares_seconds;
  int32_t energy_sample_count;
  double energy_sum_delta_percentage;
  double energy_sum_squares_delta_percentage;
  int32_t warning_count;
  int32_t error_count;
  int32_t fault_exposure_count;
  std::string resource_missing_dimensions;
  int32_t observed_at_sec;
  uint32_t observed_at_nanosec;
  int32_t last_outcome_sec;
  uint32_t last_outcome_nanosec;
  std::string last_outcome_label;
  std::string estimator_window;
  std::string selected_context_key;
  std::string missing_field_flags;
};

/**
 * @brief Context-conditioned statistics for a stable capability ID.
 */
struct SkillContextStatistics
{
  std::string skill_id;
  std::string context_key;
  std::string context_class;
  int32_t runs_started;
  int32_t runs_completed;
  int32_t runs_succeeded;
  int32_t runs_failed;
  int32_t runs_aborted;
  int32_t runs_timed_out;
  int32_t runtime_sample_count;
  double runtime_sum_seconds;
  double runtime_sum_squares_seconds;
  int32_t energy_sample_count;
  double energy_sum_delta_percentage;
  double energy_sum_squares_delta_percentage;
  int32_t warning_count;
  int32_t error_count;
  int32_t fault_exposure_count;
  std::string resource_missing_dimensions;
  std::string estimator_window;
  int32_t updated_at_sec;
  uint32_t updated_at_nanosec;
  std::string provenance;
};

/**
 * @brief Capability-capability transition statistics for graph-aware plan scoring.
 */
struct SkillEdgeStatistics
{
  std::string source_skill_id;
  std::string target_skill_id;
  std::string edge_label;
  std::string context_key;
  int32_t transition_count;
  int32_t success_count;
  int32_t failure_count;
  int32_t recovery_count;
  int32_t runtime_sample_count;
  double runtime_sum_seconds;
  double runtime_sum_squares_seconds;
  int32_t energy_sample_count;
  double energy_sum_delta_percentage;
  double energy_sum_squares_delta_percentage;
  int32_t warning_count;
  int32_t error_count;
  int32_t fault_exposure_count;
  std::string estimator_window;
  int32_t updated_at_sec;
  uint32_t updated_at_nanosec;
  std::string provenance;
};

/**
 * @brief Plan-level outcome and feedback statistics kept separate from capability lifecycle evidence.
 */
struct PlanFeedbackStatistics
{
  std::string planning_request_id;
  std::string candidate_plan_id;
  std::string graph_hash;
  bool selected;
  bool scoring_supported;
  bool feasible;
  bool sequential_graph;
  std::string feasibility_reason;
  double terminal_success_estimate;
  double expected_runtime_seconds;
  double expected_energy_delta_percentage;
  double expected_fault_burden;
  double expected_resource_demand;
  double expected_uncertainty;
  double final_rapfs;
  std::string terminal_outcome;
  bool terminal_success;
  double runtime_seconds;
  double energy_delta_percentage;
  int32_t warning_count;
  int32_t error_count;
  std::string human_judgement;
  std::string score_components;
  std::string calibration_version;
  std::string normalizer_version;
  std::string estimator_version;
  std::string weight_version;
  int32_t recorded_at_sec;
  uint32_t recorded_at_nanosec;
  std::string provenance;
};

/**
 * @brief Versioned scoring epoch record for calibration and weight-history boundaries.
 */
struct ScoringEpochStatistics
{
  std::string epoch_id;
  std::string strategy_name;
  std::string calibration_version;
  std::string normalizer_version;
  std::string estimator_version;
  std::string weight_version;
  bool uncertainty_weight_locked;
  std::string calibration_summary;
  std::string selection_bias_summary;
  bool active;
  int32_t start_sec;
  uint32_t start_nanosec;
  int32_t end_sec;
  uint32_t end_nanosec;
  int32_t recorded_at_sec;
  uint32_t recorded_at_nanosec;
  std::string provenance;
};

/**
 * @brief Aggregated plan-feedback calibration metrics for one scorer-version tuple.
 */
struct PlanFeedbackCalibrationStatistics
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

/**
 * @brief Aggregated candidate exposure metrics for studying scorer selection bias.
 */
struct PlanFeedbackSelectionBiasStatistics
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

/**
 * @brief Per-node attribution record connecting a candidate plan to capability and fallback identity.
 */
struct PlanSkillAttribution
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

/**
 * @brief Per-edge attribution record connecting a candidate plan edge to capability identities.
 */
struct PlanSkillEdgeAttribution
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

/**
 * @brief Queryable per-node explanation of which evidence row was used during scoring.
 */
struct PlanNodeScoreExplanation
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
  std::vector<std::string> estimator_names;
  std::vector<double> estimator_values;
  std::vector<bool> estimator_available;
  std::vector<std::string> estimator_reasons;
  int32_t recorded_at_sec = 0;
  uint32_t recorded_at_nanosec = 0;
  std::string provenance;
};

/**
 * @brief Shared statistical evidence summary selected for capability feature estimation.
 */
struct SkillStatisticsSummary
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
  std::string estimator_window = "prior";
};

enum class StatisticsFallbackLevel
{
  EXACT_CONTEXT,
  CONTEXT_CLASS,
  SKILL_AGGREGATE,
  CAPABILITY_AGGREGATE = SKILL_AGGREGATE,
  SEMANTIC_RELATION,
  WINNING_PLAN_PRIOR,
  PRIOR_DEFAULT
};

/**
 * @brief Fallback trace emitted with score explanations.
 */
struct StatisticsFallbackSelection
{
  StatisticsFallbackLevel level = StatisticsFallbackLevel::PRIOR_DEFAULT;
  std::string skill_id;
  std::string selected_skill_id;
  std::string requested_context_key;
  std::string requested_context_class;
  std::string selected_key = "prior/default";
  std::string estimator_window = "prior";
  std::string proxy_source;
  std::string reason;
  SkillStatisticsSummary statistics;
};

};  // namespace experience::database
