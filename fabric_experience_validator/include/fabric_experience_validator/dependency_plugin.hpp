#pragma once

#include <fabric_base/validation_base.hpp>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fabric
{

using ValidationBase = ::fabric::ValidationBase;
using PlanningRequest = ::fabric::planning::Request;
using BehaviourInfo = ::fabric::capability::Descriptor;
using CandidatePlan = ::fabric::planning::CandidatePlan;

class DependencyValidation : public ValidationBase
{
public:
  void validate(fabric::plan::Plan& plan, const std::vector<fabric::capability::Descriptor>& capabilities) override
  {
    (void)plan;
    (void)capabilities;
  }

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "DependencyValidation");
  }

  void validate(const PlanningRequest& request, const std::vector<BehaviourInfo>& capabilities,
                CandidatePlan& candidate) const override
  {
    (void)request;

    std::unordered_set<std::string> explicit_skill_names;
    for (const auto& node : candidate.nodes)
      explicit_skill_names.insert(node.capability_name);

    for (const auto& node : candidate.nodes)
    {
      const BehaviourInfo* capability = findCapability(capabilities, node.capability_name, node.provider_name);
      if (capability == nullptr)
        continue;

      std::unordered_set<std::string> warned_dependencies;
      for (const auto& relation : capability->relations)
      {
        if (relation.predicate != "depends_on" || relation.object.empty())
          continue;
        if (!explicit_skill_names.count(relation.object))
          continue;
        if (!warned_dependencies.insert(relation.object).second)
          continue;

        std::ostringstream message;
        message << "plan explicitly includes dependency runner '" << relation.object
                << "' even though it is already an implicit backend dependency of '" << node.capability_name << "'";
        addWarning(candidate, message.str(), node.candidate_node_id);
      }
    }
  }

private:
  static const BehaviourInfo* findCapability(const std::vector<BehaviourInfo>& capabilities,
                                             const std::string& interface_name, const std::string& provider_name)
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