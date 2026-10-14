#include <gtest/gtest.h>

#include <memory>

#include <rclcpp/rclcpp.hpp>

#include <fabric_experience_estimator/energy_plugin.hpp>
#include <fabric_experience_estimator/fault_burden_plugin.hpp>
#include <fabric_experience_estimator/resource_plugin.hpp>
#include <fabric_experience_estimator/runtime_plugin.hpp>
#include <fabric_experience_estimator/success_probability_plugin.hpp>
#include <fabric_experience_estimator/uncertainity_plugin.hpp>

namespace
{
std::shared_ptr<rclcpp::Node> testNode(const std::string& name)
{
  return std::make_shared<rclcpp::Node>(name);
}
}  // namespace

TEST(RuntimePlugin, ReturnsUnavailableWithoutSamples)
{
  fabric::RuntimePlugin plugin;
  plugin.initialize(testNode("runtime_plugin_empty_test"));

  const auto result = plugin.estimate(fabric::experience::EstimatorEvidence{});

  EXPECT_EQ(result.name, "runtime_seconds");
  EXPECT_FALSE(result.available);
  EXPECT_EQ(result.reason, "no runtime samples available");
}

TEST(RuntimePlugin, ReturnsMeanObservedRuntime)
{
  fabric::RuntimePlugin plugin;
  plugin.initialize(testNode("runtime_plugin_mean_test"));

  fabric::experience::EstimatorEvidence evidence;
  evidence.runtime_sample_count = 4;
  evidence.runtime_sum_seconds = 18.0;
  const auto result = plugin.estimate(evidence);

  EXPECT_TRUE(result.available);
  EXPECT_DOUBLE_EQ(result.value, 4.5);
  EXPECT_EQ(result.reason, "mean observed runtime");
}

TEST(EnergyPlugin, ReturnsMeanObservedBatteryDelta)
{
  fabric::EnergyPlugin plugin;
  plugin.initialize(testNode("energy_plugin_mean_test"));

  fabric::experience::EstimatorEvidence evidence;
  evidence.energy_sample_count = 3;
  evidence.energy_sum_delta_percentage = 9.0;
  const auto result = plugin.estimate(evidence);

  EXPECT_TRUE(result.available);
  EXPECT_DOUBLE_EQ(result.value, 3.0);
  EXPECT_EQ(result.reason, "mean observed battery delta");
}

TEST(FaultBurdenPlugin, ReturnsSeverityWeightedFaultsPerExposure)
{
  fabric::FaultBurdenPlugin plugin;
  plugin.initialize(testNode("fault_burden_plugin_test"));

  fabric::experience::EstimatorEvidence evidence;
  evidence.warning_count = 2;
  evidence.error_count = 1;
  evidence.fault_exposure_count = 5;
  const auto result = plugin.estimate(evidence);

  EXPECT_TRUE(result.available);
  EXPECT_DOUBLE_EQ(result.value, 1.0);
  EXPECT_EQ(result.reason, "severity-weighted faults per exposure");
}

TEST(ResourcePlugin, ReportsMissingResourceDimensions)
{
  fabric::ResourcePlugin plugin;
  plugin.initialize(testNode("resource_plugin_missing_dimensions_test"));

  fabric::experience::EstimatorEvidence evidence;
  evidence.resource_missing_dimensions = "battery_current,arm_load";
  const auto result = plugin.estimate(evidence);

  EXPECT_FALSE(result.available);
  EXPECT_EQ(result.reason, "missing resource dimensions: battery_current,arm_load");
}

TEST(SuccessProbabilityPlugin, UsesBetaPriorWithoutCompletedRuns)
{
  auto node = testNode("success_probability_prior_test");
  fabric::SuccessProbabilityPlugin plugin;
  plugin.initialize(node);

  const auto result = plugin.estimate(fabric::experience::EstimatorEvidence{});

  EXPECT_TRUE(result.available);
  EXPECT_DOUBLE_EQ(result.value, 0.5);
  EXPECT_EQ(result.reason, "beta prior mean");
}

TEST(SuccessProbabilityPlugin, ReturnsPosteriorMeanWithEvidence)
{
  auto node = testNode("success_probability_posterior_test");
  fabric::SuccessProbabilityPlugin plugin;
  plugin.initialize(node);

  fabric::experience::EstimatorEvidence evidence;
  evidence.runs_completed = 4;
  evidence.runs_succeeded = 3;
  const auto result = plugin.estimate(evidence);

  EXPECT_TRUE(result.available);
  EXPECT_DOUBLE_EQ(result.value, 4.0 / 6.0);
  EXPECT_EQ(result.reason, "beta-smoothed posterior mean");
}

TEST(SuccessProbabilityPlugin, ClampsNegativeEvidenceBeforePosteriorUpdate)
{
  auto node = testNode("success_probability_negative_evidence_test");
  fabric::SuccessProbabilityPlugin plugin;
  plugin.initialize(node);

  fabric::experience::EstimatorEvidence evidence;
  evidence.runs_completed = -3;
  evidence.runs_succeeded = -2;
  const auto result = plugin.estimate(evidence);

  EXPECT_TRUE(result.available);
  EXPECT_DOUBLE_EQ(result.value, 0.5);
  EXPECT_EQ(result.reason, "beta prior mean");
}

TEST(UncertaintyPlugin, ShrinksWithMoreCompletedEvidence)
{
  auto node = testNode("uncertainty_plugin_test");
  fabric::UncertaintyPlugin plugin;
  plugin.initialize(node);

  const auto prior = plugin.estimate(fabric::experience::EstimatorEvidence{});

  fabric::experience::EstimatorEvidence evidence;
  evidence.runs_completed = 20;
  evidence.runs_succeeded = 18;
  const auto posterior = plugin.estimate(evidence);

  EXPECT_TRUE(prior.available);
  EXPECT_TRUE(posterior.available);
  EXPECT_EQ(prior.reason, "beta prior interval width");
  EXPECT_EQ(posterior.reason, "beta posterior interval width");
  EXPECT_GT(prior.value, posterior.value);
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}