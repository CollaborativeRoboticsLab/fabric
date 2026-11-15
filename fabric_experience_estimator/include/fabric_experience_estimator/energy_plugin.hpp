#pragma once
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/estimate_base.hpp>

namespace fabric
{
/**
 * @brief Estimates expected battery percentage consumed by a capability run.
 */
class EnergyPlugin : public EstimateBase
{
public:
  EnergyPlugin() = default;
  ~EnergyPlugin() override = default;

  /**
   * @brief Initialize the energy estimator plugin.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "EnergyPlugin");
  }

  /**
   * @brief Estimate mean battery delta percentage from observed runs.
   *
   * @param evidence Aggregated battery-usage evidence for the capability.
   * @return EstimateResult Estimated energy delta and rationale.
   */
  fabric::experience::EstimateResult estimate(const fabric::experience::EstimatorEvidence& evidence) override
  {
    fabric::experience::EstimateResult result;
    result.name = "energy_delta_percentage";
    if (evidence.energy_sample_count <= 0)
    {
      result.reason = "no energy samples available";
      return result;
    }

    result.value = evidence.energy_sum_delta_percentage / static_cast<double>(evidence.energy_sample_count);
    result.available = true;
    result.reason = "mean observed battery delta";
    return result;
  }
};
}  // namespace fabric