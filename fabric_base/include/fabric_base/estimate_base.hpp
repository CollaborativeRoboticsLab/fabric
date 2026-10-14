#pragma once

#include <cstdint>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/utils/experience_structs.hpp>

namespace fabric
{

/**
 * @brief Base class for estimation plugins.
 *
 * Implementations should provide a estimate() method that estimates the uncertainty of the request or task
 */
class EstimateBase
{
public:
  virtual ~EstimateBase() = default;

  /**
   * @brief Initialize the estimate plugin with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "EstimateBasePlugin");
  }

  /**
   * @brief Estimate a metric from the supplied evidence bundle.
   *
   * @param evidence Aggregated capability evidence used by the estimator.
   * @return EstimateResult Estimated value, availability flag, and explanation.
   */
  virtual fabric::experience::EstimateResult estimate(const fabric::experience::EstimatorEvidence& evidence) = 0;

  /**
   * @brief Estimate a metric using an empty evidence bundle.
   *
   * @return EstimateResult Estimated value produced from default evidence.
   */
  virtual fabric::experience::EstimateResult estimate()
  {
    return estimate(fabric::experience::EstimatorEvidence{});
  }

  /**
   * @brief Return the configured plugin name.
   *
   * @return std::string Plugin name registered during initialization.
   */
  std::string plugin_name() const
  {
    return plugin_name_;
  }

protected:
  /**
   * @brief Base initialization method for derived classes.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name of the estimate plugin.
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
   * @brief Name of the estimate plugin.
   */
  std::string plugin_name_;
};

}  // namespace fabric