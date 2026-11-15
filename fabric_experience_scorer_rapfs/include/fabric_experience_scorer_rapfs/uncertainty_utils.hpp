#pragma once

#include <algorithm>
#include <sstream>
#include <string>
#include <unordered_set>

#include <fabric_base/estimate_base.hpp>
#include <fabric_base/utils/planning_structs.hpp>

namespace fabric::scoring
{

struct UncertaintyWeights
{
  double statistical = 0.40;
  double task_ambiguity = 0.15;
  double unfamiliarity = 0.25;
  double missing_evidence = 0.20;
  double default_uncertainty = 1.0;
};

/**
 * @brief Estimate task ambiguity from the wording of a task description.
 *
 * @param task_description Natural-language task description to analyze.
 * @return double Normalized ambiguity score in the range [0, 1].
 */
inline double compute_task_ambiguity(const std::string& task_description)
{
  const auto tokens = planning::tokenize(task_description);
  if (tokens.empty())
    return 1.0;

  static const std::unordered_set<std::string> ambiguous_tokens = {
    "it", "that", "this", "there", "somewhere", "something", "someone", "them", "thing", "stuff", "place", "area", "location"
  };

  size_t ambiguous_count = 0;
  for (const auto& token : tokens)
  {
    if (ambiguous_tokens.count(token) > 0)
      ++ambiguous_count;
  }

  const double referential_ambiguity = static_cast<double>(ambiguous_count) / static_cast<double>(tokens.size());
  const double brevity_penalty = tokens.size() <= 2 ? 1.0 : (tokens.size() <= 4 ? 0.5 : 0.0);
  const double lexical_specificity =
    std::min(1.0, static_cast<double>(std::unordered_set<std::string>(tokens.begin(), tokens.end()).size()) / 6.0);
  return std::clamp(0.5 * referential_ambiguity + 0.3 * brevity_penalty + 0.2 * (1.0 - lexical_specificity), 0.0,
                    1.0);
}

/**
 * @brief Estimate how much evidence required by the scorer is missing.
 *
 * @param evidence Aggregated capability evidence.
 * @return double Fraction of missing evidence components in the range [0, 1].
 */
inline double compute_missing_evidence(const fabric::experience::EstimatorEvidence& evidence)
{
  int missing_components = 0;
  missing_components += evidence.runtime_sample_count <= 0 ? 1 : 0;
  missing_components += evidence.energy_sample_count <= 0 ? 1 : 0;
  missing_components += evidence.fault_exposure_count <= 0 && evidence.runs_completed <= 0 ? 1 : 0;
  missing_components += !evidence.resource_missing_dimensions.empty() ? 1 : 0;
  return static_cast<double>(missing_components) / 4.0;
}

/**
 * @brief Combine uncertainty components into a single normalized score.
 *
 * @param weights Component weights and default fallback.
 * @param statistical_uncertainty Uncertainty derived from statistics.
 * @param task_ambiguity Uncertainty derived from task wording.
 * @param task_unfamiliarity Uncertainty derived from semantic unfamiliarity.
 * @param missing_evidence Uncertainty contribution from missing telemetry.
 * @return double Combined uncertainty score in the range [0, 1].
 */
inline double compose_uncertainty(const UncertaintyWeights& weights, double statistical_uncertainty,
                                  double task_ambiguity, double task_unfamiliarity, double missing_evidence)
{
  const double total_weight = weights.statistical + weights.task_ambiguity + weights.unfamiliarity +
                              weights.missing_evidence;
  if (total_weight <= 0.0)
    return std::clamp(weights.default_uncertainty, 0.0, 1.0);

  const double combined = weights.statistical * statistical_uncertainty +
                          weights.task_ambiguity * task_ambiguity +
                          weights.unfamiliarity * task_unfamiliarity +
                          weights.missing_evidence * missing_evidence;
  return std::clamp(combined / total_weight, 0.0, 1.0);
}

/**
 * @brief Build a textual explanation for a composed uncertainty value.
 *
 * @param statistical_uncertainty Statistical uncertainty component.
 * @param task_ambiguity Task-ambiguity component.
 * @param task_unfamiliarity Task-unfamiliarity component.
 * @param missing_evidence Missing-evidence component.
 * @param statistical_reason Explanation emitted by the statistical estimator.
 * @return std::string Human-readable explanation string.
 */
inline std::string build_uncertainty_reason(double statistical_uncertainty, double task_ambiguity,
                                            double task_unfamiliarity, double missing_evidence,
                                            const std::string& statistical_reason)
{
  std::ostringstream stream;
  stream << "components(stat=" << statistical_uncertainty << ", amb=" << task_ambiguity << ", unfam="
         << task_unfamiliarity << ", miss=" << missing_evidence << ")";
  if (!statistical_reason.empty())
    stream << "; " << statistical_reason;
  return stream.str();
}

}  // namespace fabric::scoring