#pragma once
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/estimate_base.hpp>

namespace fabric
{
/**
 * @brief Reports resource estimates as unavailable until quantitative telemetry exists.
 */
class ResourcePlugin : public EstimateBase
{
public:
  ResourcePlugin() = default;
  ~ResourcePlugin() override = default;

  /**
   * @brief Initialize the resource estimator plugin.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "ResourcePlugin");
  }

  /**
   * @brief Report whether quantitative resource-demand telemetry is available.
   *
   * @param evidence Aggregated resource evidence for the capability.
   * @return EstimateResult Availability state and explanation for resource demand.
   */
  fabric::experience::EstimateResult estimate(const fabric::experience::EstimatorEvidence& evidence) override
  {
    fabric::experience::EstimateResult result;
    result.name = "resource_demand";
    result.reason = evidence.resource_missing_dimensions.empty() ?
                        "resource telemetry has no quantitative dimensions" :
                        "missing resource dimensions: " + evidence.resource_missing_dimensions;
    return result;
  }
};
}  // namespace fabric