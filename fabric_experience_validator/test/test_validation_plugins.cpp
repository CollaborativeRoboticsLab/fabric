#include <gtest/gtest.h>

#include <fabric_experience_validator/compatibility_plugin.hpp>
#include <fabric_experience_validator/dataflow_plugin.hpp>
#include <fabric_experience_validator/dependency_plugin.hpp>
#include <fabric_experience_validator/semantic_plugin.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

namespace
{
using Parameter = fabric::capability::ParameterInfo;
using BehaviourInfo = fabric::capability::Descriptor;

Parameter parameter(const std::string& name, const std::string& type, const std::string& semantic_key,
                    const bool required = false)
{
  Parameter output;
  output.name = name;
  output.type = type;
  output.semantic_key = semantic_key;
  output.required = required;
  return output;
}

BehaviourInfo producerCapability()
{
  BehaviourInfo capability;
  capability.interface_name = "demo/DetectObject";
  capability.provider_name = "demo_provider";
  capability.runtime_output_parameters.push_back(parameter("object_pose", "geometry_msgs/PoseStamped", "object_pose"));
  return capability;
}

BehaviourInfo consumerCapability()
{
  BehaviourInfo capability;
  capability.interface_name = "demo/GraspObject";
  capability.provider_name = "demo_provider";
  Parameter input = parameter("object_pose", "geometry_msgs/PoseStamped", "object_pose", true);
  input.satisfiable_from = { "upstream" };
  capability.runtime_input_parameters.push_back(input);
  return capability;
}

BehaviourInfo navLaunchCapability()
{
  BehaviourInfo capability;
  capability.interface_name = "nav2_capabilities/Nav2LaunchRunner";
  capability.provider_name = "nav2_capabilities/Nav2LaunchRunner";
  capability.interface_description = "Launches Nav2 resources.";
  return capability;
}

BehaviourInfo waypointCapabilityWithImplicitDependency()
{
  BehaviourInfo capability;
  capability.interface_name = "nav2_capabilities/WaypointRunner";
  capability.provider_name = "nav2_capabilities/WaypointRunner";
  capability.interface_description = "Navigates to a target pose.";
  capability.relations.push_back(
      { "nav2_capabilities/WaypointRunner", "depends_on", "nav2_capabilities/Nav2LaunchRunner" });
  return capability;
}

BehaviourInfo aliasConsumerCapability()
{
  BehaviourInfo capability = consumerCapability();
  capability.runtime_input_parameters.front().name = "goal_pose";
  capability.runtime_input_parameters.front().aliases = { "object_pose" };
  capability.runtime_input_parameters.front().semantic_key.clear();
  return capability;
}

BehaviourInfo configurationFallbackCapability()
{
  BehaviourInfo capability;
  capability.interface_name = "demo/ConfigureTarget";
  capability.provider_name = "demo_provider";
  Parameter input = parameter("object_pose", "geometry_msgs/PoseStamped", "object_pose", true);
  input.satisfiable_from = { "configuration" };
  capability.runtime_input_parameters.push_back(input);
  Parameter config = parameter("object_pose", "geometry_msgs/PoseStamped", "object_pose");
  capability.configuration_parameters.push_back(config);
  return capability;
}

BehaviourInfo defaultFallbackCapability()
{
  BehaviourInfo capability;
  capability.interface_name = "demo/UseDefaultPose";
  capability.provider_name = "demo_provider";
  Parameter input = parameter("object_pose", "geometry_msgs/PoseStamped", "object_pose", true);
  input.satisfiable_from = { "default" };
  input.has_default = true;
  capability.runtime_input_parameters.push_back(input);
  return capability;
}

BehaviourInfo externalFallbackCapability()
{
  BehaviourInfo capability;
  capability.interface_name = "demo/InjectExternalPose";
  capability.provider_name = "demo_provider";
  Parameter input = parameter("object_pose", "geometry_msgs/PoseStamped", "object_pose", true);
  input.satisfiable_from = { "external" };
  capability.runtime_input_parameters.push_back(input);
  return capability;
}

fabric::planning::CandidatePlanNode node(const std::string& id, const BehaviourInfo& capability)
{
  fabric::planning::CandidatePlanNode plan_node;
  plan_node.candidate_node_id = id;
  plan_node.capability_name = capability.interface_name;
  plan_node.provider_name = capability.provider_name;
  plan_node.capability_id = id;
  return plan_node;
}

fabric::planning::CandidatePlanEdge edge(const std::string& source, const std::string& target)
{
  fabric::planning::CandidatePlanEdge plan_edge;
  plan_edge.source_node_id = source;
  plan_edge.target_node_id = target;
  plan_edge.source_capability_id = source;
  plan_edge.target_capability_id = target;
  plan_edge.edge_label = "on_success";
  return plan_edge;
}

fabric::planning::Request request()
{
  fabric::planning::Request planning_request;
  planning_request.planning_request_id = "test_request";
  planning_request.task_description = "test task";
  return planning_request;
}

std::shared_ptr<rclcpp::Node> testNode(const std::string& name)
{
  return std::make_shared<rclcpp::Node>(name);
}

class StubSemanticValidation : public fabric::SemanticValidation
{
public:
  using Decision = fabric::SemanticValidation::SemanticValidationDecision;
  using fabric::SemanticValidation::buildSemanticPrompt;
  using fabric::SemanticValidation::parseSemanticDecision;

  void setDecision(const std::optional<Decision>& decision)
  {
    decision_ = decision;
  }

protected:
  std::optional<Decision> requestSemanticDecision(const fabric::planning::Request&, const std::vector<BehaviourInfo>&,
                                                  const fabric::planning::CandidatePlan&) const override
  {
    return decision_;
  }

private:
  std::optional<Decision> decision_;
};
}  // namespace

TEST(ExtendedCompatibilityValidation, RejectsUnknownCapability)
{
  auto node_handle = testNode("compatibility_validation_test");
  fabric::ExtendedCompatibilityValidation validator;
  validator.initialize(node_handle);

  const auto known = producerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", known));
  candidate.nodes.front().provider_name = "missing_provider";

  validator.validate(request(), { known }, candidate);

  EXPECT_TRUE(candidate.validation.rejected);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_EQ(candidate.validation.findings.front().level, fabric::planning::ValidationFindingLevel::Reject);
  EXPECT_EQ(candidate.validation.findings.front().plugin_name, "ExtendedCompatibilityValidation");
}

TEST(DependencyValidation, WarnsWhenImplicitDependencyIsExplicitlyPlanned)
{
  auto node_handle = testNode("dependency_validation_warns_test");
  fabric::DependencyValidation validator;
  validator.initialize(node_handle);

  const auto waypoint = waypointCapabilityWithImplicitDependency();
  const auto nav_launch = navLaunchCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", nav_launch));
  candidate.nodes.push_back(node("runner_2", waypoint));

  validator.validate(request(), { waypoint, nav_launch }, candidate);

  EXPECT_FALSE(candidate.validation.rejected);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_EQ(candidate.validation.findings.front().level, fabric::planning::ValidationFindingLevel::Warning);
  EXPECT_NE(candidate.validation.findings.front().message.find("implicit backend dependency"), std::string::npos);
}

TEST(DependencyValidation, DoesNotWarnWhenDependencyIsNotExplicitlyPlanned)
{
  auto node_handle = testNode("dependency_validation_no_warning_test");
  fabric::DependencyValidation validator;
  validator.initialize(node_handle);

  const auto waypoint = waypointCapabilityWithImplicitDependency();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", waypoint));

  validator.validate(request(), { waypoint }, candidate);

  EXPECT_TRUE(candidate.validation.findings.empty());
}

TEST(ExtendedCompatibilityValidation, RejectsDuplicateNodeIds)
{
  auto node_handle = testNode("compatibility_validation_duplicate_node_test");
  fabric::ExtendedCompatibilityValidation validator;
  validator.initialize(node_handle);

  const auto known = producerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", known));
  candidate.nodes.push_back(node("runner_1", known));

  validator.validate(request(), { known }, candidate);

  EXPECT_TRUE(candidate.validation.rejected);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_NE(candidate.validation.summary.find("duplicate node ids"), std::string::npos);
}

TEST(ExtendedCompatibilityValidation, RejectsEdgeReferencingUnknownNode)
{
  auto node_handle = testNode("compatibility_validation_unknown_edge_test");
  fabric::ExtendedCompatibilityValidation validator;
  validator.initialize(node_handle);

  const auto known = producerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", known));
  candidate.edges.push_back(edge("runner_1", "runner_2"));

  validator.validate(request(), { known }, candidate);

  EXPECT_TRUE(candidate.validation.rejected);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_NE(candidate.validation.summary.find("unknown node id"), std::string::npos);
}

TEST(DataflowValidation, AcceptsOrderedUpstreamProducer)
{
  auto node_handle = testNode("dataflow_validation_ordered_test");
  fabric::DataflowValidation validator;
  validator.initialize(node_handle);

  const auto producer = producerCapability();
  const auto consumer = consumerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", producer));
  candidate.nodes.push_back(node("runner_2", consumer));
  candidate.edges.push_back(edge("runner_1", "runner_2"));

  validator.validate(request(), { producer, consumer }, candidate);

  EXPECT_FALSE(candidate.validation.rejected);
}

TEST(DataflowValidation, RejectsReverseOrderedUpstreamProducer)
{
  auto node_handle = testNode("dataflow_validation_reverse_order_test");
  fabric::DataflowValidation validator;
  validator.initialize(node_handle);

  const auto producer = producerCapability();
  const auto consumer = consumerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_2", consumer));
  candidate.nodes.push_back(node("runner_1", producer));
  candidate.edges.push_back(edge("runner_1", "runner_2"));

  validator.validate(request(), { producer, consumer }, candidate);

  EXPECT_TRUE(candidate.validation.rejected);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_NE(candidate.validation.summary.find("appears after target node"), std::string::npos);
}

TEST(DataflowValidation, RejectsMissingRequiredUpstreamInput)
{
  auto node_handle = testNode("dataflow_validation_missing_input_test");
  fabric::DataflowValidation validator;
  validator.initialize(node_handle);

  const auto consumer = consumerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", consumer));

  validator.validate(request(), { consumer }, candidate);

  EXPECT_TRUE(candidate.validation.rejected);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_NE(candidate.validation.summary.find("required runtime input"), std::string::npos);
}

TEST(DataflowValidation, WarnsWhenOnlyAliasBasedUpstreamMatchExists)
{
  auto node_handle = testNode("dataflow_validation_alias_match_test");
  fabric::DataflowValidation validator;
  validator.initialize(node_handle);

  const auto producer = producerCapability();
  const auto consumer = aliasConsumerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", producer));
  candidate.nodes.push_back(node("runner_2", consumer));
  candidate.edges.push_back(edge("runner_1", "runner_2"));

  validator.validate(request(), { producer, consumer }, candidate);

  EXPECT_FALSE(candidate.validation.rejected);
  EXPECT_GT(candidate.validation.total_penalty, 0.0);
  ASSERT_GE(candidate.validation.findings.size(), 2u);
  EXPECT_EQ(candidate.validation.findings.front().level, fabric::planning::ValidationFindingLevel::Warning);
}

TEST(DataflowValidation, WarnsWhenConfigurationFallbackIsUsed)
{
  auto node_handle = testNode("dataflow_validation_configuration_fallback_test");
  fabric::DataflowValidation validator;
  validator.initialize(node_handle);

  const auto consumer = configurationFallbackCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", consumer));

  validator.validate(request(), { consumer }, candidate);

  EXPECT_FALSE(candidate.validation.rejected);
  EXPECT_GT(candidate.validation.total_penalty, 0.0);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_EQ(candidate.validation.findings.front().level, fabric::planning::ValidationFindingLevel::Warning);
}

TEST(DataflowValidation, WarnsWhenDefaultFallbackIsUsed)
{
  auto node_handle = testNode("dataflow_validation_default_fallback_test");
  fabric::DataflowValidation validator;
  validator.initialize(node_handle);

  const auto consumer = defaultFallbackCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", consumer));

  validator.validate(request(), { consumer }, candidate);

  EXPECT_FALSE(candidate.validation.rejected);
  EXPECT_GT(candidate.validation.total_penalty, 0.0);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_EQ(candidate.validation.findings.front().level, fabric::planning::ValidationFindingLevel::Warning);
}

TEST(DataflowValidation, WarnsWhenExternalFallbackIsUsed)
{
  auto node_handle = testNode("dataflow_validation_external_fallback_test");
  fabric::DataflowValidation validator;
  validator.initialize(node_handle);

  const auto consumer = externalFallbackCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", consumer));

  validator.validate(request(), { consumer }, candidate);

  EXPECT_FALSE(candidate.validation.rejected);
  EXPECT_GT(candidate.validation.total_penalty, 0.0);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_EQ(candidate.validation.findings.front().level, fabric::planning::ValidationFindingLevel::Warning);
}

TEST(DataflowValidation, UsesExperienceScopedPenaltyAndPreservesFindingIdentity)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides({rclcpp::Parameter("experience.DataflowValidation.external_penalty", 0.31)});
  auto node_handle = std::make_shared<rclcpp::Node>("dataflow_scoped_penalty_test", options);
  fabric::DataflowValidation validator;
  validator.initialize(node_handle);
  const auto consumer = externalFallbackCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", consumer));

  validator.validate(request(), { consumer }, candidate);

  EXPECT_DOUBLE_EQ(candidate.validation.total_penalty, 0.31 + 0.05);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_EQ(candidate.validation.findings.front().plugin_name, "DataflowValidation");
}

TEST(SemanticValidation, UsesExperienceScopedAvailabilitySetting)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides({rclcpp::Parameter("experience.SemanticValidation.warn_when_unavailable", false)});
  auto node_handle = std::make_shared<rclcpp::Node>("semantic_scoped_setting_test", options);
  StubSemanticValidation validator;
  validator.initialize(node_handle);
  validator.setDecision(std::nullopt);
  const auto producer = producerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", producer));

  validator.validate(request(), { producer }, candidate);

  EXPECT_TRUE(candidate.validation.findings.empty());
}

TEST(SemanticValidation, AcceptsAlignedPlan)
{
  auto node_handle = testNode("semantic_validation_accepts_plan_test");
  StubSemanticValidation validator;
  validator.initialize(node_handle);

  StubSemanticValidation::Decision decision;
  decision.aligned = true;
  decision.confidence = 0.91;
  decision.reason = "ordered actions match the requested pick task";
  validator.setDecision(decision);

  const auto producer = producerCapability();
  const auto consumer = consumerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", producer));
  candidate.nodes.push_back(node("runner_2", consumer));
  candidate.edges.push_back(edge("runner_1", "runner_2"));

  auto req = request();
  req.task_description = "detect the apple and grasp it";
  validator.validate(req, { producer, consumer }, candidate);

  EXPECT_FALSE(candidate.validation.rejected);
}

TEST(SemanticValidation, RejectsSemanticallyMismatchedPlan)
{
  auto node_handle = testNode("semantic_validation_rejects_mismatch_test");
  StubSemanticValidation validator;
  validator.initialize(node_handle);

  StubSemanticValidation::Decision decision;
  decision.aligned = false;
  decision.confidence = 0.95;
  decision.reason = "the plan only detects an object and never performs the requested grasp";
  decision.unsupported_steps = { "runner_1" };
  validator.setDecision(decision);

  const auto producer = producerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", producer));

  auto req = request();
  req.task_description = "pick the apple";
  validator.validate(req, { producer }, candidate);

  EXPECT_TRUE(candidate.validation.rejected);
  EXPECT_NE(candidate.validation.summary.find("semantic validation rejected candidate"), std::string::npos);
}

TEST(SemanticValidation, RejectsWrongOrderPlan)
{
  auto node_handle = testNode("semantic_validation_rejects_wrong_order_test");
  StubSemanticValidation validator;
  validator.initialize(node_handle);

  StubSemanticValidation::Decision decision;
  decision.aligned = false;
  decision.confidence = 0.88;
  decision.reason = "the plan attempts grasping before object detection provides the pose context";
  decision.unsupported_steps = { "runner_2", "runner_1" };
  validator.setDecision(decision);

  const auto producer = producerCapability();
  const auto consumer = consumerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_2", consumer));
  candidate.nodes.push_back(node("runner_1", producer));

  auto req = request();
  req.task_description = "pick the apple";
  validator.validate(req, { producer, consumer }, candidate);

  EXPECT_TRUE(candidate.validation.rejected);
}

TEST(SemanticValidation, WarnsWhenSemanticCheckIsUnavailable)
{
  auto node_handle = testNode("semantic_validation_warns_unavailable_test");
  StubSemanticValidation validator;
  validator.initialize(node_handle);
  validator.setDecision(std::nullopt);

  const auto producer = producerCapability();
  fabric::planning::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", producer));

  auto req = request();
  req.task_description = "inspect the workspace";
  validator.validate(req, { producer }, candidate);

  EXPECT_FALSE(candidate.validation.rejected);
  ASSERT_FALSE(candidate.validation.findings.empty());
  EXPECT_EQ(candidate.validation.findings.front().level, fabric::planning::ValidationFindingLevel::Warning);
}

TEST(SemanticValidation, PenalizesLowConfidenceAlignment)
{
  auto node_handle = testNode("semantic_validation_low_confidence_test");
  StubSemanticValidation validator;
  validator.initialize(node_handle);

  StubSemanticValidation::Decision decision;
  decision.aligned = true;
  decision.confidence = 0.42;
  decision.reason = "the plan is plausible but misses some task-specific intent details";
  validator.setDecision(decision);

  const auto producer = producerCapability();
  fabric::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", producer));

  auto req = request();
  req.task_description = "inspect the workspace";
  validator.validate(req, { producer }, candidate);

  EXPECT_FALSE(candidate.validation.rejected);
  EXPECT_GT(candidate.validation.total_penalty, 0.0);
}

TEST(SemanticValidation, ParsesJsonDecisionResponse)
{
  StubSemanticValidation validator;
  const auto decision = validator.parseSemanticDecision(
      R"({"aligned":false,"confidence":0.93,"reason":"wrong sequence","unsupported_steps":["runner_2"]})");

  ASSERT_TRUE(decision.has_value());
  EXPECT_FALSE(decision->aligned);
  EXPECT_DOUBLE_EQ(decision->confidence, 0.93);
  ASSERT_EQ(decision->unsupported_steps.size(), 1u);
  EXPECT_EQ(decision->unsupported_steps.front(), "runner_2");
}

TEST(SemanticValidation, BuildsOrderedDescriptionPromptWithoutParameterSchema)
{
  StubSemanticValidation validator;

  auto req = request();
  req.task_description = "pick the apple from the table";

  auto producer = producerCapability();
  producer.interface_description = "detects the target object on the table";
  producer.provider_description = "vision provider that localizes apples";
  auto consumer = consumerCapability();
  consumer.interface_description = "grasps the detected object";
  consumer.provider_description = "gripper provider that closes on the target pose";
  consumer.relations.push_back({ "demo/GraspObject", "depends_on", "demo/NavStackReady" });

  fabric::CandidatePlan candidate;
  candidate.nodes.push_back(node("runner_1", producer));
  candidate.nodes.push_back(node("runner_2", consumer));

  const std::string prompt = validator.buildSemanticPrompt(req, { producer, consumer }, candidate);

  EXPECT_NE(prompt.find("Ordered capability descriptions:"), std::string::npos);
  EXPECT_NE(prompt.find("interface_description: detects the target object on the table"), std::string::npos);
  EXPECT_NE(prompt.find("provider_description: vision provider that localizes apples"), std::string::npos);
  EXPECT_NE(prompt.find("interface_description: grasps the detected object"), std::string::npos);
  EXPECT_NE(prompt.find("backend-managed dependencies that are auto-started by capabilities2"), std::string::npos);
  EXPECT_NE(prompt.find("implicit_prerequisites: [demo/NavStackReady]"), std::string::npos);
  EXPECT_EQ(prompt.find("runtime_inputs:"), std::string::npos);
  EXPECT_EQ(prompt.find("runtime_outputs:"), std::string::npos);
  EXPECT_EQ(prompt.find("relations:"), std::string::npos);
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
