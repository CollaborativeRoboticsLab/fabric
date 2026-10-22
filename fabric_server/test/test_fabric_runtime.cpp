#include <type_traits>

#include <gtest/gtest.h>
#include <fabric_server/fabric_server.hpp>

static_assert(!std::is_base_of_v<rclcpp::Node, fabric::FabricRuntime>);
static_assert(std::is_base_of_v<rclcpp::Node, fabric::Fabric>);

class FabricRuntimeTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite()
  {
    rclcpp::shutdown();
  }
};

TEST_F(FabricRuntimeTest, CanBeOwnedByANonFabricNodeWithoutCreatingEndpoints)
{
  auto node = std::make_shared<rclcpp::Node>("runtime_boundary_test");
  const auto endpoints = node->get_service_names_and_types();
  fabric::FabricRuntime runtime(*node);
  EXPECT_FALSE(runtime.ready());
  EXPECT_FALSE(runtime.experienceReady());
  EXPECT_EQ(node->get_service_names_and_types(), endpoints);
  const auto status = runtime.getPlanStatus("", "");
  ASSERT_TRUE(status);
  EXPECT_TRUE(status->plan_id.empty());
  EXPECT_FALSE(runtime.getPlanStatus("missing-plan", ""));
  EXPECT_FALSE(runtime.getPlanStatus("", "missing-bond"));
  EXPECT_FALSE(runtime.cancelPlan("missing-plan"));
  EXPECT_FALSE(runtime.completePlan("missing-plan"));
}

TEST_F(FabricRuntimeTest, RejectsGenerationUntilReadyWithoutCallingCallbacks)
{
  auto node = std::make_shared<rclcpp::Node>("runtime_generation_test");
  fabric::FabricRuntime runtime(*node);
  bool called = false;
  fabric::GenerationCallbacks callbacks;
  callbacks.progress = [&](const fabric::GenerationProgress&) { called = true; };
  callbacks.complete = [&](const fabric::GenerationResult&) { called = true; };
  EXPECT_FALSE(runtime.startGeneration({ "test task", "", false, false }, callbacks));
  EXPECT_FALSE(called);
  runtime.shutdownRuntime();
  runtime.shutdownRuntime();
  EXPECT_FALSE(runtime.startGeneration({ "test task", "", false, false }, callbacks));
  EXPECT_FALSE(runtime.ready());
}

TEST_F(FabricRuntimeTest, RejectsExperienceOperationsWithoutLoadedPlugins)
{
  auto node = std::make_shared<rclcpp::Node>("runtime_experience_test");
  fabric::FabricRuntime runtime(*node);
  EXPECT_THROW(runtime.readScoreExplanation({}), std::runtime_error);
  EXPECT_THROW(runtime.readNodeScoreExplanation({}), std::runtime_error);
  EXPECT_THROW(runtime.readScoringEpoch("epoch"), std::runtime_error);
  EXPECT_THROW(runtime.readRawScoringEpoch("epoch"), std::runtime_error);
  EXPECT_THROW(runtime.recordScoringEpoch({}), std::runtime_error);
  EXPECT_THROW(runtime.recordPlanFeedback({}), std::runtime_error);
  EXPECT_THROW(runtime.scorePlan({}, "", "<Plan/>"), std::runtime_error);
}

TEST_F(FabricRuntimeTest, AdapterRequiresExplicitInitialization)
{
  auto node = std::make_shared<fabric::Fabric>();
  EXPECT_THROW(node->runtime(), std::runtime_error);
  EXPECT_NO_THROW(node->shutdownRuntime());
}