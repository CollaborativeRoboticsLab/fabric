#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fabric_base/validation_base.hpp>

namespace fabric
{

using ValidationBase = ::fabric::ValidationBase;
using PlanningRequest = ::fabric::planning::Request;
using BehaviourInfo = ::fabric::capability::Descriptor;
using CandidatePlan = ::fabric::planning::CandidatePlan;


/**
 * @brief Validate that parsed candidate nodes and edges reference known capabilities.
 */
class CompatibilityValidation : public ValidationBase
{
public:
  void validate(fabric::plan::Plan& plan, std::vector<fabric::capability::Info>& capabilities) override
  {
    (void)plan;
    (void)capabilities;
  }

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "CompatibilityValidation");
  }

  void validate(const PlanningRequest& request, const std::vector<BehaviourInfo>& capabilities,
                CandidatePlan& candidate) const override
  {
    (void)request;

    if (candidate.nodes.empty())
    {
      addReject(candidate, "candidate contains no executable nodes");
      return;
    }

    std::unordered_set<std::string> node_ids;
    for (const auto& node : candidate.nodes)
    {
      if (node.candidate_node_id.empty())
      {
        addReject(candidate, "candidate node is missing a candidate_node_id");
        return;
      }
      if (!node_ids.insert(node.candidate_node_id).second)
      {
        addReject(candidate, "candidate contains duplicate node ids", node.candidate_node_id);
        return;
      }
      if (node.capability_name.empty() || node.provider_name.empty())
      {
        addReject(candidate, "candidate node is missing interface or provider metadata", node.candidate_node_id);
        return;
      }
      if (findCapability(capabilities, node.capability_name, node.provider_name) == nullptr)
      {
        addReject(candidate,
                  "candidate node does not resolve to known capability metadata: " + node.capability_name +
                    " / " + node.provider_name,
                  node.candidate_node_id);
        return;
      }
    }

    for (const auto& edge : candidate.edges)
    {
      if (node_ids.count(edge.source_node_id) == 0 || node_ids.count(edge.target_node_id) == 0)
      {
        addReject(candidate,
                  "candidate edge references an unknown node id: " + edge.source_node_id + " -> " + edge.target_node_id,
                  {}, {}, edge.source_node_id, edge.target_node_id);
        return;
      }
    }
  }

private:
  static const BehaviourInfo* findCapability(const std::vector<BehaviourInfo>& capabilities, const std::string& interface_name,
                                             const std::string& provider_name)
  {
    for (const auto& capability : capabilities)
    {
      if (capability.interface_name == interface_name && capability.provider_name == provider_name)
        return &capability;
    }
    return nullptr;
  }
};

}  // namespace fabric