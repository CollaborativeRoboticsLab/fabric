#pragma once

#include <memory>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/core_base.hpp>
#include <fabric_base/estimate_base.hpp>
#include <fabric_base/utils/planning_structs.hpp>

namespace fabric
{

/**
 * @brief Base class for candidate-plan scoring plugins.
 *
 * Derived scorers share access to the knowledge core and estimator outputs so
 * they can apply a consistent ranking policy across candidate plans.
 */
class ScorerBase
{
public:
  virtual ~ScorerBase() = default;

  /**
   * @brief Initialize the scorer plugin with the owning ROS node.
    *
    * This captures the node handle once so derived scorers can later read
    * parameters, emit logs, and create ROS resources without repeating setup.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "ScorerBasePlugin");
  }

  /**
   * @brief Provide the scorer with access to the core plugin and estimator plugins.
    *
    * Scorers need the core plugin to resolve historical evidence and the
    * estimator plugins to turn that evidence into comparable node features.
   *
   * @param core_plugin Shared core plugin used to resolve evidence and familiarity.
   * @param estimator_plugins Estimator plugin list used to populate node features.
   */
  virtual void configure(const std::shared_ptr<CoreBase>& core_plugin,
                         const std::vector<std::shared_ptr<EstimateBase>>* estimator_plugins)
  {
    core_plugin_ = core_plugin;
    estimator_plugins_ = estimator_plugins;
  }

  /**
   * @brief Score each candidate plan and select the preferred result.
    *
    * Implementations are expected to update candidate score fields in place so
    * downstream components can persist both the numeric result and the reasoning.
   *
   * @param request Planning request that produced the candidates.
   * @param planning_result Candidate plans to score and update in place.
   */
  virtual void scorePlanningResult(const fabric::planning::Request& request, fabric::planning::Result& planning_result) const = 0;

protected:
  /**
   * @brief Store the shared node handle and plugin name for derived scorers.
    *
    * Centralizing this setup keeps derived scorers focused on scoring logic
    * instead of repeating common ROS plugin initialization code.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name registered for the scorer plugin.
   */
  void initialize_base(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name)
  {
    node_ = node;
    plugin_name_ = plugin_name;
  }

  rclcpp::Node::SharedPtr node_;
  std::string plugin_name_;
  std::shared_ptr<CoreBase> core_plugin_;
  const std::vector<std::shared_ptr<EstimateBase>>* estimator_plugins_ = nullptr;
};

}  // namespace fabric