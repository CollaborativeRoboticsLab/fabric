#pragma once
#include <algorithm>
#include <cmath>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/estimate_base.hpp>

namespace fabric
{
/**
 * @brief Estimates normalized planning uncertainty from sparse and missing evidence.
 */
class UncertaintyPlugin : public EstimateBase
{
public:
  UncertaintyPlugin() = default;
  ~UncertaintyPlugin() override = default;

  /**
   * @brief Initialize Bayesian uncertainty estimator parameters.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "UncertaintyPlugin");
    const std::string parameter_namespace = "experience." + plugin_name_;

    node_->declare_parameter<double>(parameter_namespace + ".alpha", 1.0);
    node_->declare_parameter<double>(parameter_namespace + ".beta", 1.0);
    node_->declare_parameter<double>(parameter_namespace + ".credible_interval_z", 1.96);

    node_->get_parameter(parameter_namespace + ".alpha", alpha_);
    node_->get_parameter(parameter_namespace + ".beta", beta_);
    node_->get_parameter(parameter_namespace + ".credible_interval_z", credible_interval_z_);
  }

  /**
   * @brief Estimate uncertainty from the width of a Beta posterior interval.
   *
   * @param evidence Aggregated success and failure evidence for the capability.
   * @return EstimateResult Estimated uncertainty score and rationale.
   */
  fabric::experience::EstimateResult estimate(const fabric::experience::EstimatorEvidence& evidence) override
  {
    fabric::experience::EstimateResult result;
    result.name = "uncertainty";

    const double successes = static_cast<double>(std::max(0, evidence.runs_succeeded));
    const double failures = static_cast<double>(std::max(0, evidence.runs_completed - evidence.runs_succeeded));
    const double posterior_alpha = alpha_ + successes;
    const double posterior_beta = beta_ + failures;
    const double total = posterior_alpha + posterior_beta;
    const double variance = total > 1.0
                              ? (posterior_alpha * posterior_beta) / (total * total * (total + 1.0))
                              : 0.25;

    result.value =
      std::clamp(2.0 * credible_interval_z_ * std::sqrt(std::max(0.0, variance)), 0.0, 1.0);
    result.available = true;
    result.reason = evidence.runs_completed > 0 ? "beta posterior interval width" : "beta prior interval width";
    return result;
  }

protected:
  double alpha_ = 1.0;
  double beta_ = 1.0;
  double credible_interval_z_ = 1.96;
};
}  // namespace fabric