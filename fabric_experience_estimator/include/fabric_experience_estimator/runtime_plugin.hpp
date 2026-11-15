#pragma once
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/estimate_base.hpp>

namespace fabric
{
/**
 * @brief Estimates expected runtime in seconds from observed run durations.
 */
class RuntimePlugin : public EstimateBase
{
public:
  RuntimePlugin() = default;
  ~RuntimePlugin() override = default;

  /**
   * @brief Initialize the runtime estimator plugin.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "RuntimePlugin");
  }

  /**
   * @brief Estimate mean runtime from observed run durations.
   *
   * @param evidence Aggregated runtime evidence for the capability.
   * @return EstimateResult Estimated runtime and rationale.
   */
  fabric::experience::EstimateResult estimate(const fabric::experience::EstimatorEvidence& evidence) override
  {
    fabric::experience::EstimateResult result;
    result.name = "runtime_seconds";
    if (evidence.runtime_sample_count <= 0)
    {
      result.reason = "no runtime samples available";
      return result;
    }

    result.value = evidence.runtime_sum_seconds / static_cast<double>(evidence.runtime_sample_count);
    result.available = true;
    result.reason = "mean observed runtime";
    return result;
  }
};
}  // namespace fabric