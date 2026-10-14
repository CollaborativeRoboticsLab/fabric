#pragma once

#include <string>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/interface_base.hpp>
#include <fabric_base/utils/metadata_structs.hpp>

namespace fabric
{

/**
 * @brief Base class for behaviour-plan parsing adapters.
 */
class BehaviourInterfaceBase : public InterfaceBase
{
public:
  virtual ~BehaviourInterfaceBase() = default;

  /**
   * @brief Initialize the behaviour interface plugin with the owning ROS node.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "BehaviourInterfaceBasePlugin");
  }

  /**
   * @brief Perform any optional background collection required by the interface plugin.
   */
  void gather() override
  {
  }

  /**
   * @brief Parse a plan representation into the internal behaviour-plan structure.
   *
   * @param plan_xml Serialized plan input to parse.
   * @return fabric::capability::PlanDefinition Parsed behaviour-plan definition.
   */
  virtual fabric::capability::PlanDefinition parseBehaviourPlan(const std::string& plan_xml) = 0;
};

using CapabilityInterfaceBase = BehaviourInterfaceBase;

}  // namespace fabric