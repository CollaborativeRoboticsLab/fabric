#pragma once

#include <array>
#include <cctype>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include <fabric_base/utils/experience_structs.hpp>

namespace fabric
{
namespace planning
{

enum class ValidationFindingLevel
{
  Reject,
  Warning,
  Penalty,
};

inline const char* validationFindingLevelLabel(const ValidationFindingLevel level)
{
  switch (level)
  {
    case ValidationFindingLevel::Reject:
      return "reject";
    case ValidationFindingLevel::Warning:
      return "warning";
    case ValidationFindingLevel::Penalty:
      return "penalty";
  }
  return "warning";
}

struct Request
{
  std::string planning_request_id;
  std::string task_description;
  std::string uuid;
  bool use_optimization = false;
  bool flush = false;
  int32_t recorded_at_sec = 0;
  uint32_t recorded_at_nanosec = 0;
};

struct RefinementRequest
{
  Request request;
  std::string draft_plan;
  std::string critique;
  int32_t attempt_index = 0;
};

struct CandidatePlanNode
{
  std::string candidate_node_id;
  int32_t runtime_instance_id = -1;
  std::string capability_name;
  std::string provider_name;
  std::string capability_id;
  std::string context_key;
  std::string context_class;
  fabric::experience::CandidateSkillResolution resolution;
  fabric::experience::SemanticContextBundle semantic_context;
  std::vector<fabric::experience::EstimateResult> estimator_outputs;
  std::array<double, 6> feature_vector{ 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
  std::array<bool, 6> feature_available{ false, false, false, false, false, false };
};

struct CandidatePlanEdge
{
  std::string source_node_id;
  std::string target_node_id;
  std::string source_capability_id;
  std::string target_capability_id;
  std::string edge_label;
};

struct CandidatePlanScore
{
  bool supported = false;
  bool feasible = false;
  bool sequential_graph = false;
  std::string reason;
  double terminal_success_estimate = 0.0;
  double expected_runtime_seconds = 0.0;
  double expected_energy_delta_percentage = 0.0;
  double expected_fault_burden = 0.0;
  double expected_resource_demand = 0.0;
  double expected_uncertainty = 0.0;
  double final_rapfs = -std::numeric_limits<double>::infinity();
  std::vector<double> normalized_scores;
  std::vector<double> weighted_scores;
  std::vector<double> score_weights;
  std::string score_components;
  std::string calibration_version = "baseline_v1";
  std::string normalizer_version = "baseline_v1";
  std::string estimator_version = "baseline_v1";
  std::string weight_version = "baseline_v1";
};

struct CandidateValidationFinding
{
  std::string plugin_name;
  ValidationFindingLevel level = ValidationFindingLevel::Warning;
  std::string candidate_node_id;
  std::string source_node_id;
  std::string target_node_id;
  std::string parameter_name;
  std::string message;
  double penalty = 0.0;
};

struct CandidateValidationSummary
{
  bool rejected = false;
  double total_penalty = 0.0;
  std::string summary;
  std::vector<CandidateValidationFinding> findings;
};

struct CandidatePlan
{
  std::string candidate_plan_id;
  std::string graph_hash;
  std::string plan;
  std::string reasoning;
  std::vector<CandidatePlanNode> nodes;
  std::vector<CandidatePlanEdge> edges;
  CandidateValidationSummary validation;
  CandidatePlanScore score;
};

struct Result
{
  std::vector<CandidatePlan> candidates;
  std::string selected_candidate_plan_id;
  std::string selected_plan;
  std::string selected_reasoning;
};

/**
 * @brief Normalize free-form text into a lowercase identifier.
 *
 * This is used anywhere task or skill text must become a stable key for
 * lookups, hashes, or persisted records.
 *
 * @param text Source text to normalize.
 * @return std::string Identifier-safe normalized text.
 */
inline std::string normalize_identifier(const std::string& text)
{
  std::string normalized;
  normalized.reserve(text.size());
  for (const unsigned char ch : text)
  {
    if (std::isalnum(ch))
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    else
      normalized.push_back('_');
  }
  return normalized;
}

/**
 * @brief Tokenize free-form text into lowercase alphanumeric words.
 *
 * Tokenization gives lightweight lexical features that are reused for prompt
 * context selection and ambiguity heuristics without pulling in heavier NLP.
 *
 * @param text Source text to tokenize.
 * @return std::vector<std::string> Token sequence extracted from the text.
 */
inline std::vector<std::string> tokenize(const std::string& text)
{
  std::string normalized;
  normalized.reserve(text.size());
  for (const unsigned char ch : text)
    normalized.push_back(std::isalnum(ch) ? static_cast<char>(std::tolower(ch)) : ' ');

  std::istringstream stream(normalized);
  std::vector<std::string> tokens;
  std::string token;
  while (stream >> token)
    tokens.push_back(token);
  return tokens;
}

/**
 * @brief Build a stable context key for a planning task description.
 *
 * The key keeps repeated or similar requests aligned to the same evidence row
 * so plan scoring can reuse prior observations across executions.
 *
 * @param task_description Natural-language task description.
 * @return std::string Context key derived from the task text.
 */
inline std::string build_context_key(const std::string& task_description)
{
  return "task:" + normalize_identifier(task_description);
}

/**
 * @brief Build a coarse context class label for a planning task description.
 *
 * This provides a softer fallback bucket when an exact task key has no direct
 * evidence but similar requests should still share aggregate statistics.
 *
 * @param task_description Natural-language task description.
 * @return std::string Context class derived from the task text.
 */
inline std::string build_context_class(const std::string& task_description)
{
  const auto tokens = tokenize(task_description);
  return tokens.empty() ? "task_general" : "task:" + tokens.front();
}

/**
 * @brief Hash a candidate plan into a stable graph identity string.
 *
 * The hash lets generated plans, score artifacts, and later supervisor updates
 * refer to the same candidate structure without storing duplicate graph data.
 *
 * @param plan Candidate plan to hash.
 * @return std::string Hash string representing the candidate plan structure.
 */
inline std::string hash_candidate_plan(const CandidatePlan& plan)
{
  std::ostringstream stream;
  stream << plan.candidate_plan_id << '|' << plan.plan;
  for (const auto& node : plan.nodes)
  {
    stream << '|' << node.candidate_node_id << '|' << node.capability_id << '|' << node.provider_name << '|' << node.context_key << '|'
           << node.context_class;
  }
  for (const auto& edge : plan.edges)
    stream << '|' << edge.source_node_id << '|' << edge.target_node_id << '|' << edge.edge_label;
  return std::to_string(std::hash<std::string>{}(stream.str()));
}

}  // namespace planning

}  // namespace fabric