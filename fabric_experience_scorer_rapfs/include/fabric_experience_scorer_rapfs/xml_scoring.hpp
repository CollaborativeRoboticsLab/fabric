#pragma once

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fabric_base/utils/planning_structs.hpp>

namespace fabric::scoring
{

struct XmlScoreNode
{
  enum class Kind
  {
    Runner,
    Sequential,
    ParallelAll,
    ParallelAny,
    Recovery
  };

  Kind kind = Kind::Runner;
  std::string label;
  const planning::CandidatePlanNode* runner = nullptr;
  std::vector<XmlScoreNode> children;
};

struct AggregateScoreSummary
{
  struct ExecutionScenario
  {
    double probability = 0.0;
    bool success = false;
    double runtime_seconds = 0.0;
    double energy_delta_percentage = 0.0;
    double fault_burden = 0.0;
    double resource_demand = 0.0;
    double uncertainty = 1.0;
    std::vector<const planning::CandidatePlanNode*> ordered_nodes;
  };

  bool supported = false;
  bool sequential_graph = true;
  std::string topology = "unsupported";
  std::string reason;
  double terminal_success_estimate = 0.0;
  double expected_runtime_seconds = 0.0;
  double expected_energy_delta_percentage = 0.0;
  double expected_fault_burden = 0.0;
  double expected_resource_demand = 0.0;
  double expected_uncertainty = 1.0;
  std::string uncertainty_aggregation_mode = "reachable_node_max";
  bool uncertainty_weight_locked = true;
  std::string uncertainty_weight_target;
  std::vector<const planning::CandidatePlanNode*> ordered_nodes;
  std::vector<ExecutionScenario> scenarios;
};

/**
 * @brief Append ordered nodes from one sequence to another.
 *
 * @param destination Destination sequence to extend.
 * @param source Source sequence to append.
 */
inline void append_ordered_nodes(std::vector<const planning::CandidatePlanNode*>& destination,
                                 const std::vector<const planning::CandidatePlanNode*>& source)
{
  destination.insert(destination.end(), source.begin(), source.end());
}

/**
 * @brief Append ordered nodes while suppressing duplicate candidate-node IDs.
 *
 * @param destination Destination sequence to extend.
 * @param source Source sequence to append.
 */
inline void append_ordered_nodes_unique(std::vector<const planning::CandidatePlanNode*>& destination,
                                        const std::vector<const planning::CandidatePlanNode*>& source)
{
  std::unordered_set<std::string> seen;
  for (const auto* node : destination)
  {
    if (node != nullptr)
      seen.insert(node->candidate_node_id);
  }

  for (const auto* node : source)
  {
    if (node == nullptr || !seen.insert(node->candidate_node_id).second)
      continue;
    destination.push_back(node);
  }
}

/**
 * @brief Reduce execution scenarios into aggregate score statistics.
 *
 * @param summary Scenario summary to aggregate.
 * @return AggregateScoreSummary Summary with aggregate expectation fields populated.
 */
inline AggregateScoreSummary summarize_scenarios(AggregateScoreSummary summary)
{
  if (summary.scenarios.empty())
  {
    summary.supported = false;
    if (summary.reason.empty())
      summary.reason = "no execution scenarios were produced";
    return summary;
  }

  summary.supported = true;
  summary.terminal_success_estimate = 0.0;
  summary.expected_runtime_seconds = 0.0;
  summary.expected_energy_delta_percentage = 0.0;
  summary.expected_fault_burden = 0.0;
  summary.expected_resource_demand = 0.0;
  summary.expected_uncertainty = 0.0;
  summary.ordered_nodes.clear();

  for (const auto& scenario : summary.scenarios)
  {
    const double probability = std::clamp(scenario.probability, 0.0, 1.0);
    if (scenario.success)
      summary.terminal_success_estimate += probability;
    summary.expected_runtime_seconds += probability * std::max(0.0, scenario.runtime_seconds);
    summary.expected_energy_delta_percentage += probability * std::max(0.0, scenario.energy_delta_percentage);
    summary.expected_fault_burden += probability * std::max(0.0, scenario.fault_burden);
    summary.expected_resource_demand = std::max(summary.expected_resource_demand, std::max(0.0, scenario.resource_demand));
    summary.expected_uncertainty = std::max(summary.expected_uncertainty, std::clamp(scenario.uncertainty, 0.0, 1.0));
    append_ordered_nodes_unique(summary.ordered_nodes, scenario.ordered_nodes);
  }
  return summary;
}

/**
 * @brief Enumerate the Cartesian product of branch execution scenarios.
 *
 * @param branches Branch summaries whose scenarios should be combined.
 * @return std::vector<std::vector<AggregateScoreSummary::ExecutionScenario>> Scenario combinations across branches.
 */
inline std::vector<std::vector<AggregateScoreSummary::ExecutionScenario>> enumerate_scenario_combinations(
  const std::vector<AggregateScoreSummary>& branches)
{
  std::vector<std::vector<AggregateScoreSummary::ExecutionScenario>> combinations(1);
  for (const auto& branch : branches)
  {
    std::vector<std::vector<AggregateScoreSummary::ExecutionScenario>> next;
    for (const auto& combination : combinations)
    {
      for (const auto& scenario : branch.scenarios)
      {
        auto expanded = combination;
        expanded.push_back(scenario);
        next.push_back(std::move(expanded));
      }
    }
    combinations = std::move(next);
  }
  return combinations;
}

/**
 * @brief Compose two sequential branch summaries into one combined summary.
 *
 * @param left Summary for the left-hand execution segment.
 * @param right Summary for the right-hand execution segment.
 * @param topology Topology label for the combined summary.
 * @param reason Explanation string for the combined summary.
 * @param sequential_graph Whether the combined graph remains sequential.
 * @return AggregateScoreSummary Combined sequential summary.
 */
inline AggregateScoreSummary compose_sequential_summaries(const AggregateScoreSummary& left,
                                                          const AggregateScoreSummary& right,
                                                          const std::string& topology,
                                                          const std::string& reason,
                                                          bool sequential_graph)
{
  AggregateScoreSummary combined;
  combined.supported = left.supported && right.supported;
  combined.sequential_graph = sequential_graph;
  combined.topology = topology;
  combined.reason = reason;
  if (!combined.supported)
    return combined;

  for (const auto& left_scenario : left.scenarios)
  {
    if (!left_scenario.success)
    {
      combined.scenarios.push_back(left_scenario);
      continue;
    }

    for (const auto& right_scenario : right.scenarios)
    {
      AggregateScoreSummary::ExecutionScenario scenario;
      scenario.probability = left_scenario.probability * right_scenario.probability;
      scenario.success = right_scenario.success;
      scenario.runtime_seconds = left_scenario.runtime_seconds + right_scenario.runtime_seconds;
      scenario.energy_delta_percentage = left_scenario.energy_delta_percentage + right_scenario.energy_delta_percentage;
      scenario.fault_burden = left_scenario.fault_burden + right_scenario.fault_burden;
      scenario.resource_demand = std::max(left_scenario.resource_demand, right_scenario.resource_demand);
      scenario.uncertainty = std::max(left_scenario.uncertainty, right_scenario.uncertainty);
      scenario.ordered_nodes = left_scenario.ordered_nodes;
      append_ordered_nodes(scenario.ordered_nodes, right_scenario.ordered_nodes);
      combined.scenarios.push_back(std::move(scenario));
    }
  }
  return summarize_scenarios(std::move(combined));
}

}  // namespace fabric::scoring