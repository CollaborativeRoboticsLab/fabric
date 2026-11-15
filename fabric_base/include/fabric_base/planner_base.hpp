#pragma once

#include <string>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/parser_base.hpp>
#include <fabric_base/core_base.hpp>
#include <fabric_base/utils/planning_structs.hpp>

namespace fabric
{

/**
 * @brief Base class for planner plugins.
 *
 * Implementations should provide a plan() method that generates a plan based on experience
 * and the current request.
 */
class PlannerBase
{
public:
  virtual ~PlannerBase() = default;

  /**
   * @brief Initialize the planner plugin with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   * @return true if initialization was successful, false otherwise.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "PlannerBasePlugin");
  }

  /**
   * @brief Generate a plan based on experience and the current request.
   *
   * Implementations should populate the provided fabric::planning::Result with candidate plans and select the preferred result.
   *
   * @param request The planning request containing task description and other parameters.
   * @param capabilities Support data for evaluation as a vector of fabric::capability::Descriptor
   * @param core Reference to the CoreBase plugin for accessing knowledge and evidence.
  * @param parser Reference to the active Fabric plan parser.
   *
   */
  virtual fabric::planning::Result plan(const fabric::planning::Request& request, const std::vector<fabric::capability::Descriptor>& capabilities,
                                        CoreBase& core, ParserBase& parser) = 0;

  /**
   * @brief Refine or replan from an existing draft and critique.
   *
   * The default implementation falls back to a fresh plan pass so planner
   * implementations can opt into structured refinement incrementally.
   *
   * @param refinement_request The refinement request containing the draft plan and critique.
   * @param capabilities Support data for evaluation as a vector of fabric::capability::Descriptor
   * @param core Reference to the CoreBase plugin for accessing knowledge and evidence.
  * @param parser Reference to the active Fabric plan parser.
   */
  virtual fabric::planning::Result refinePlan(const fabric::planning::RefinementRequest& refinement_request,
                                              const std::vector<fabric::capability::Descriptor>& capabilities, CoreBase& core,
                                              ParserBase& parser)
  {
    return plan(refinement_request.request, capabilities, core, parser);
  }

protected:
  /**
   * @brief Base initialization method for derived classes.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name of the planner plugin.
   */
  void initialize_base(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name)
  {
    node_ = node;
    plugin_name_ = plugin_name;
  }

  /**
   * @brief Shared pointer to the ROS2 node.
   */
  rclcpp::Node::SharedPtr node_;

  /**
   * @brief Name of the planner plugin.
   */
  std::string plugin_name_;
};

}  // namespace fabric