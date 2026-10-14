#pragma once
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/estimate_base.hpp>

namespace fabric
{
/**
 * @brief Estimates severity-weighted fault burden per capability exposure.
 */
class FaultBurdenPlugin : public EstimateBase
{
public:
  FaultBurdenPlugin() = default;
  ~FaultBurdenPlugin() override = default;

  /**
   * @brief Initialize fault-burden weighting parameters.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "FaultBurdenPlugin");

    node_->declare_parameter<double>(plugin_name_ + ".warning_weight", 1.0);
    node_->declare_parameter<double>(plugin_name_ + ".error_weight", 3.0);

    node_->get_parameter(plugin_name_ + ".warning_weight", warning_weight_);
    node_->get_parameter(plugin_name_ + ".error_weight", error_weight_);
  }

  /**
   * @brief Estimate severity-weighted fault burden per observed exposure.
   *
   * @param evidence Aggregated warning, error, and exposure evidence.
   * @return EstimateResult Estimated fault burden and rationale.
   */
  fabric::experience::EstimateResult estimate(const fabric::experience::EstimatorEvidence& evidence) override
  {
    fabric::experience::EstimateResult result;
    result.name = "fault_burden";
    const int32_t exposure_count =
        evidence.fault_exposure_count > 0 ? evidence.fault_exposure_count : evidence.runs_completed;
    if (exposure_count <= 0)
    {
      result.reason = "no fault exposure samples available";
      return result;
    }

    const double weighted_faults = warning_weight_ * static_cast<double>(evidence.warning_count) +
                                   error_weight_ * static_cast<double>(evidence.error_count);
    result.value = weighted_faults / static_cast<double>(exposure_count);
    result.available = true;
    result.reason = "severity-weighted faults per exposure";
    return result;
  }

protected:
  double warning_weight_ = 1.0;
  double error_weight_ = 3.0;
};
}  // namespace fabric