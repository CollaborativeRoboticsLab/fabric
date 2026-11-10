#include <type_traits>
#include <atomic>
#include <thread>

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
  EXPECT_FALSE(runtime.getCurrentPlanSnapshot());
  EXPECT_EQ(node->get_service_names_and_types(), endpoints);
  const auto status = runtime.getPlanStatus("", "");
  ASSERT_TRUE(status);
  EXPECT_TRUE(status->plan_id.empty());
  EXPECT_FALSE(runtime.getPlanStatus("missing-plan", ""));
  EXPECT_FALSE(runtime.getPlanStatus("", "missing-bond"));
  EXPECT_FALSE(runtime.cancelPlan("missing-plan"));
  EXPECT_FALSE(runtime.completePlan("missing-plan"));
}

TEST(PlanSnapshotResponseTest, EmptySnapshotReportsNoPlan)
{
  fabric_msgs::srv::GetFabricPlanSnapshot::Response response;
  fabric::server::plan_snapshot::populateResponse(nullptr, response);

  EXPECT_FALSE(response.has_plan);
  EXPECT_FALSE(response.connections_complete);
  EXPECT_TRUE(response.plan_id.empty());
  EXPECT_TRUE(response.bond_id.empty());
  EXPECT_TRUE(response.instances.empty());
  EXPECT_TRUE(response.connections.empty());
  EXPECT_EQ(response.status.code, fabric_msgs::msg::FabricStatus::UNKNOWN);
}

TEST(PlanSnapshotStoreTest, ClearingCurrentPlanPublishesNoPlanSnapshot)
{
  fabric::CurrentPlanSnapshotStore store;
  fabric::plan::Plan active_plan;
  active_plan.plan_id = "plan-before-reset";
  active_plan.status = fabric::plan::Status::RUNNING;
  active_plan.connections[0].source = { "demo/Move", "demo/Provider", {}, 1 };
  store.publish(active_plan, true, builtin_interfaces::msg::Time());

  store.publish(fabric::plan::Plan(), false, builtin_interfaces::msg::Time());
  const auto snapshot = store.load();
  ASSERT_TRUE(snapshot);

  fabric_msgs::srv::GetFabricPlanSnapshot::Response response;
  fabric::server::plan_snapshot::populateResponse(snapshot.get(), response);

  EXPECT_FALSE(response.has_plan);
  EXPECT_FALSE(response.connections_complete);
  EXPECT_TRUE(response.plan_id.empty());
  EXPECT_TRUE(response.instances.empty());
  EXPECT_TRUE(response.connections.empty());
  EXPECT_EQ(response.status.code, fabric_msgs::msg::FabricStatus::UNKNOWN);
}

TEST(PlanSnapshotResponseTest, PreservesEveryEventAndRepeatedInstances)
{
  fabric::CurrentPlanSnapshot snapshot;
  snapshot.connections_complete = true;
  snapshot.plan.plan_id = "plan-42";
  snapshot.plan.bond_id = "bond-42";
  snapshot.plan.status = fabric::plan::Status::RUNNING;
  snapshot.stamp.sec = 123;
  snapshot.stamp.nanosec = 456;

  auto& first = snapshot.plan.connections[0];
  first.source = { "demo/Move", "demo/Provider", {}, 1 };
  first.on_start = { "demo/Start", "demo/Provider", {}, 2 };
  first.on_stop = { "demo/Stop", "demo/Provider", {}, 3 };
  first.on_success = { "demo/Success", "demo/Provider", {}, 4 };
  first.on_failure = { "demo/Failure", "demo/Provider", {}, 5 };
  first.description = "first connection";

  auto& repeated = snapshot.plan.connections[1];
  repeated.source = { "demo/Move", "demo/Provider", {}, 6 };
  repeated.on_success = { "demo/Next", "demo/Provider", {}, 7 };

  fabric_msgs::srv::GetFabricPlanSnapshot::Response response;
  fabric::server::plan_snapshot::populateResponse(&snapshot, response);

  ASSERT_TRUE(response.has_plan);
  EXPECT_TRUE(response.connections_complete);
  EXPECT_EQ(response.plan_id, "plan-42");
  EXPECT_EQ(response.bond_id, "bond-42");
  EXPECT_EQ(response.status.code, fabric_msgs::msg::FabricStatus::RUNNING);
  EXPECT_EQ(response.header.stamp.sec, 123);
  EXPECT_EQ(response.header.stamp.nanosec, 456u);
  ASSERT_EQ(response.instances.size(), 7u);
  EXPECT_EQ(response.instances[0].instance_id, 1);
  EXPECT_EQ(response.instances[1].instance_id, 2);
  EXPECT_EQ(response.instances[2].instance_id, 3);
  EXPECT_EQ(response.instances[3].instance_id, 4);
  EXPECT_EQ(response.instances[4].instance_id, 5);
  EXPECT_EQ(response.instances[5].instance_id, 6);
  EXPECT_EQ(response.instances[6].instance_id, 7);
  ASSERT_EQ(response.connections.size(), 2u);
  EXPECT_EQ(response.connections[0].source.instance_id, 1);
  EXPECT_EQ(response.connections[0].on_start.instance_id, 2);
  EXPECT_EQ(response.connections[0].on_stop.instance_id, 3);
  EXPECT_EQ(response.connections[0].on_success.instance_id, 4);
  EXPECT_EQ(response.connections[0].on_failure.instance_id, 5);
  EXPECT_EQ(response.connections[0].description, "first connection");
  EXPECT_EQ(response.connections[1].source.instance_id, 6);
  EXPECT_EQ(response.connections[1].on_success.instance_id, 7);
}

TEST(PlanSnapshotResponseTest, KeepsPartialParseDataAndTerminalStatuses)
{
  fabric::CurrentPlanSnapshot snapshot;
  snapshot.plan.plan_id = "plan-partial";
  snapshot.plan.status = fabric::plan::Status::PARSING;

  fabric_msgs::srv::GetFabricPlanSnapshot::Response response;
  fabric::server::plan_snapshot::populateResponse(&snapshot, response);
  ASSERT_TRUE(response.has_plan);
  EXPECT_FALSE(response.connections_complete);
  EXPECT_TRUE(response.connections.empty());
  EXPECT_EQ(response.status.code, fabric_msgs::msg::FabricStatus::PARSING);

  snapshot.plan.status = fabric::plan::Status::PARSE_FAILED;
  snapshot.plan.connections[3].source = { "demo/Partial", "demo/Provider", {}, 9 };

  fabric::server::plan_snapshot::populateResponse(&snapshot, response);
  ASSERT_TRUE(response.has_plan);
  EXPECT_FALSE(response.connections_complete);
  EXPECT_EQ(response.status.code, fabric_msgs::msg::FabricStatus::PARSE_FAILED);
  ASSERT_EQ(response.connections.size(), 1u);
  EXPECT_EQ(response.connections.front().source.instance_id, 9);

  snapshot.connections_complete = true;
  snapshot.plan.connections.clear();
  snapshot.plan.status = fabric::plan::Status::COMPLETED;
  fabric::server::plan_snapshot::populateResponse(&snapshot, response);
  EXPECT_TRUE(response.connections_complete);
  EXPECT_TRUE(response.connections.empty());
  EXPECT_EQ(response.status.code, fabric_msgs::msg::FabricStatus::COMPLETED);

  snapshot.plan.status = fabric::plan::Status::CANCELLED;
  fabric::server::plan_snapshot::populateResponse(&snapshot, response);
  EXPECT_EQ(response.status.code, fabric_msgs::msg::FabricStatus::CANCELLED);
}

TEST(PlanSnapshotStoreTest, ConcurrentReadsObserveConsistentImmutableCopies)
{
  fabric::CurrentPlanSnapshotStore store;
  std::atomic<bool> finished{ false };
  std::atomic<bool> consistent{ true };
  std::thread writer([&]() {
    fabric::plan::Plan plan;
    for (int version = 1; version <= 10000; ++version)
    {
      plan.plan_id = std::to_string(version);
      plan.connections[0].source.instance_id = version;
      plan.connections[0].on_success.instance_id = -version;
      plan.connections[0].description = plan.plan_id;
      builtin_interfaces::msg::Time stamp;
      stamp.sec = version;
      store.publish(plan, version % 2 == 0, stamp);
    }
    finished.store(true, std::memory_order_release);
  });

  while (!finished.load(std::memory_order_acquire))
  {
    const auto snapshot = store.load();
    if (!snapshot)
    {
      continue;
    }
    const int version = std::stoi(snapshot->plan.plan_id);
    const auto& connection = snapshot->plan.connections.at(0);
    if (connection.source.instance_id != version || connection.on_success.instance_id != -version ||
        connection.description != snapshot->plan.plan_id || snapshot->stamp.sec != version || snapshot->connections_complete != (version % 2 == 0))
    {
      consistent.store(false, std::memory_order_relaxed);
      break;
    }
  }
  writer.join();

  EXPECT_TRUE(consistent.load());
  const auto final_snapshot = store.load();
  ASSERT_TRUE(final_snapshot);
  EXPECT_EQ(final_snapshot->plan.plan_id, "10000");
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

TEST_F(FabricRuntimeTest, RecognizesKnowledgeCoreForRebuild)
{
  namespace experience_runtime = fabric::server::experience_runtime;
  EXPECT_TRUE(experience_runtime::isKnowledgeCoreGraphRagPlugin("fabric::KnowledgeCoreGraphRag"));
  EXPECT_FALSE(experience_runtime::isKnowledgeCoreGraphRagPlugin("fabric::CoreGraphRag"));
  EXPECT_FALSE(experience_runtime::isKnowledgeCoreGraphRagPlugin("fabric::GraphKnowledgeCore"));
  EXPECT_EQ(experience_runtime::pluginParameterNamespace("fabric::KnowledgeCoreGraphRag"), "experience.KnowledgeCoreGraphRag");
}
