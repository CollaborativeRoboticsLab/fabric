#include <thread>

#include <gtest/gtest.h>

#include <fabric_server/capability_client.hpp>

class CapabilityMetadataTest : public ::testing::Test
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
    node_ = std::make_shared<rclcpp::Node>("capability_metadata_test");
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

  void serve(const capabilities2_msgs::srv::GetRunnableSpecs::Response & response)
  {
    service_ = node_->create_service<capabilities2_msgs::srv::GetRunnableSpecs>(
      "/capabilities/get_runnable_specs",
      [response](const capabilities2_msgs::srv::GetRunnableSpecs::Request::SharedPtr,
      capabilities2_msgs::srv::GetRunnableSpecs::Response::SharedPtr result) {
        *result = response;
      });
    executor_thread_ = std::thread([this]() {executor_.spin();});
  }

  rclcpp::Node::SharedPtr node_;
  fabric::CapabilityClient client_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  std::thread executor_thread_;
  rclcpp::Service<capabilities2_msgs::srv::GetRunnableSpecs>::SharedPtr service_;
};

TEST_F(CapabilityMetadataTest, CollectsRunnableSpecsAsNativeDescriptors)
{
  capabilities2_msgs::srv::GetRunnableSpecs::Response response;
  response.index_of_specs.push_back("demo/Provider");
  capabilities2_msgs::msg::RunnableSpec spec;
  spec.spec =
    R"(
interface:
  name: demo/Move
  description: Move to a target
provider:
  name: demo/Provider
  description: Target provider
definition:
  command: '<Runner interface="demo/Move" provider="demo/Provider" />'
  configuration_parameters:
    - {name: speed, type: double, default: 0.5}
  runtime_parameters:
    input:
      - {name: target, type: string, required: true, semantic_key: pose, aliases: [goal]}
    output:
      - {name: reached, type: bool}
relations:
  - {subject: demo/Move, predicate: depends_on, object: demo/Localization}
)";
  response.runnable_specs.push_back(spec);
  serve(response);

  std::vector<fabric::capability::Descriptor> capabilities;
  client_.getRunnableSpecs(capabilities);
  ASSERT_EQ(capabilities.size(), 1u);
  const auto & descriptor = capabilities.front();
  EXPECT_EQ(descriptor.interface_name, "demo/Move");
  EXPECT_EQ(descriptor.provider_name, "demo/Provider");
  EXPECT_EQ(descriptor.interface_description, "Move to a target");
  EXPECT_EQ(descriptor.provider_description, "Target provider");
  EXPECT_NE(descriptor.command.find("demo/Move"), std::string::npos);
  ASSERT_EQ(descriptor.configuration_parameters.size(), 1u);
  EXPECT_TRUE(descriptor.configuration_parameters.front().has_default);
  EXPECT_EQ(descriptor.configuration_parameters.front().default_value, "0.5");
  ASSERT_EQ(descriptor.runtime_input_parameters.size(), 1u);
  EXPECT_TRUE(descriptor.runtime_input_parameters.front().required);
  EXPECT_EQ(descriptor.runtime_input_parameters.front().semantic_key, "pose");
  ASSERT_EQ(descriptor.runtime_input_parameters.front().aliases.size(), 1u);
  EXPECT_EQ(descriptor.runtime_input_parameters.front().aliases.front(), "goal");
  ASSERT_EQ(descriptor.runtime_output_parameters.size(), 1u);
  EXPECT_EQ(descriptor.runtime_output_parameters.front().name, "reached");
  ASSERT_EQ(descriptor.relations.size(), 1u);
  EXPECT_EQ(descriptor.relations.front().predicate, "depends_on");
  EXPECT_EQ(descriptor.relations.front().object, "demo/Localization");
}

TEST_F(CapabilityMetadataTest, ReportsMalformedResponseOnTheCallingThread)
{
  capabilities2_msgs::srv::GetRunnableSpecs::Response response;
  response.index_of_specs.push_back("missing-payload");
  serve(response);
  std::vector<fabric::capability::Descriptor> capabilities;
  EXPECT_THROW(client_.getRunnableSpecs(capabilities), fabric::fabric_exception);
  EXPECT_TRUE(capabilities.empty());
}

TEST_F(CapabilityMetadataTest, AcceptsMissingNullAndEmptyDefinitionMetadata)
{
  const std::vector<std::string> definitions{
    "",
    "definition: null\n",
    "definition: {}\n",
    "definition: {command: echo}\n",
    "definition: {runtime_parameters: null}\n",
    "definition: {runtime_parameters: {}}\n",
  };
  capabilities2_msgs::srv::GetRunnableSpecs::Response response;
  for (size_t index = 0; index < definitions.size(); ++index) {
    response.index_of_specs.push_back("demo/Provider" + std::to_string(index));
    capabilities2_msgs::msg::RunnableSpec spec;
    spec.spec =
      "interface: {name: demo/Move, description: Move to a target}\n"
      "provider: {name: demo/Provider, description: Target provider}\n"
      "relations: [{subject: demo/Move, predicate: depends_on, object: demo/Localization}]\n" +
      definitions[index];
    response.runnable_specs.push_back(spec);
  }
  serve(response);

  std::vector<fabric::capability::Descriptor> capabilities;
  ASSERT_NO_THROW(client_.getRunnableSpecs(capabilities));
  ASSERT_EQ(capabilities.size(), definitions.size());
  for (size_t index = 0; index < capabilities.size(); ++index) {
    SCOPED_TRACE(definitions[index]);
    const auto & descriptor = capabilities[index];
    EXPECT_EQ(descriptor.interface_name, "demo/Move");
    EXPECT_EQ(descriptor.provider_name, "demo/Provider");
    EXPECT_EQ(descriptor.interface_description, "Move to a target");
    EXPECT_EQ(descriptor.provider_description, "Target provider");
    EXPECT_EQ(descriptor.command, index == 3 ? "echo" : "");
    EXPECT_TRUE(descriptor.configuration_parameters.empty());
    EXPECT_TRUE(descriptor.runtime_input_parameters.empty());
    EXPECT_TRUE(descriptor.runtime_output_parameters.empty());
    ASSERT_EQ(descriptor.relations.size(), 1u);
    EXPECT_EQ(descriptor.relations.front().object, "demo/Localization");
  }

  std::vector<fabric::capability::Info> provider_capabilities(1);
  provider_capabilities.front().provider = "demo/Provider";
  ASSERT_NO_THROW(client_.getRunnableSpecs(provider_capabilities));
  ASSERT_EQ(provider_capabilities.front().provider_details.size(), 1u);
  const auto & provider = provider_capabilities.front().provider_details.front();
  EXPECT_EQ(provider.provider, "demo/Provider");
  EXPECT_TRUE(provider.configuration_parameters.empty());
  EXPECT_TRUE(provider.runtime_input_parameters.empty());
  EXPECT_TRUE(provider.runtime_output_parameters.empty());
}
