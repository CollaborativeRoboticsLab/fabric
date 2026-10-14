#pragma once
#include <algorithm>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/estimate_base.hpp>

namespace fabric
{
/**
 * @brief Estimates capability success probability from completed run outcomes.
 */
class SuccessProbabilityPlugin : public EstimateBase
{
public:
  SuccessProbabilityPlugin() = default;
  ~SuccessProbabilityPlugin() override = default;

  /**
   * @brief Initialize the success-probability estimator parameters.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "SuccessProbabilityPlugin");
    
    node_->declare_parameter<double>(plugin_name_ + ".alpha", 1.0);
    node_->declare_parameter<double>(plugin_name_ + ".beta", 1.0);
    node_->declare_parameter<bool>(plugin_name_ + ".use_beta_smoothing", true);

    node_->get_parameter(plugin_name_ + ".alpha", alpha_);
    node_->get_parameter(plugin_name_ + ".beta", beta_);
    node_->get_parameter(plugin_name_ + ".use_beta_smoothing", use_beta_smoothing_);
  }

  /**
   * @brief Estimate success probability from completed run evidence.
   *
   * @param evidence Aggregated run outcomes for the capability.
   * @return EstimateResult Estimated success probability and rationale.
   */
  fabric::experience::EstimateResult estimate(const fabric::experience::EstimatorEvidence& evidence) override
  {
    fabric::experience::EstimateResult result;
    result.name = "success_probability";
    const double completed_runs = static_cast<double>(std::max(0, evidence.runs_completed));
    const double succeeded_runs = static_cast<double>(std::max(0, evidence.runs_succeeded));

    if (use_beta_smoothing_)
    {
      result.value = (succeeded_runs + alpha_) / std::max(1.0, completed_runs + alpha_ + beta_);
      result.available = true;
      result.reason = completed_runs > 0.0 ? "beta-smoothed posterior mean" : "beta prior mean";
      return result;
    }

    result.value = succeeded_runs / std::max(1.0, completed_runs);
    result.available = completed_runs > 0.0;
    result.reason = result.available ? "empirical success ratio" : "no completed runs available";
    return result;
  }

protected:
  double alpha_ = 1.0;
  double beta_ = 1.0;
  bool use_beta_smoothing_ = false;
};
}  // namespace fabric