#pragma once

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <tinyxml2.h>

#include <fabric_base/scorer_base.hpp>
#include <fabric_experience_scorer_rapfs/uncertainty_utils.hpp>
#include <fabric_experience_scorer_rapfs/xml_scoring.hpp>

namespace fabric
{

/**
 * @brief RAPFS-based scorer for candidate plans.
 *
 * This scorer combines learned evidence, structural plan semantics, and hard
 * feasibility thresholds so plan selection is both explainable and bounded.
 */
class RapfsScorerPlugin : public ScorerBase
{
public:
  using PlanningRequest = planning::Request;
  using PlanningResult = planning::Result;
  using CandidatePlan = planning::CandidatePlan;
  using CandidatePlanNode = planning::CandidatePlanNode;
  using CandidatePlanEdge = planning::CandidatePlanEdge;
  using CandidatePlanScore = planning::CandidatePlanScore;
  using CandidateSkillRequest = experience::CandidateSkillRequest;
  using EstimatorEvidence = experience::EstimatorEvidence;
  using EstimateResult = experience::EstimateResult;
  using SemanticContextBundle = experience::SemanticContextBundle;
  enum class NonSuccessTerminalPolicy
  {
    Exclude,
    CountAsFailure,
  };

  struct PiecewiseLinearTransform
  {
    std::vector<double> inputs;
    std::vector<double> outputs;

    bool valid() const
    {
      return inputs.size() >= 2 && inputs.size() == outputs.size();
    }

    double apply(double value) const
    {
      if (!valid())
        return 0.0;

      if (value <= inputs.front())
        return outputs.front();
      if (value >= inputs.back())
        return outputs.back();

      for (size_t index = 1; index < inputs.size(); ++index)
      {
        if (value > inputs[index])
          continue;

        const double left_x = inputs[index - 1];
        const double right_x = inputs[index];
        const double left_y = outputs[index - 1];
        const double right_y = outputs[index];
        const double span = right_x - left_x;
        if (span <= 0.0)
          return right_y;
        const double ratio = (value - left_x) / span;
        return left_y + ratio * (right_y - left_y);
      }

      return outputs.back();
    }
  };

  struct FeasibilityConfig
  {
    double min_success_probability = 0.20;
    double max_runtime_seconds = 300.0;
    double max_energy_delta_percentage = 50.0;
    double max_fault_burden = 1.5;
    double max_resource_demand = 1.0;
    double max_uncertainty = 0.95;
  };

  /**
   * @brief Initialize scorer state and load configurable scoring parameters.
    *
    * Most RAPFS behavior is parameterized so deployments can retune weighting
    * and feasibility policy without changing code.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "RapfsScorerPlugin");
    loadScoringConfig();
  }

  /**
   * @brief Score all candidate plans and choose the best feasible candidate.
    *
    * Feasible candidates always outrank infeasible ones; when none are feasible,
    * the best unsupported fallback score is still returned for observability.
   *
   * @param request Planning request associated with the candidates.
   * @param planning_result Candidate plans to score and update in place.
   */
  void scorePlanningResult(const PlanningRequest& request, PlanningResult& planning_result) const override
  {
    double best_score = -std::numeric_limits<double>::infinity();
    size_t best_index = 0;
    bool found_feasible = false;
    bool found_selectable = false;

    for (size_t index = 0; index < planning_result.candidates.size(); ++index)
    {
      auto& candidate = planning_result.candidates[index];
      scoreCandidatePlan(request, candidate);

      if (candidate.validation.rejected)
        continue;

      const double candidate_score = candidate.score.final_rapfs;
      found_selectable = true;
      if (candidate.score.feasible)
      {
        if (!found_feasible || candidate_score > best_score)
        {
          best_score = candidate_score;
          best_index = index;
          found_feasible = true;
        }
      }
      else if (!found_feasible && (index == 0 || candidate_score > best_score))
      {
        best_score = candidate_score;
        best_index = index;
      }
    }

    if (found_selectable)
    {
      planning_result.selected_candidate_plan_id = planning_result.candidates[best_index].candidate_plan_id;
      planning_result.selected_plan = planning_result.candidates[best_index].plan;
      planning_result.selected_reasoning = planning_result.candidates[best_index].reasoning;
      return;
    }

    planning_result.selected_candidate_plan_id.clear();
    planning_result.selected_plan.clear();
    planning_result.selected_reasoning.clear();
  }

protected:
  using XmlScoreNode = scoring::XmlScoreNode;
  using AggregateScoreSummary = scoring::AggregateScoreSummary;

  /**
   * @brief Load scorer weights, feasibility thresholds, and normalization transforms from parameters.
    *
    * Keeping these values in ROS parameters makes the scoring policy inspectable
    * and tunable across robots, tasks, and evaluation runs.
   */
  void loadScoringConfig()
  {
    const std::vector<std::string> default_estimator_names = {
      "success", "time", "energy", "fault", "resource", "uncertainty"
    };

    auto declare_if_missing = [this](const auto& parameter_name, const auto& default_value) {
      if (!node_->has_parameter(parameter_name))
        node_->declare_parameter(parameter_name, default_value);
    };

    declare_if_missing(plugin_name_ + ".calibration_version", std::string("baseline_v1"));
    declare_if_missing(plugin_name_ + ".normalizer_version", std::string("calibrated_v1"));
    declare_if_missing(plugin_name_ + ".weight_version", std::string("baseline_v2"));
    declare_if_missing(plugin_name_ + ".estimator_version", std::string("baseline_v1"));
    declare_if_missing(plugin_name_ + ".non_success_terminal_policy", std::string("count_as_failure"));
    declare_if_missing(plugin_name_ + ".estimator_names", default_estimator_names);
    declare_if_missing(plugin_name_ + ".plan_uncertainty_aggregation", std::string("scenario_weighted_mean"));
    declare_if_missing(plugin_name_ + ".default_success_probability", 0.5);
    declare_if_missing(plugin_name_ + ".default_uncertainty", 1.0);
    declare_if_missing(plugin_name_ + ".uncertainty_statistical_weight", 0.40);
    declare_if_missing(plugin_name_ + ".uncertainty_task_ambiguity_weight", 0.15);
    declare_if_missing(plugin_name_ + ".uncertainty_unfamiliarity_weight", 0.25);
    declare_if_missing(plugin_name_ + ".uncertainty_missing_evidence_weight", 0.20);
    declare_if_missing(plugin_name_ + ".uncertainty_weight_locked", true);
    declare_if_missing(plugin_name_ + ".uncertainty_weight_target", std::string(""));
    declare_if_missing(plugin_name_ + ".unknown_unfamiliarity_prior", 0.75);
    declare_if_missing(plugin_name_ + ".feasibility.min_success_probability",
                       feasibility_config_.min_success_probability);
    declare_if_missing(plugin_name_ + ".feasibility.max_runtime_seconds", feasibility_config_.max_runtime_seconds);
    declare_if_missing(plugin_name_ + ".feasibility.max_energy_delta_percentage",
                       feasibility_config_.max_energy_delta_percentage);
    declare_if_missing(plugin_name_ + ".feasibility.max_fault_burden", feasibility_config_.max_fault_burden);
    declare_if_missing(plugin_name_ + ".feasibility.max_resource_demand", feasibility_config_.max_resource_demand);
    declare_if_missing(plugin_name_ + ".feasibility.max_uncertainty", feasibility_config_.max_uncertainty);

    calibration_version_ = node_->get_parameter(plugin_name_ + ".calibration_version").as_string();
    normalizer_version_ = node_->get_parameter(plugin_name_ + ".normalizer_version").as_string();
    weight_version_ = node_->get_parameter(plugin_name_ + ".weight_version").as_string();
    configured_estimator_version_ = node_->get_parameter(plugin_name_ + ".estimator_version").as_string();
    estimator_names_ = node_->get_parameter(plugin_name_ + ".estimator_names").as_string_array();
    non_success_terminal_policy_ = parseNonSuccessTerminalPolicy(
      node_->get_parameter(plugin_name_ + ".non_success_terminal_policy").as_string());
    estimator_version_ = configured_estimator_version_ + "+terminal_policy:" + nonSuccessTerminalPolicyLabel();
    uncertainty_aggregation_mode_ =
      node_->get_parameter(plugin_name_ + ".plan_uncertainty_aggregation").as_string();
    default_success_probability_ = node_->get_parameter(plugin_name_ + ".default_success_probability").as_double();
    default_uncertainty_ = node_->get_parameter(plugin_name_ + ".default_uncertainty").as_double();
    uncertainty_statistical_weight_ = node_->get_parameter(plugin_name_ + ".uncertainty_statistical_weight").as_double();
    uncertainty_task_ambiguity_weight_ = node_->get_parameter(plugin_name_ + ".uncertainty_task_ambiguity_weight").as_double();
    uncertainty_unfamiliarity_weight_ = node_->get_parameter(plugin_name_ + ".uncertainty_unfamiliarity_weight").as_double();
    uncertainty_missing_evidence_weight_ = node_->get_parameter(plugin_name_ + ".uncertainty_missing_evidence_weight").as_double();
    uncertainty_weight_locked_ = node_->get_parameter(plugin_name_ + ".uncertainty_weight_locked").as_bool();
    uncertainty_weight_target_ = node_->get_parameter(plugin_name_ + ".uncertainty_weight_target").as_string();
    unknown_unfamiliarity_prior_ = node_->get_parameter(plugin_name_ + ".unknown_unfamiliarity_prior").as_double();
    weight_success_ = resolveEstimatorWeight("success", 0.40);
    weight_time_ = resolveEstimatorWeight("time", 0.15);
    weight_energy_ = resolveEstimatorWeight("energy", 0.10);
    weight_fault_ = resolveEstimatorWeight("fault", 0.10);
    weight_resource_ = resolveEstimatorWeight("resource", 0.05);
    weight_uncertainty_ = resolveEstimatorWeight("uncertainty", 0.20);
    feasibility_config_.min_success_probability =
      node_->get_parameter(plugin_name_ + ".feasibility.min_success_probability").as_double();
    feasibility_config_.max_runtime_seconds = node_->get_parameter(plugin_name_ + ".feasibility.max_runtime_seconds").as_double();
    feasibility_config_.max_energy_delta_percentage =
      node_->get_parameter(plugin_name_ + ".feasibility.max_energy_delta_percentage").as_double();
    feasibility_config_.max_fault_burden = node_->get_parameter(plugin_name_ + ".feasibility.max_fault_burden").as_double();
    feasibility_config_.max_resource_demand =
      node_->get_parameter(plugin_name_ + ".feasibility.max_resource_demand").as_double();
    feasibility_config_.max_uncertainty = node_->get_parameter(plugin_name_ + ".feasibility.max_uncertainty").as_double();

    if (uncertainty_aggregation_mode_ != "scenario_weighted_mean" &&
        uncertainty_aggregation_mode_ != "reachable_node_max")
    {
      RCLCPP_WARN(node_->get_logger(),
                  "Invalid plan uncertainty aggregation '%s'; falling back to scenario_weighted_mean.",
                  uncertainty_aggregation_mode_.c_str());
      uncertainty_aggregation_mode_ = "scenario_weighted_mean";
    }

    if (uncertainty_weight_target_.empty() && !uncertainty_weight_locked_)
    {
      RCLCPP_WARN(node_->get_logger(),
                  "uncertainty_weight_locked=false without a separate uncertainty target; forcing policy lock for w_u.");
      uncertainty_weight_locked_ = true;
    }

    runtime_transform_ = loadTransform(plugin_name_ + ".transforms.runtime_seconds",
                                       { 0.0, 30.0, 60.0, 120.0, 300.0 },
                                       { 0.0, 0.15, 0.35, 0.70, 1.0 });
    energy_transform_ = loadTransform(plugin_name_ + ".transforms.energy_delta_percentage",
                                      { 0.0, 5.0, 10.0, 25.0, 50.0 },
                                      { 0.0, 0.15, 0.35, 0.75, 1.0 });
    fault_transform_ = loadTransform(plugin_name_ + ".transforms.fault_burden",
                                     { 0.0, 0.25, 0.50, 1.0, 2.0 },
                                     { 0.0, 0.15, 0.35, 0.75, 1.0 });
    resource_transform_ = loadTransform(plugin_name_ + ".transforms.resource_demand",
                                        { 0.0, 0.25, 0.50, 1.0, 2.0 },
                                        { 0.0, 0.15, 0.40, 0.75, 1.0 });
    familiarity_transform_ = loadTransform(plugin_name_ + ".transforms.semantic_familiarity",
                                           { 0.0, 0.25, 0.50, 0.75, 1.0 },
                                           { 1.0, 0.80, 0.50, 0.20, 0.0 });
  }

  /**
   * @brief Resolve one RAPFS weight from nested estimator config.
   */
  double resolveEstimatorWeight(const std::string& estimator_name, double default_value)
  {
    const std::string nested_key = plugin_name_ + ".estimators." + estimator_name + ".weight";
    node_->declare_parameter<double>(nested_key, default_value);
    return node_->get_parameter(nested_key).as_double();
  }

  /**
   * @brief Load a configurable piecewise-linear transform or fall back to defaults.
    *
    * The transform converts raw units like seconds or battery delta into a
    * normalized penalty scale that can be combined inside RAPFS.
   *
   * @param default_inputs Default x-axis breakpoints.
   * @param default_outputs Default y-axis values.
   * @return PiecewiseLinearTransform Loaded transform.
   */
  PiecewiseLinearTransform loadTransform(const std::string& param_prefix,
                                         const std::vector<double>& default_inputs,
                                         const std::vector<double>& default_outputs)
  {
    node_->declare_parameter<std::vector<double>>(param_prefix + ".inputs", default_inputs);
    node_->declare_parameter<std::vector<double>>(param_prefix + ".outputs", default_outputs);

    PiecewiseLinearTransform transform;
    transform.inputs = node_->get_parameter(param_prefix + ".inputs").as_double_array();
    transform.outputs = node_->get_parameter(param_prefix + ".outputs").as_double_array();
    if (!transform.valid())
    {
      RCLCPP_WARN(node_->get_logger(), "Invalid scoring transform at '%s'; falling back to defaults.", param_prefix.c_str());
      transform.inputs = default_inputs;
      transform.outputs = default_outputs;
    }
    return transform;
  }

  /**
   * @brief Run all configured estimator plugins for one node's evidence.
    *
    * The scorer keeps estimators decoupled so each feature can evolve
    * independently while still producing one comparable node feature vector.
   *
   * @param evidence Evidence bundle for the candidate node.
   * @return std::vector<EstimateResult> Estimator outputs for the node.
   */
  std::vector<EstimateResult> estimateNodeOutputs(const EstimatorEvidence& evidence) const
  {
    std::vector<EstimateResult> outputs;
    outputs.reserve(estimator_plugins_ != nullptr ? estimator_plugins_->size() : 0);
    if (estimator_plugins_ == nullptr)
      return outputs;
    for (const auto& estimator : *estimator_plugins_)
    {
      const EstimatorEvidence adapted_evidence = adaptEvidenceForEstimator(estimator->plugin_name(), evidence);
      auto output = estimator->estimate(adapted_evidence);
      annotateOutcomePolicyReason(estimator->plugin_name(), output);
      outputs.push_back(std::move(output));
    }
    return outputs;
  }

  EstimatorEvidence adaptEvidenceForEstimator(const std::string& estimator_plugin_name,
                                              const EstimatorEvidence& evidence) const
  {
    if (non_success_terminal_policy_ != NonSuccessTerminalPolicy::CountAsFailure)
      return evidence;

    if (estimator_plugin_name != "SuccessProbabilityPlugin" && estimator_plugin_name != "UncertaintyPlugin")
      return evidence;

    EstimatorEvidence adapted = evidence;
    const int32_t non_success_terminals = std::max(0, evidence.runs_aborted) + std::max(0, evidence.runs_timed_out);
    adapted.runs_completed = std::max(0, evidence.runs_completed) + non_success_terminals;
    adapted.runs_failed = std::max(0, evidence.runs_failed) + non_success_terminals;
    return adapted;
  }

  void annotateOutcomePolicyReason(const std::string& estimator_plugin_name, EstimateResult& output) const
  {
    if (estimator_plugin_name != "SuccessProbabilityPlugin" && estimator_plugin_name != "UncertaintyPlugin")
      return;

    if (!output.reason.empty())
      output.reason += "; ";
    output.reason += "non-success terminal policy=" + nonSuccessTerminalPolicyLabel();
  }

  NonSuccessTerminalPolicy parseNonSuccessTerminalPolicy(const std::string& value) const
  {
    if (value == "exclude")
      return NonSuccessTerminalPolicy::Exclude;
    if (value == "count_as_failure")
      return NonSuccessTerminalPolicy::CountAsFailure;

    RCLCPP_WARN(node_->get_logger(), "Invalid non-success terminal policy '%s'; falling back to count_as_failure.",
                value.c_str());
    return NonSuccessTerminalPolicy::CountAsFailure;
  }

  std::string nonSuccessTerminalPolicyLabel() const
  {
    switch (non_success_terminal_policy_)
    {
      case NonSuccessTerminalPolicy::Exclude:
        return "exclude";
      case NonSuccessTerminalPolicy::CountAsFailure:
      default:
        return "count_as_failure";
    }
  }

  static std::string escapeJsonString(const std::string& value)
  {
    std::ostringstream escaped;
    for (const char ch : value)
    {
      switch (ch)
      {
        case '"':
          escaped << "\\\"";
          break;
        case '\\':
          escaped << "\\\\";
          break;
        case '\b':
          escaped << "\\b";
          break;
        case '\f':
          escaped << "\\f";
          break;
        case '\n':
          escaped << "\\n";
          break;
        case '\r':
          escaped << "\\r";
          break;
        case '\t':
          escaped << "\\t";
          break;
        default:
          if (static_cast<unsigned char>(ch) < 0x20)
            escaped << "\\u" << std::hex << std::uppercase << static_cast<int>(static_cast<unsigned char>(ch))
                    << std::dec << std::nouppercase;
          else
            escaped << ch;
          break;
      }
    }
    return escaped.str();
  }

  static void appendJsonNumberOrNull(std::ostringstream& stream, const double value)
  {
    if (std::isfinite(value))
      stream << value;
    else
      stream << "null";
  }

  /**
   * @brief Resolve node evidence and compute a final score for one candidate plan.
    *
    * This is the main scoring pipeline: normalize identity, gather evidence,
    * populate features, score topology, then persist a single candidate score.
   *
   * @param request Planning request associated with the candidate.
   * @param candidate Candidate plan to score in place.
   */
  void scoreCandidatePlan(const PlanningRequest& request, CandidatePlan& candidate) const
  {
    if (candidate.validation.rejected)
    {
      candidate.score = rejectedValidationScore(candidate);
      candidate.score.score_components = serializeRejectedScoreComponents(candidate);
      return;
    }

    resolveCandidateNodes(request, candidate);
    for (auto& node : candidate.nodes)
      populateNodeFeatures(request, node);

    AggregateScoreSummary summary = scoreXmlCandidate(candidate);
    if (!summary.supported)
      summary = scoreSequentialFallbackCandidate(candidate);

    summary.expected_uncertainty = aggregatePlanUncertainty(summary);

    candidate.score = finalizeCandidateScore(summary);
    applyValidationPenalty(candidate);
    candidate.score.score_components = serializeScoreComponents(candidate, summary);
  }

  CandidatePlanScore rejectedValidationScore(const CandidatePlan& candidate) const
  {
    CandidatePlanScore score;
    score.supported = true;
    score.feasible = false;
    score.sequential_graph = false;
    score.reason = candidate.validation.summary.empty() ? "candidate rejected by validation" : candidate.validation.summary;
    score.final_rapfs = -std::numeric_limits<double>::infinity();
    score.calibration_version = calibration_version_;
    score.normalizer_version = normalizer_version_;
    score.estimator_version = estimator_version_;
    score.weight_version = weight_version_;
    score.score_weights = { weight_success_, weight_time_, weight_energy_, weight_fault_, weight_resource_,
                            weight_uncertainty_ };
    return score;
  }

  void populateScoreBreakdownArrays(CandidatePlanScore& score) const
  {
    const double normalized_runtime = runtime_transform_.apply(std::max(0.0, score.expected_runtime_seconds));
    const double normalized_energy = energy_transform_.apply(std::max(0.0, score.expected_energy_delta_percentage));
    const double normalized_fault = fault_transform_.apply(std::max(0.0, score.expected_fault_burden));
    const double normalized_resource = resource_transform_.apply(std::max(0.0, score.expected_resource_demand));
    const double normalized_uncertainty = std::clamp(score.expected_uncertainty, 0.0, 1.0);

    score.score_weights = { weight_success_, weight_time_, weight_energy_, weight_fault_, weight_resource_,
                            weight_uncertainty_ };
    score.normalized_scores = { score.terminal_success_estimate, normalized_runtime, normalized_energy,
                                normalized_fault, normalized_resource, normalized_uncertainty };
    score.weighted_scores = { weight_success_ * score.terminal_success_estimate,
                              -weight_time_ * normalized_runtime,
                              -weight_energy_ * normalized_energy,
                              -weight_fault_ * normalized_fault,
                              -weight_resource_ * normalized_resource,
                              -weight_uncertainty_ * normalized_uncertainty };
  }

  void applyValidationPenalty(CandidatePlan& candidate) const
  {
    if (candidate.validation.total_penalty <= 0.0 || !candidate.score.feasible ||
        !std::isfinite(candidate.score.final_rapfs))
      return;

    candidate.score.final_rapfs -= candidate.validation.total_penalty;
    if (candidate.score.reason.empty())
      candidate.score.reason = "validation penalty applied";
    else
      candidate.score.reason += "; validation penalty applied";
  }

  /**
   * @brief Resolve stable skill identities and context for all candidate nodes.
    *
    * Evidence lookups depend on normalized skill IDs and task-derived context,
    * so node metadata is completed here before any estimator runs.
   *
   * @param request Planning request associated with the candidate.
   * @param candidate Candidate plan whose nodes should be resolved.
   */
  void resolveCandidateNodes(const PlanningRequest& request, CandidatePlan& candidate) const
  {
    const std::string default_context_key = planning::build_context_key(request.task_description);
    const std::string default_context_class = planning::build_context_class(request.task_description);

    for (size_t index = 0; index < candidate.nodes.size(); ++index)
    {
      auto& node = candidate.nodes[index];
      if (node.capability_id.empty())
        node.capability_id = planning::normalize_identifier(node.capability_name);
      if (node.context_key.empty())
        node.context_key = default_context_key;
      if (node.context_class.empty())
        node.context_class = default_context_class;
      if (node.candidate_node_id.empty())
        node.candidate_node_id = "runner_" + std::to_string(index) + "_" + node.capability_id;

      node.resolution = core_plugin_->resolveCandidateSkill(CandidateSkillRequest{
        request.planning_request_id, candidate.candidate_plan_id, candidate.graph_hash, node.candidate_node_id,
        node.capability_name, node.capability_id, node.context_key, node.context_class
      });
    }

    if (candidate.graph_hash.empty())
      candidate.graph_hash = planning::hash_candidate_plan(candidate);
  }

  /**
   * @brief Populate the feature vector and estimator outputs for a candidate node.
    *
    * The resulting feature vector is the common interface between evidence
    * estimators and graph-aware scenario scoring.
   *
   * @param request Planning request providing task context.
   * @param node Candidate node to enrich in place.
   */
  void populateNodeFeatures(const PlanningRequest& request, CandidatePlanNode& node) const
  {
    node.semantic_context = core_plugin_->retrieveSemanticContext(request.task_description, node.capability_name);
    node.estimator_outputs = estimateNodeOutputs(node.resolution.evidence);
    node.feature_vector = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    node.feature_available = { false, false, false, false, false, false };
    const double task_ambiguity = scoring::compute_task_ambiguity(request.task_description);
    const double task_unfamiliarity = computeTaskUnfamiliarity(node.semantic_context);
    const double missing_evidence = scoring::compute_missing_evidence(node.resolution.evidence);
    double statistical_uncertainty = default_uncertainty_;
    bool statistical_uncertainty_available = false;
    std::string statistical_uncertainty_reason = "uncertainty estimator unavailable";

    for (auto& output : node.estimator_outputs)
    {
      if (output.name == "uncertainty")
      {
        statistical_uncertainty = std::clamp(output.value, 0.0, 1.0);
        statistical_uncertainty_available = output.available;
        statistical_uncertainty_reason = output.reason;
      }

      if (output.name == "success_probability")
      {
        node.feature_vector[0] = output.available ? output.value : default_success_probability_;
        node.feature_available[0] = output.available;
      }
      else if (output.name == "runtime_seconds")
      {
        node.feature_vector[1] = output.value;
        node.feature_available[1] = output.available;
      }
      else if (output.name == "energy_delta_percentage")
      {
        node.feature_vector[2] = output.value;
        node.feature_available[2] = output.available;
      }
      else if (output.name == "fault_burden")
      {
        node.feature_vector[3] = output.value;
        node.feature_available[3] = output.available;
      }
      else if (output.name == "resource_demand")
      {
        node.feature_vector[4] = output.value;
        node.feature_available[4] = output.available;
      }
    }

    const scoring::UncertaintyWeights uncertainty_weights{
      uncertainty_statistical_weight_, uncertainty_task_ambiguity_weight_, uncertainty_unfamiliarity_weight_,
      uncertainty_missing_evidence_weight_, default_uncertainty_,
    };
    const double combined_uncertainty = scoring::compose_uncertainty(
      uncertainty_weights, statistical_uncertainty, task_ambiguity, task_unfamiliarity, missing_evidence);
    const std::string uncertainty_reason = scoring::build_uncertainty_reason(
      statistical_uncertainty, task_ambiguity, task_unfamiliarity, missing_evidence, statistical_uncertainty_reason);
    upsertEstimateResult(node.estimator_outputs,
                         EstimateResult{ "uncertainty", combined_uncertainty, true, uncertainty_reason });
    node.estimator_outputs.push_back(EstimateResult{
      "uncertainty_statistical", statistical_uncertainty,
      statistical_uncertainty_available || statistical_uncertainty > 0.0, statistical_uncertainty_reason,
    });
    node.estimator_outputs.push_back(
      EstimateResult{ "uncertainty_task_ambiguity", task_ambiguity, true, "task-description ambiguity heuristic" });
    node.estimator_outputs.push_back(EstimateResult{
      "uncertainty_unfamiliarity", task_unfamiliarity, true, "semantic familiarity transformed into unfamiliarity",
    });
    node.estimator_outputs.push_back(
      EstimateResult{ "uncertainty_missing_evidence", missing_evidence, true, "fraction of missing estimator inputs" });
    node.feature_vector[5] = combined_uncertainty;
    node.feature_available[5] = true;
  }

  /**
   * @brief Convert an aggregate summary into a persisted candidate score record.
   *
   * @param summary Aggregate scenario summary for the candidate.
   * @return CandidatePlanScore Finalized score record.
   */
  CandidatePlanScore finalizeCandidateScore(const AggregateScoreSummary& summary) const
  {
    CandidatePlanScore score;
    score.supported = summary.supported;
    score.sequential_graph = summary.sequential_graph;
    score.reason = summary.reason;
    score.terminal_success_estimate = summary.terminal_success_estimate;
    score.expected_runtime_seconds = summary.expected_runtime_seconds;
    score.expected_energy_delta_percentage = summary.expected_energy_delta_percentage;
    score.expected_fault_burden = summary.expected_fault_burden;
    score.expected_resource_demand = summary.expected_resource_demand;
    score.expected_uncertainty = summary.expected_uncertainty;
    score.calibration_version = calibration_version_;
    score.normalizer_version = normalizer_version_;
    score.estimator_version = estimator_version_;
    score.weight_version = weight_version_;
    populateScoreBreakdownArrays(score);

    if (!summary.supported)
    {
      score.feasible = false;
      score.final_rapfs = -std::numeric_limits<double>::infinity();
      score.reason = score.reason.empty() ? "unsupported candidate graph shape" : score.reason;
      return score;
    }

    const double normalized_runtime = score.normalized_scores[1];
    const double normalized_energy = score.normalized_scores[2];
    const double normalized_fault = score.normalized_scores[3];
    const double normalized_resource = score.normalized_scores[4];
    const double normalized_uncertainty = score.normalized_scores[5];

    score.feasible = applyFeasibilityThresholds(score);
    if (!score.feasible)
    {
      score.final_rapfs = -std::numeric_limits<double>::infinity();
      return score;
    }

    score.final_rapfs = weight_success_ * score.terminal_success_estimate - weight_time_ * normalized_runtime -
                        weight_energy_ * normalized_energy - weight_fault_ * normalized_fault -
                        weight_resource_ * normalized_resource - weight_uncertainty_ * normalized_uncertainty;
    return score;
  }

  /**
   * @brief Apply hard feasibility thresholds to a candidate score.
   *
   * @param score Candidate score to validate and annotate.
   * @return bool True when the score satisfies all thresholds.
   */
  bool applyFeasibilityThresholds(CandidatePlanScore& score) const
  {
    if (score.terminal_success_estimate < feasibility_config_.min_success_probability)
    {
      score.reason = "predicted success below minimum threshold";
      return false;
    }
    if (score.expected_runtime_seconds > feasibility_config_.max_runtime_seconds)
    {
      score.reason = "predicted runtime exceeds budget";
      return false;
    }
    if (score.expected_energy_delta_percentage > feasibility_config_.max_energy_delta_percentage)
    {
      score.reason = "predicted energy exceeds budget";
      return false;
    }
    if (score.expected_fault_burden > feasibility_config_.max_fault_burden)
    {
      score.reason = "predicted fault burden exceeds threshold";
      return false;
    }
    if (score.expected_resource_demand > feasibility_config_.max_resource_demand)
    {
      score.reason = "predicted resource demand exceeds budget";
      return false;
    }
    if (score.expected_uncertainty > feasibility_config_.max_uncertainty)
    {
      score.reason = "predicted uncertainty exceeds threshold";
      return false;
    }
    return true;
  }

  /**
   * @brief Score a candidate plan by interpreting its XML control-flow structure.
   *
   * @param candidate Candidate plan to score.
   * @return AggregateScoreSummary XML-aware score summary.
   */
  AggregateScoreSummary scoreXmlCandidate(const CandidatePlan& candidate) const
  {
    std::string reason;
    const auto scoring_tree = buildScoringTree(candidate, reason);
    if (!scoring_tree.has_value())
    {
      AggregateScoreSummary unsupported;
      unsupported.reason = reason.empty() ? "failed to parse candidate XML into a scoring tree" : reason;
      return unsupported;
    }

    auto summary = scoreXmlNode(scoring_tree.value());
    if (!summary.supported && summary.reason.empty())
      summary.reason = "unsupported control-flow composition in XML scorer";
    return summary;
  }

  /**
   * @brief Score a candidate plan as a simple sequential graph fallback.
   *
   * @param candidate Candidate plan to score.
   * @return AggregateScoreSummary Sequential fallback summary.
   */
  AggregateScoreSummary scoreSequentialFallbackCandidate(const CandidatePlan& candidate) const
  {
    AggregateScoreSummary summary;
    summary.supported = buildSequentialOrdering(candidate, summary.ordered_nodes, summary.reason);
    summary.sequential_graph = summary.supported;
    summary.topology = summary.supported ? "sequential_fallback" : "unsupported";
    if (!summary.supported)
      return summary;

    summary.terminal_success_estimate = 1.0;
    summary.expected_uncertainty = 0.0;
    for (const auto* node : summary.ordered_nodes)
    {
      const double success_value = node->feature_available[0] ? node->feature_vector[0] : default_success_probability_;
      summary.terminal_success_estimate *= std::clamp(success_value, 0.0, 1.0);
      summary.expected_runtime_seconds += std::max(0.0, node->feature_vector[1]);
      summary.expected_energy_delta_percentage += std::max(0.0, node->feature_vector[2]);
      summary.expected_fault_burden += std::max(0.0, node->feature_vector[3]);
      summary.expected_resource_demand = std::max(summary.expected_resource_demand, std::max(0.0, node->feature_vector[4]));
      summary.expected_uncertainty = std::max(summary.expected_uncertainty, std::clamp(node->feature_vector[5], 0.0, 1.0));
    }
    summary.reason = "sequential edge-graph fallback";
    return summary;
  }

  /**
   * @brief Build a sequential execution ordering from candidate nodes and edges.
   *
   * @param candidate Candidate plan whose graph should be linearized.
   * @param ordered_nodes Output sequence of ordered nodes.
   * @param reason Output reason when ordering fails.
   * @return bool True when a valid sequential ordering was produced.
   */
  bool buildSequentialOrdering(const CandidatePlan& candidate, std::vector<const CandidatePlanNode*>& ordered_nodes,
                               std::string& reason) const
  {
    ordered_nodes.clear();
    if (candidate.nodes.empty())
    {
      reason = "candidate contains no executable nodes";
      return false;
    }

    if (candidate.nodes.size() == 1 && candidate.edges.empty())
    {
      ordered_nodes.push_back(&candidate.nodes.front());
      return true;
    }

    std::unordered_map<std::string, const CandidatePlanNode*> node_by_id;
    std::unordered_map<std::string, int> indegree;
    std::unordered_map<std::string, int> outdegree;
    std::unordered_map<std::string, const CandidatePlanEdge*> next_edge;
    for (const auto& node : candidate.nodes)
    {
      node_by_id.emplace(node.candidate_node_id, &node);
      indegree.emplace(node.candidate_node_id, 0);
      outdegree.emplace(node.candidate_node_id, 0);
    }

    if (candidate.edges.size() != candidate.nodes.size() - 1)
    {
      reason = "sequential baseline expects exactly N-1 edges";
      return false;
    }

    for (const auto& edge : candidate.edges)
    {
      if (edge.edge_label != "on_success")
      {
        reason = "sequential baseline supports only on_success edges";
        return false;
      }
      if (!node_by_id.count(edge.source_node_id) || !node_by_id.count(edge.target_node_id))
      {
        reason = "candidate edge references an unknown node";
        return false;
      }
      if (++outdegree[edge.source_node_id] > 1 || ++indegree[edge.target_node_id] > 1)
      {
        reason = "sequential baseline requires indegree and outdegree <= 1";
        return false;
      }
      next_edge[edge.source_node_id] = &edge;
    }

    std::string start_node_id;
    int start_count = 0;
    int end_count = 0;
    for (const auto& node : candidate.nodes)
    {
      if (indegree[node.candidate_node_id] == 0)
      {
        start_node_id = node.candidate_node_id;
        ++start_count;
      }
      if (outdegree[node.candidate_node_id] == 0)
        ++end_count;
    }

    if (start_count != 1 || end_count != 1)
    {
      reason = "sequential baseline requires exactly one start node and one end node";
      return false;
    }

    std::unordered_set<std::string> visited;
    std::string current = start_node_id;
    while (!current.empty())
    {
      if (!visited.insert(current).second)
      {
        reason = "cycle detected in candidate graph";
        return false;
      }
      ordered_nodes.push_back(node_by_id.at(current));
      const auto edge_it = next_edge.find(current);
      if (edge_it == next_edge.end())
        break;
      current = edge_it->second->target_node_id;
    }

    if (visited.size() != candidate.nodes.size())
    {
      reason = "candidate graph is disconnected or branched";
      return false;
    }
    return true;
  }

  /**
   * @brief Transform semantic familiarity into a task-unfamiliarity score.
   *
   * @param semantic_context Retrieved semantic context for the task-skill pair.
   * @return double Normalized unfamiliarity score in the range [0, 1].
   */
  double computeTaskUnfamiliarity(const SemanticContextBundle& semantic_context) const
  {
    const double semantic_familiarity = semantic_context.familiarity;
    if (semantic_familiarity <= 0.0)
      return unknown_unfamiliarity_prior_;
    return std::clamp(familiarity_transform_.apply(std::clamp(semantic_familiarity, 0.0, 1.0)), 0.0, 1.0);
  }

  /**
   * @brief Replace or append an estimator output by name.
   *
   * @param outputs Output vector to update.
   * @param replacement Replacement estimator result.
   */
  static void upsertEstimateResult(std::vector<EstimateResult>& outputs, EstimateResult replacement)
  {
    for (auto& output : outputs)
    {
      if (output.name != replacement.name)
        continue;
      output = std::move(replacement);
      return;
    }
    outputs.push_back(std::move(replacement));
  }

  /**
   * @brief Aggregate plan-level uncertainty from reachable scenarios under the configured policy.
   *
   * Supported modes are:
   * - scenario_weighted_mean: probability-weighted average of per-scenario reachable-node maxima
   * - reachable_node_max: worst-case maximum over reachable execution scenarios
   */
  double aggregatePlanUncertainty(const AggregateScoreSummary& summary) const
  {
    if (summary.scenarios.empty())
      return std::clamp(summary.expected_uncertainty, 0.0, 1.0);

    if (uncertainty_aggregation_mode_ == "reachable_node_max")
    {
      double reachable_max = 0.0;
      for (const auto& scenario : summary.scenarios)
        reachable_max = std::max(reachable_max, std::clamp(scenario.uncertainty, 0.0, 1.0));
      return reachable_max;
    }

    double weighted_sum = 0.0;
    double probability_sum = 0.0;
    for (const auto& scenario : summary.scenarios)
    {
      const double probability = std::clamp(scenario.probability, 0.0, 1.0);
      weighted_sum += probability * std::clamp(scenario.uncertainty, 0.0, 1.0);
      probability_sum += probability;
    }

    if (probability_sum <= 0.0)
      return std::clamp(summary.expected_uncertainty, 0.0, 1.0);
    return std::clamp(weighted_sum / probability_sum, 0.0, 1.0);
  }

  /**
   * @brief Parse candidate XML into a scoring tree aligned with candidate nodes.
   *
   * @param candidate Candidate plan containing XML and node bindings.
   * @param reason Output reason when parsing fails.
   * @return std::optional<XmlScoreNode> Root scoring node when parsing succeeds.
   */
  std::optional<XmlScoreNode> buildScoringTree(const CandidatePlan& candidate, std::string& reason) const
  {
    tinyxml2::XMLDocument document;
    if (document.Parse(candidate.plan.c_str()) != tinyxml2::XML_SUCCESS)
    {
      reason = "candidate XML failed to parse for graph-aware scoring";
      return std::nullopt;
    }

    const auto* plan = document.FirstChildElement("Plan");
    if (plan == nullptr)
    {
      reason = "candidate XML is missing a Plan root element";
      return std::nullopt;
    }

    std::unordered_map<std::string, std::deque<const CandidatePlanNode*>> runners_by_key;
    std::unordered_map<std::string, std::deque<const CandidatePlanNode*>> runners_by_skill;
    for (const auto& node : candidate.nodes)
    {
      runners_by_key[buildRunnerKey(node.capability_name, node.provider_name)].push_back(&node);
      runners_by_skill[node.capability_name].push_back(&node);
    }

    const auto* first_child = plan->FirstChildElement();
    if (first_child == nullptr)
    {
      reason = "candidate XML contains no executable controls or runners";
      return std::nullopt;
    }

    return buildSequenceNode(first_child, runners_by_key, runners_by_skill, reason);
  }

  /**
   * @brief Build a stable lookup key for an interface/provider pair.
   *
   * @param interface_name Runner interface name.
   * @param provider_name Runner provider name.
   * @return std::string Composite lookup key.
   */
  static std::string buildRunnerKey(const std::string& interface_name, const std::string& provider_name)
  {
    return interface_name + "\x1f" + provider_name;
  }

  /**
   * @brief Build a sequential scoring node from sibling XML elements.
   *
   * @param first_element First XML element in the sibling chain.
   * @param runners_by_key Candidate runners indexed by interface/provider.
   * @param runners_by_skill Candidate runners indexed by skill name.
   * @param reason Output reason when tree construction fails.
   * @return std::optional<XmlScoreNode> Constructed sequential node.
   */
  std::optional<XmlScoreNode> buildSequenceNode(
    const tinyxml2::XMLElement* first_element,
    std::unordered_map<std::string, std::deque<const CandidatePlanNode*>>& runners_by_key,
    std::unordered_map<std::string, std::deque<const CandidatePlanNode*>>& runners_by_skill, std::string& reason) const
  {
    std::vector<XmlScoreNode> children;
    for (const auto* element = first_element; element != nullptr; element = element->NextSiblingElement())
    {
      auto child = buildScoreNode(element, runners_by_key, runners_by_skill, reason);
      if (!child.has_value())
        return std::nullopt;
      children.push_back(std::move(child.value()));
    }

    if (children.size() == 1)
      return children.front();

    XmlScoreNode sequence;
    sequence.kind = XmlScoreNode::Kind::Sequential;
    sequence.label = "sequential";
    sequence.children = std::move(children);
    return sequence;
  }

  /**
   * @brief Convert a single XML element into a scoring node.
   *
   * @param element XML element to convert.
   * @param runners_by_key Candidate runners indexed by interface/provider.
   * @param runners_by_skill Candidate runners indexed by skill name.
   * @param reason Output reason when conversion fails.
   * @return std::optional<XmlScoreNode> Constructed score node.
   */
  std::optional<XmlScoreNode> buildScoreNode(
    const tinyxml2::XMLElement* element,
    std::unordered_map<std::string, std::deque<const CandidatePlanNode*>>& runners_by_key,
    std::unordered_map<std::string, std::deque<const CandidatePlanNode*>>& runners_by_skill, std::string& reason) const
  {
    const std::string tag = element->Name();
    if (tag == "Runner")
    {
      const char* interface_attr = element->Attribute("interface");
      const char* provider_attr = element->Attribute("provider");
      if (interface_attr == nullptr || provider_attr == nullptr)
      {
        reason = "Runner element is missing interface/provider attributes";
        return std::nullopt;
      }

      const std::string interface_name(interface_attr);
      const std::string provider_name(provider_attr);
      const std::string key = buildRunnerKey(interface_name, provider_name);

      const CandidatePlanNode* bound_runner = nullptr;
      auto key_it = runners_by_key.find(key);
      if (key_it != runners_by_key.end() && !key_it->second.empty())
      {
        bound_runner = key_it->second.front();
        key_it->second.pop_front();
      }
      else
      {
        auto skill_it = runners_by_skill.find(interface_name);
        if (skill_it != runners_by_skill.end() && !skill_it->second.empty())
        {
          bound_runner = skill_it->second.front();
          skill_it->second.pop_front();
        }
      }

      if (bound_runner == nullptr)
      {
        reason = "failed to bind XML runner to a candidate node";
        return std::nullopt;
      }

      XmlScoreNode runner;
      runner.kind = XmlScoreNode::Kind::Runner;
      runner.label = interface_name;
      runner.runner = bound_runner;
      return runner;
    }

    if (tag != "Control")
    {
      reason = "candidate XML contains an unsupported element";
      return std::nullopt;
    }

    const char* type_attr = element->Attribute("type");
    if (type_attr == nullptr)
    {
      reason = "Control element is missing a type attribute";
      return std::nullopt;
    }

    XmlScoreNode control;
    control.label = type_attr;
    const std::string control_type(type_attr);
    if (control_type == "sequential")
      control.kind = XmlScoreNode::Kind::Sequential;
    else if (control_type == "parallel_all")
      control.kind = XmlScoreNode::Kind::ParallelAll;
    else if (control_type == "parallel_any")
      control.kind = XmlScoreNode::Kind::ParallelAny;
    else if (control_type == "recovery")
      control.kind = XmlScoreNode::Kind::Recovery;
    else
    {
      reason = "candidate XML contains an unsupported control type";
      return std::nullopt;
    }

    const auto* first_child = element->FirstChildElement();
    if (first_child == nullptr)
    {
      reason = "Control element contains no executable children";
      return std::nullopt;
    }

    if (control.kind == XmlScoreNode::Kind::Recovery)
    {
      auto recovery_body = buildSequenceNode(first_child, runners_by_key, runners_by_skill, reason);
      if (!recovery_body.has_value())
        return std::nullopt;
      control.children.push_back(std::move(recovery_body.value()));
      return control;
    }

    for (const auto* child = first_child; child != nullptr; child = child->NextSiblingElement())
    {
      auto branch = buildScoreNode(child, runners_by_key, runners_by_skill, reason);
      if (!branch.has_value())
        return std::nullopt;
      control.children.push_back(std::move(branch.value()));
    }
    return control;
  }

  /**
   * @brief Dispatch scoring based on the XML score-node kind.
   *
   * @param node XML score node to evaluate.
   * @return AggregateScoreSummary Scenario summary for the node.
   */
  AggregateScoreSummary scoreXmlNode(const XmlScoreNode& node) const
  {
    switch (node.kind)
    {
      case XmlScoreNode::Kind::Runner:
        return scoreRunnerNode(node);
      case XmlScoreNode::Kind::Sequential:
        return scoreSequentialXmlNode(node);
      case XmlScoreNode::Kind::ParallelAll:
        return scoreParallelAllNode(node);
      case XmlScoreNode::Kind::ParallelAny:
        return scoreParallelAnyNode(node);
      case XmlScoreNode::Kind::Recovery:
        return scoreRecoveryBodyNode(node);
    }

    AggregateScoreSummary unsupported;
    unsupported.reason = "unknown XML score node kind";
    return unsupported;
  }

  /**
   * @brief Score a single runner node.
   *
   * @param node Runner node to evaluate.
   * @return AggregateScoreSummary Scenario summary for the runner.
   */
  AggregateScoreSummary scoreRunnerNode(const XmlScoreNode& node) const
  {
    AggregateScoreSummary summary;
    if (node.runner == nullptr)
    {
      summary.reason = "score runner node is missing a bound candidate runner";
      return summary;
    }

    summary.supported = true;
    summary.sequential_graph = true;
    summary.topology = "runner";
    summary.reason = "single runner scenario";

    const double success_probability = std::clamp(
      node.runner->feature_available[0] ? node.runner->feature_vector[0] : default_success_probability_, 0.0, 1.0);
    const double runtime_seconds = std::max(0.0, node.runner->feature_vector[1]);
    const double energy_delta_percentage = std::max(0.0, node.runner->feature_vector[2]);
    const double fault_burden = std::max(0.0, node.runner->feature_vector[3]);
    const double resource_demand = std::max(0.0, node.runner->feature_vector[4]);
    const double uncertainty = std::clamp(node.runner->feature_vector[5], 0.0, 1.0);

    if (success_probability > 0.0)
    {
      AggregateScoreSummary::ExecutionScenario success;
      success.probability = success_probability;
      success.success = true;
      success.runtime_seconds = runtime_seconds;
      success.energy_delta_percentage = energy_delta_percentage;
      success.fault_burden = fault_burden;
      success.resource_demand = resource_demand;
      success.uncertainty = uncertainty;
      success.ordered_nodes.push_back(node.runner);
      summary.scenarios.push_back(std::move(success));
    }

    if (success_probability < 1.0)
    {
      AggregateScoreSummary::ExecutionScenario failure;
      failure.probability = 1.0 - success_probability;
      failure.success = false;
      failure.runtime_seconds = runtime_seconds;
      failure.energy_delta_percentage = energy_delta_percentage;
      failure.fault_burden = fault_burden;
      failure.resource_demand = resource_demand;
      failure.uncertainty = uncertainty;
      failure.ordered_nodes.push_back(node.runner);
      summary.scenarios.push_back(std::move(failure));
    }

    return scoring::summarize_scenarios(std::move(summary));
  }

  /**
   * @brief Score a sequential control node.
   *
   * @param node Sequential node to evaluate.
   * @return AggregateScoreSummary Scenario summary for the sequence.
   */
  AggregateScoreSummary scoreSequentialXmlNode(const XmlScoreNode& node) const
  {
    AggregateScoreSummary summary;
    if (node.children.empty())
    {
      summary.reason = "sequential control contains no executable children";
      return summary;
    }

    std::vector<AggregateScoreSummary> steps;
    for (const auto& child : node.children)
    {
      if (child.kind == XmlScoreNode::Kind::Recovery)
      {
        if (steps.empty())
        {
          summary.reason = "recovery control must follow an executable step";
          return summary;
        }
        auto recovery = scoreRecoveryBodyNode(child);
        if (!recovery.supported)
          return recovery;
        steps.back() = applyRecoveryToPrimary(steps.back(), recovery);
        continue;
      }

      auto child_summary = scoreXmlNode(child);
      if (!child_summary.supported)
        return child_summary;
      steps.push_back(std::move(child_summary));
    }

    summary = steps.front();
    summary.topology = "sequential";
    summary.reason = "XML sequential scenario composition";
    for (size_t index = 1; index < steps.size(); ++index)
    {
      summary = scoring::compose_sequential_summaries(summary, steps[index], "sequential",
                                                      "XML sequential scenario composition",
                                                      summary.sequential_graph && steps[index].sequential_graph);
    }
    summary.topology = "sequential";
    summary.reason = "XML sequential scenario composition";
    summary.sequential_graph = true;
    for (const auto& step : steps)
      summary.sequential_graph = summary.sequential_graph && step.sequential_graph;
    return scoring::summarize_scenarios(std::move(summary));
  }

  /**
   * @brief Score a parallel-all control node.
   *
   * @param node Parallel-all node to evaluate.
   * @return AggregateScoreSummary Scenario summary for the parallel-all branch set.
   */
  AggregateScoreSummary scoreParallelAllNode(const XmlScoreNode& node) const
  {
    AggregateScoreSummary summary;
    if (node.children.empty())
    {
      summary.reason = "parallel_all control contains no branches";
      return summary;
    }

    std::vector<AggregateScoreSummary> branches;
    branches.reserve(node.children.size());
    for (const auto& child : node.children)
    {
      auto branch = scoreXmlNode(child);
      if (!branch.supported)
        return branch;
      branches.push_back(std::move(branch));
    }

    summary.supported = true;
    summary.sequential_graph = false;
    summary.topology = "parallel_all";
    summary.reason = "parallel_all scenario composition";
    for (const auto& combination : scoring::enumerate_scenario_combinations(branches))
    {
      AggregateScoreSummary::ExecutionScenario scenario;
      scenario.probability = 1.0;
      scenario.success = true;
      scenario.uncertainty = 0.0;
      for (const auto& branch : combination)
      {
        scenario.probability *= branch.probability;
        scenario.success = scenario.success && branch.success;
        scenario.runtime_seconds = std::max(scenario.runtime_seconds, branch.runtime_seconds);
        scenario.energy_delta_percentage += branch.energy_delta_percentage;
        scenario.fault_burden += branch.fault_burden;
        scenario.resource_demand += branch.resource_demand;
        scenario.uncertainty = std::max(scenario.uncertainty, branch.uncertainty);
        scoring::append_ordered_nodes(scenario.ordered_nodes, branch.ordered_nodes);
      }
      summary.scenarios.push_back(std::move(scenario));
    }
    return scoring::summarize_scenarios(std::move(summary));
  }

  /**
   * @brief Score a parallel-any control node.
   *
   * @param node Parallel-any node to evaluate.
   * @return AggregateScoreSummary Scenario summary for the parallel-any branch set.
   */
  AggregateScoreSummary scoreParallelAnyNode(const XmlScoreNode& node) const
  {
    AggregateScoreSummary summary;
    if (node.children.empty())
    {
      summary.reason = "parallel_any control contains no branches";
      return summary;
    }

    std::vector<AggregateScoreSummary> branches;
    branches.reserve(node.children.size());
    for (const auto& child : node.children)
    {
      auto branch = scoreXmlNode(child);
      if (!branch.supported)
        return branch;
      branches.push_back(std::move(branch));
    }

    summary.supported = true;
    summary.sequential_graph = false;
    summary.topology = "parallel_any";
    summary.reason = "parallel_any scenario composition";
    for (const auto& combination : scoring::enumerate_scenario_combinations(branches))
    {
      AggregateScoreSummary::ExecutionScenario scenario;
      scenario.probability = 1.0;
      scenario.success = false;
      scenario.uncertainty = 0.0;

      double stop_time = 0.0;
      bool stop_time_set = false;
      for (const auto& branch : combination)
      {
        scenario.probability *= branch.probability;
        scenario.resource_demand += branch.resource_demand;
        scenario.uncertainty = std::max(scenario.uncertainty, branch.uncertainty);
        scoring::append_ordered_nodes(scenario.ordered_nodes, branch.ordered_nodes);
        if (branch.success && (!stop_time_set || branch.runtime_seconds < stop_time))
        {
          stop_time = branch.runtime_seconds;
          stop_time_set = true;
        }
      }

      if (stop_time_set)
      {
        scenario.success = true;
        scenario.runtime_seconds = stop_time;
      }
      else
      {
        for (const auto& branch : combination)
          stop_time = std::max(stop_time, branch.runtime_seconds);
        scenario.runtime_seconds = stop_time;
      }

      for (const auto& branch : combination)
      {
        const double branch_runtime = std::max(branch.runtime_seconds, 1e-6);
        const double completion_fraction =
          branch.runtime_seconds <= stop_time ? 1.0 : std::min(1.0, stop_time / branch_runtime);
        scenario.energy_delta_percentage += branch.energy_delta_percentage * completion_fraction;
        scenario.fault_burden += branch.fault_burden * completion_fraction;
      }

      summary.scenarios.push_back(std::move(scenario));
    }
    return scoring::summarize_scenarios(std::move(summary));
  }

  /**
   * @brief Score a recovery-body node.
   *
   * @param node Recovery node to evaluate.
   * @return AggregateScoreSummary Scenario summary for the recovery body.
   */
  AggregateScoreSummary scoreRecoveryBodyNode(const XmlScoreNode& node) const
  {
    AggregateScoreSummary summary;
    if (node.children.empty())
    {
      summary.reason = "recovery control contains no executable body";
      return summary;
    }

    auto recovery_body = scoreXmlNode(node.children.front());
    if (!recovery_body.supported)
      return recovery_body;
    recovery_body.sequential_graph = false;
    recovery_body.topology = "recovery";
    recovery_body.reason = "recovery scenario composition";
    return scoring::summarize_scenarios(std::move(recovery_body));
  }

  /**
   * @brief Apply recovery scenarios to failed primary scenarios.
   *
   * @param primary Primary execution summary.
   * @param recovery Recovery execution summary.
   * @return AggregateScoreSummary Combined recovery-aware summary.
   */
  AggregateScoreSummary applyRecoveryToPrimary(const AggregateScoreSummary& primary,
                                               const AggregateScoreSummary& recovery) const
  {
    AggregateScoreSummary combined;
    combined.supported = primary.supported && recovery.supported;
    combined.sequential_graph = false;
    combined.topology = "recovery";
    combined.reason = "recovery scenario composition";
    if (!combined.supported)
      return combined;

    for (const auto& primary_scenario : primary.scenarios)
    {
      if (primary_scenario.success)
      {
        combined.scenarios.push_back(primary_scenario);
        continue;
      }

      for (const auto& recovery_scenario : recovery.scenarios)
      {
        AggregateScoreSummary::ExecutionScenario scenario;
        scenario.probability = primary_scenario.probability * recovery_scenario.probability;
        scenario.success = recovery_scenario.success;
        scenario.runtime_seconds = primary_scenario.runtime_seconds + recovery_scenario.runtime_seconds;
        scenario.energy_delta_percentage =
          primary_scenario.energy_delta_percentage + recovery_scenario.energy_delta_percentage;
        scenario.fault_burden = primary_scenario.fault_burden + recovery_scenario.fault_burden;
        scenario.resource_demand = std::max(primary_scenario.resource_demand, recovery_scenario.resource_demand);
        scenario.uncertainty = std::max(primary_scenario.uncertainty, recovery_scenario.uncertainty);
        scenario.ordered_nodes = primary_scenario.ordered_nodes;
        scoring::append_ordered_nodes(scenario.ordered_nodes, recovery_scenario.ordered_nodes);
        combined.scenarios.push_back(std::move(scenario));
      }
    }
    return scoring::summarize_scenarios(std::move(combined));
  }

  /**
   * @brief Serialize candidate score components for persistence and debugging.
   *
   * @param candidate Candidate plan with finalized score fields.
   * @param summary Aggregate summary used to produce the score.
   * @return std::string Serialized score-components document.
   */
  std::string serializeScoreComponents(const CandidatePlan& candidate, const AggregateScoreSummary& summary) const
  {
    const double normalized_runtime = runtime_transform_.apply(std::max(0.0, candidate.score.expected_runtime_seconds));
    const double normalized_energy = energy_transform_.apply(std::max(0.0, candidate.score.expected_energy_delta_percentage));
    const double normalized_fault = fault_transform_.apply(std::max(0.0, candidate.score.expected_fault_burden));
    const double normalized_resource = resource_transform_.apply(std::max(0.0, candidate.score.expected_resource_demand));
    const double normalized_uncertainty = std::clamp(candidate.score.expected_uncertainty, 0.0, 1.0);
    const double success_reward = weight_success_ * candidate.score.terminal_success_estimate;
    const double time_penalty = weight_time_ * normalized_runtime;
    const double energy_penalty = weight_energy_ * normalized_energy;
    const double fault_penalty = weight_fault_ * normalized_fault;
    const double resource_penalty = weight_resource_ * normalized_resource;
    const double uncertainty_penalty = weight_uncertainty_ * normalized_uncertainty;
    const double raw_formula_score = success_reward - time_penalty - energy_penalty - fault_penalty -
                                     resource_penalty - uncertainty_penalty;

    std::ostringstream stream;
    stream << '{';
    stream << "\"supported\":" << (candidate.score.supported ? "true" : "false");
    stream << ",\"feasible\":" << (candidate.score.feasible ? "true" : "false");
    stream << ",\"sequential_graph\":" << (candidate.score.sequential_graph ? "true" : "false");
    stream << ",\"topology\":\"" << escapeJsonString(summary.topology) << "\"";
    stream << ",\"reason\":\"" << escapeJsonString(candidate.score.reason) << "\"";
    stream << ",\"terminal_success_estimate\":" << candidate.score.terminal_success_estimate;
    stream << ",\"expected_runtime_seconds\":" << candidate.score.expected_runtime_seconds;
    stream << ",\"expected_energy_delta_percentage\":" << candidate.score.expected_energy_delta_percentage;
    stream << ",\"expected_fault_burden\":" << candidate.score.expected_fault_burden;
    stream << ",\"expected_resource_demand\":" << candidate.score.expected_resource_demand;
    stream << ",\"expected_uncertainty\":" << candidate.score.expected_uncertainty;
    stream << ",\"plan_uncertainty_aggregation\":\"" << escapeJsonString(summary.uncertainty_aggregation_mode) << "\"";
    stream << ",\"uncertainty_weight_locked\":" << (summary.uncertainty_weight_locked ? "true" : "false");
    stream << ",\"uncertainty_weight_target\":\"" << escapeJsonString(summary.uncertainty_weight_target) << "\"";
    stream << ",\"non_success_terminal_policy\":\"" << escapeJsonString(nonSuccessTerminalPolicyLabel()) << "\"";
    stream << ",\"validation_rejected\":" << (candidate.validation.rejected ? "true" : "false");
    stream << ",\"validation_penalty\":" << candidate.validation.total_penalty;
    stream << ",\"validation_findings\":[";
    for (size_t index = 0; index < candidate.validation.findings.size(); ++index)
    {
      if (index > 0)
        stream << ',';
      const auto& finding = candidate.validation.findings[index];
      stream << "{\"plugin\":\"" << escapeJsonString(finding.plugin_name) << "\",\"level\":\""
             << validationFindingLevelLabel(finding.level) << "\",\"penalty\":" << finding.penalty
             << ",\"message\":\"" << escapeJsonString(finding.message) << "\"}";
    }
    stream << ']';
    stream << ",\"final_rapfs\":";
    appendJsonNumberOrNull(stream, candidate.score.final_rapfs);
    stream << ",\"rapfs\":{";
    stream << "\"weights\":{";
    stream << "\"success\":" << weight_success_;
    stream << ",\"time\":" << weight_time_;
    stream << ",\"energy\":" << weight_energy_;
    stream << ",\"fault\":" << weight_fault_;
    stream << ",\"resource\":" << weight_resource_;
    stream << ",\"uncertainty\":" << weight_uncertainty_;
    stream << "}";
    stream << ",\"raw_terms\":{";
    stream << "\"success\":" << candidate.score.terminal_success_estimate;
    stream << ",\"time\":" << candidate.score.expected_runtime_seconds;
    stream << ",\"energy\":" << candidate.score.expected_energy_delta_percentage;
    stream << ",\"fault\":" << candidate.score.expected_fault_burden;
    stream << ",\"resource\":" << candidate.score.expected_resource_demand;
    stream << ",\"uncertainty\":" << candidate.score.expected_uncertainty;
    stream << "}";
    stream << ",\"normalized_terms\":{";
    stream << "\"success\":" << candidate.score.terminal_success_estimate;
    stream << ",\"time\":" << normalized_runtime;
    stream << ",\"energy\":" << normalized_energy;
    stream << ",\"fault\":" << normalized_fault;
    stream << ",\"resource\":" << normalized_resource;
    stream << ",\"uncertainty\":" << normalized_uncertainty;
    stream << "}";
    stream << ",\"weighted_terms\":{";
    stream << "\"success_reward\":" << success_reward;
    stream << ",\"time_penalty\":" << time_penalty;
    stream << ",\"energy_penalty\":" << energy_penalty;
    stream << ",\"fault_penalty\":" << fault_penalty;
    stream << ",\"resource_penalty\":" << resource_penalty;
    stream << ",\"uncertainty_penalty\":" << uncertainty_penalty;
    stream << "}";
    stream << ",\"feasibility\":{";
    stream << "\"passed\":" << (candidate.score.feasible ? "true" : "false");
    stream << ",\"reason\":\"" << escapeJsonString(candidate.score.feasible ? std::string() : candidate.score.reason) << "\"";
    stream << ",\"thresholds\":{";
    stream << "\"min_success_probability\":" << feasibility_config_.min_success_probability;
    stream << ",\"max_runtime_seconds\":" << feasibility_config_.max_runtime_seconds;
    stream << ",\"max_energy_delta_percentage\":" << feasibility_config_.max_energy_delta_percentage;
    stream << ",\"max_fault_burden\":" << feasibility_config_.max_fault_burden;
    stream << ",\"max_resource_demand\":" << feasibility_config_.max_resource_demand;
    stream << ",\"max_uncertainty\":" << feasibility_config_.max_uncertainty;
    stream << "}}";
    stream << ",\"formula\":{";
    stream << "\"raw_score\":";
    appendJsonNumberOrNull(stream, raw_formula_score);
    stream << ",\"validation_penalty\":" << candidate.validation.total_penalty;
    stream << ",\"final_score\":";
    appendJsonNumberOrNull(stream, candidate.score.final_rapfs);
    stream << "}";
    stream << "}";
    stream << ",\"ordered_nodes\":[";
    for (size_t index = 0; index < summary.ordered_nodes.size(); ++index)
    {
      if (index > 0)
        stream << ',';
      stream << '\"' << escapeJsonString(summary.ordered_nodes[index]->candidate_node_id) << '\"';
    }
    stream << "]}";
    return stream.str();
  }

  std::string serializeRejectedScoreComponents(const CandidatePlan& candidate) const
  {
    std::ostringstream stream;
    stream << '{';
    stream << "\"supported\":true";
    stream << ",\"feasible\":false";
    stream << ",\"validation_rejected\":true";
    stream << ",\"validation_penalty\":" << candidate.validation.total_penalty;
    stream << ",\"reason\":\"" << escapeJsonString(candidate.score.reason) << "\"";
    stream << ",\"validation_findings\":[";
    for (size_t index = 0; index < candidate.validation.findings.size(); ++index)
    {
      if (index > 0)
        stream << ',';
      const auto& finding = candidate.validation.findings[index];
      stream << "{\"plugin\":\"" << escapeJsonString(finding.plugin_name) << "\",\"level\":\""
             << validationFindingLevelLabel(finding.level) << "\",\"penalty\":" << finding.penalty
             << ",\"message\":\"" << escapeJsonString(finding.message) << "\"}";
    }
    stream << ']';
    stream << ",\"rapfs\":{";
    stream << "\"weights\":{";
    stream << "\"success\":" << weight_success_;
    stream << ",\"time\":" << weight_time_;
    stream << ",\"energy\":" << weight_energy_;
    stream << ",\"fault\":" << weight_fault_;
    stream << ",\"resource\":" << weight_resource_;
    stream << ",\"uncertainty\":" << weight_uncertainty_;
    stream << "}";
    stream << ",\"feasibility\":{";
    stream << "\"passed\":false";
    stream << ",\"reason\":\"validation rejected before RAPFS aggregation\"";
    stream << "}";
    stream << ",\"formula\":{";
    stream << "\"raw_score\":null";
    stream << ",\"validation_penalty\":" << candidate.validation.total_penalty;
    stream << ",\"final_score\":null";
    stream << "}";
    stream << "}";
    stream << "}";
    return stream.str();
  }

  FeasibilityConfig feasibility_config_;
  PiecewiseLinearTransform runtime_transform_;
  PiecewiseLinearTransform energy_transform_;
  PiecewiseLinearTransform fault_transform_;
  PiecewiseLinearTransform resource_transform_;
  PiecewiseLinearTransform familiarity_transform_;
  std::vector<std::string> estimator_names_;
  std::string calibration_version_ = "baseline_v1";
  std::string normalizer_version_ = "calibrated_v1";
  std::string configured_estimator_version_ = "baseline_v1";
  std::string estimator_version_ = "baseline_v1";
  std::string weight_version_ = "baseline_v2";
  NonSuccessTerminalPolicy non_success_terminal_policy_ = NonSuccessTerminalPolicy::CountAsFailure;
  std::string uncertainty_aggregation_mode_ = "scenario_weighted_mean";
  std::string uncertainty_weight_target_;
  double default_success_probability_ = 0.5;
  double default_uncertainty_ = 1.0;
  double uncertainty_statistical_weight_ = 0.40;
  double uncertainty_task_ambiguity_weight_ = 0.15;
  double uncertainty_unfamiliarity_weight_ = 0.25;
  double uncertainty_missing_evidence_weight_ = 0.20;
  double unknown_unfamiliarity_prior_ = 0.75;
  double weight_success_ = 0.40;
  double weight_time_ = 0.15;
  double weight_energy_ = 0.10;
  double weight_fault_ = 0.10;
  double weight_resource_ = 0.05;
  double weight_uncertainty_ = 0.20;
  bool uncertainty_weight_locked_ = true;
};

}  // namespace fabric