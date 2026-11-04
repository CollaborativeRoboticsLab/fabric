#include <future>
#include <thread>

#include <gtest/gtest.h>

#include <fabric_server/capability_client.hpp>

class CapabilityLifecycleTest : public ::testing::Test
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

  void SetUp() override
  {
    node_ = std::make_shared<rclcpp::Node>("capability_lifecycle_test");
    client_.initialize(node_);
    executor_.add_node(node_);
  }

  void TearDown() override
  {
    executor_.cancel();
    if (executor_thread_.joinable()) {
      executor_thread_.join();
    }
    executor_.remove_node(node_);
  }

  rclcpp::Node::SharedPtr node_;
  fabric::CapabilityClient client_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  std::thread executor_thread_;
};

TEST_F(CapabilityLifecycleTest, RejectsConflictingProvidersForRepeatedInterface)
{
  std::vector<std::string> requested_providers;
  auto service = node_->create_service<fabric::CapabilityClient::UseCapability>(
    "/capabilities/use_capability",
    [&requested_providers](const fabric::CapabilityClient::UseCapability::Request::SharedPtr request,
    fabric::CapabilityClient::UseCapability::Response::SharedPtr response) {
      requested_providers.push_back(request->preferred_provider);
      response->successful = request->preferred_provider == "demo/First";
      response->error_message = response->successful ? "" : "provider conflict";
    });
  executor_thread_ = std::thread([this]() {executor_.spin();});

  fabric::plan::Plan plan;
  plan.bond_id = "bond";
  plan.connections[0].source.interface = "demo/Move";
  plan.connections[0].source.provider = "demo/First";
  plan.connections[1].source.interface = "demo/Move";
  plan.connections[1].source.provider = "demo/Second";

  EXPECT_THROW(client_.use_capabilities(plan), fabric::fabric_exception);
  EXPECT_EQ(requested_providers, (std::vector<std::string>{"demo/First", "demo/Second"}));
}

TEST_F(CapabilityLifecycleTest, FreesOnlySuccessfulClaimsAfterUseFailure)
{
  std::vector<std::string> freed;
  auto use_service = node_->create_service<fabric::CapabilityClient::UseCapability>(
    "/capabilities/use_capability",
    [](const fabric::CapabilityClient::UseCapability::Request::SharedPtr request,
    fabric::CapabilityClient::UseCapability::Response::SharedPtr response) {
      response->successful = request->capability == "demo/First";
      response->error_message = response->successful ? "" : "start failed";
    });
  auto free_service = node_->create_service<fabric::CapabilityClient::FreeCapability>(
    "/capabilities/free_capability",
    [&freed](const fabric::CapabilityClient::FreeCapability::Request::SharedPtr request,
    fabric::CapabilityClient::FreeCapability::Response::SharedPtr) {
      freed.push_back(request->bond_id + ":" + request->capability);
    });
  executor_thread_ = std::thread([this]() {executor_.spin();});

  fabric::plan::Plan plan;
  plan.bond_id = "bond";
  plan.connections[0].source.interface = "demo/First";
  plan.connections[1].source.interface = "demo/Second";

  EXPECT_THROW(client_.use_capabilities(plan), fabric::fabric_exception);
  EXPECT_NO_THROW(client_.free_capabilities(plan));
  EXPECT_NO_THROW(client_.free_capabilities(plan));
  EXPECT_EQ(freed, (std::vector<std::string>{"bond:demo/First"}));
}

TEST_F(CapabilityLifecycleTest, FreesSharedCapabilitySeparatelyForEachBond)
{
  std::vector<std::string> freed;
  auto use_service = node_->create_service<fabric::CapabilityClient::UseCapability>(
    "/capabilities/use_capability",
    [](const fabric::CapabilityClient::UseCapability::Request::SharedPtr,
    fabric::CapabilityClient::UseCapability::Response::SharedPtr response) {
      response->successful = true;
    });
  auto free_service = node_->create_service<fabric::CapabilityClient::FreeCapability>(
    "/capabilities/free_capability",
    [&freed](const fabric::CapabilityClient::FreeCapability::Request::SharedPtr request,
    fabric::CapabilityClient::FreeCapability::Response::SharedPtr) {
      freed.push_back(request->bond_id + ":" + request->capability);
    });
  executor_thread_ = std::thread([this]() {executor_.spin();});

  fabric::plan::Plan first;
  first.bond_id = "first";
  first.connections[0].source.interface = "demo/Shared";
  fabric::plan::Plan second;
  second.bond_id = "second";
  second.connections[0].source.interface = "demo/Shared";

  auto first_claim = std::async(std::launch::async, [&]() {client_.use_capabilities(first);});
  auto second_claim = std::async(std::launch::async, [&]() {client_.use_capabilities(second);});
  EXPECT_NO_THROW(first_claim.get());
  EXPECT_NO_THROW(second_claim.get());
  EXPECT_NO_THROW(client_.free_capabilities(first));
  EXPECT_EQ(freed, (std::vector<std::string>{"first:demo/Shared"}));
  EXPECT_NO_THROW(client_.free_capabilities(second));
  EXPECT_EQ(freed, (std::vector<std::string>{"first:demo/Shared", "second:demo/Shared"}));
}

TEST_F(CapabilityLifecycleTest, ReportsConnectionAndTriggerFailures)
{
  auto connect_service = node_->create_service<fabric::CapabilityClient::ConnectCapability>(
    "/capabilities/connect_capability",
    [](const fabric::CapabilityClient::ConnectCapability::Request::SharedPtr,
    fabric::CapabilityClient::ConnectCapability::Response::SharedPtr response) {
      response->successful = false;
      response->error_message = "connection failed";
    });
  auto trigger_service = node_->create_service<fabric::CapabilityClient::TriggerCapability>(
    "/capabilities/trigger_capability",
    [](const fabric::CapabilityClient::TriggerCapability::Request::SharedPtr,
    fabric::CapabilityClient::TriggerCapability::Response::SharedPtr response) {
      response->successful = false;
      response->error_message = "trigger failed";
    });
  executor_thread_ = std::thread([this]() {executor_.spin();});

  fabric::plan::Plan plan;
  plan.bond_id = "bond";
  plan.connections[0].source.interface = "demo/First";
  plan.connections[0].source.provider = "demo/Provider";
  plan.connections[0].on_success.interface = "demo/Second";
  plan.connections[0].on_success.provider = "demo/Provider";

  EXPECT_THROW(client_.connect_capabilities(plan), fabric::fabric_exception);
  EXPECT_THROW(client_.trigger_first_node(plan), fabric::fabric_exception);
}
