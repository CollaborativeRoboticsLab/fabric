#pragma once

#include <algorithm>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

#include <fabric_base/behaviour_interface_base.hpp>
#include <fabric_base/core_base.hpp>
#include <fabric_base/utils/planning_structs.hpp>

namespace fabric::server::planning
{
using PlanningRequest = ::fabric::planning::Request;
using PlanningResult = ::fabric::planning::Result;
using CandidatePlan = ::fabric::planning::CandidatePlan;
using CandidatePlanNode = ::fabric::planning::CandidatePlanNode;
using CandidatePlanEdge = ::fabric::planning::CandidatePlanEdge;
using PendingGeneratedPlan = ::fabric::planning::PendingGeneratedPlan;
using ValidationFindingLevel = ::fabric::planning::ValidationFindingLevel;
using SkillInfo = ::fabric::plan::node;
using CandidateSkillRequest = ::fabric::experience::CandidateSkillRequest;

inline bool isInternalSkill(const std::string & capability_name)
{
  return capability_name == "fabric_capabilities/FabricCompletionRunner";
}

inline std::string summarizeSelectionFailure(const PlanningResult & planning_result)
{
  for (const auto & candidate : planning_result.candidates) {
    if (candidate.validation.rejected && !candidate.validation.summary.empty()) {
      return candidate.validation.summary;
    }
    if (!candidate.score.reason.empty()) {
      return candidate.score.reason;
    }
  }

  return "no feasible candidate plan was produced";
}

inline bool hasValidationSelectableCandidate(const PlanningResult & planning_result)
{
  return std::any_of(planning_result.candidates.begin(), planning_result.candidates.end(),
           [](const CandidatePlan & candidate) {return !candidate.validation.rejected;});
}

inline bool hasValidationFinding(
  const PlanningResult & planning_result,
  const ValidationFindingLevel level)
{
  return std::any_of(planning_result.candidates.begin(), planning_result.candidates.end(),
           [level](const CandidatePlan & candidate) {
             return std::any_of(candidate.validation.findings.begin(),
        candidate.validation.findings.end(),
                    [level](const auto & finding) {
                      return finding.level == level;
                                          });
                     });
}

inline bool shouldRefinePlanningResult(
  const PlanningResult & planning_result,
  const bool retry_on_penalty_findings,
  const bool retry_on_warning_findings)
{
  if (!hasValidationSelectableCandidate(planning_result)) {
    return true;
  }

  if (retry_on_penalty_findings && hasValidationFinding(planning_result,
      ValidationFindingLevel::Penalty))
  {
    return true;
  }

  if (retry_on_warning_findings && hasValidationFinding(planning_result,
      ValidationFindingLevel::Warning))
  {
    return true;
  }

  return false;
}

inline std::string selectedOrFirstPlan(const PlanningResult & planning_result)
{
  if (!planning_result.selected_plan.empty()) {
    return planning_result.selected_plan;
  }
  return planning_result.candidates.empty() ? std::string() :
         planning_result.candidates.front().plan;
}

inline bool selectedPlanCanDispatch(const PlanningResult & planning_result)
{
  for (const auto & candidate : planning_result.candidates) {
    if (candidate.candidate_plan_id == planning_result.selected_candidate_plan_id) {
      return candidate.score.feasible;
    }
  }
  return false;
}

inline std::string buildValidationCritique(const PlanningResult & planning_result)
{
  std::ostringstream critique;
  critique << "Validation rejected the current candidate plan. Fix the following issues: ";

  bool appended = false;
  for (const auto & candidate : planning_result.candidates) {
    if (!candidate.validation.summary.empty()) {
      critique << candidate.validation.summary << ' ';
      appended = true;
    }

    const size_t limit = std::min<size_t>(4, candidate.validation.findings.size());
    for (size_t index = 0; index < limit; ++index) {
      const auto & finding = candidate.validation.findings[index];
      critique << validationFindingLevelLabel(finding.level) << ": " << finding.message << ' ';
      appended = true;
    }
  }

  if (!appended) {
    critique << "No valid candidate plan was produced by Fabric validation.";
  }

  return critique.str();
}

inline void appendParsedCandidateEdge(
  CandidatePlan & candidate,
  const std::unordered_map<int, std::string> & node_id_by_instance,
  const SkillInfo & source,
  const SkillInfo & target,
  const std::string & label)
{
  if (!target.exists()) {
    return;
  }

  auto source_it = node_id_by_instance.find(source.instance_id);
  auto target_it = node_id_by_instance.find(target.instance_id);
  if (source_it == node_id_by_instance.end() || target_it == node_id_by_instance.end()) {
    return;
  }

  if (isInternalSkill(source.interface) || isInternalSkill(target.interface)) {
    return;
  }

  CandidatePlanEdge edge;
  edge.source_node_id = source_it->second;
  edge.target_node_id = target_it->second;
  edge.source_capability_id = ::fabric::planning::normalize_identifier(source.interface);
  edge.target_capability_id = ::fabric::planning::normalize_identifier(target.interface);
  edge.edge_label = label;
  candidate.edges.push_back(edge);
}

inline CandidatePlan parseCandidatePlanForScoring(
  ::fabric::CoreBase & core_plugin,
  ::fabric::BehaviourInterfaceBase & behaviour_plugin,
  const PlanningRequest & request,
  const std::string & candidate_plan_id,
  const std::string & plan_xml)
{
  const auto parsed_plan = behaviour_plugin.parseBehaviourPlan(plan_xml);
  if (!parsed_plan.success) {
    throw std::runtime_error(parsed_plan.error.empty() ? "plan parsing failed" : parsed_plan.error);
  }

  CandidatePlan candidate;
  candidate.candidate_plan_id = candidate_plan_id;
  candidate.plan = plan_xml;
  candidate.reasoning = "score-only evaluation of supplied plan XML";

  std::unordered_map<int, std::string> node_id_by_instance;
  const std::string context_key = ::fabric::planning::build_context_key(request.task_description);
  const std::string context_class =
    ::fabric::planning::build_context_class(request.task_description);

  for (const auto & connection : parsed_plan.connections) {
    if (isInternalSkill(connection.source.interface)) {
      continue;
    }

    const int instance_id = connection.source.instance_id;
    if (node_id_by_instance.count(instance_id) > 0) {
      continue;
    }

    CandidatePlanNode node;
    node.candidate_node_id = "runner_" + std::to_string(instance_id);
    node.runtime_instance_id = instance_id;
    node.capability_name = connection.source.interface;
    node.provider_name = connection.source.provider;
    node.capability_id = ::fabric::planning::normalize_identifier(connection.source.interface);
    node.context_key = context_key;
    node.context_class = context_class;
    node.resolution = core_plugin.resolveCandidateSkill(CandidateSkillRequest{
        request.planning_request_id,
        candidate.candidate_plan_id,
        "",
        node.candidate_node_id,
        node.capability_name,
        node.capability_id,
        node.context_key,
        node.context_class,
    });
    candidate.nodes.push_back(node);
    node_id_by_instance.emplace(instance_id, node.candidate_node_id);
  }

  for (const auto & connection : parsed_plan.connections) {
    appendParsedCandidateEdge(
      candidate, node_id_by_instance, connection.source, connection.on_success, "on_success");
    appendParsedCandidateEdge(
      candidate, node_id_by_instance, connection.source, connection.on_failure, "on_failure");
    appendParsedCandidateEdge(
      candidate, node_id_by_instance, connection.source, connection.on_start, "on_start");
    appendParsedCandidateEdge(
      candidate, node_id_by_instance, connection.source, connection.on_stop, "on_stop");
  }

  candidate.graph_hash = ::fabric::planning::hash_candidate_plan(candidate);
  return candidate;
}

inline void queuePendingSelectedPlan(
  std::mutex & planning_state_mutex,
  std::vector<PendingGeneratedPlan> & pending_generated_plans,
  const PlanningRequest & request,
  const PlanningResult & planning_result)
{
  for (const auto & candidate : planning_result.candidates) {
    if (candidate.candidate_plan_id != planning_result.selected_candidate_plan_id) {
      continue;
    }

    std::lock_guard<std::mutex> lock(planning_state_mutex);
    pending_generated_plans.push_back(PendingGeneratedPlan{
        request.planning_request_id,
        candidate.candidate_plan_id,
        candidate.graph_hash,
        candidate,
        request.recorded_at_sec,
        request.recorded_at_nanosec,
    });
    break;
  }
}
}  // namespace fabric::server::planning
