#include <gtest/gtest.h>

#include <fabric_experience_planner/planning_plugin.hpp>

namespace
{

class TestPlanningPlugin : public fabric::PlanningPlugin
{
public:
  using CapabilityGenerationContext = PlanningPlugin::CapabilityGenerationContext;
  using PlanningPlugin::buildGenerationPrompt;
  using PlanningPlugin::max_capability_context_;
  using PlanningPlugin::model_family_;
};

fabric::planning::Request request()
{
  fabric::planning::Request req;
  req.task_description = "navigate to the charging dock";
  return req;
}

fabric::experience::CandidateSkillResolution resolution()
{
  fabric::experience::CandidateSkillResolution value;
  value.fallback_level = "empirical";
  value.reason = "observed in prior runs";
  value.evidence.runs_completed = 3;
  value.evidence.runtime_sample_count = 2;
  return value;
}

}  // namespace

TEST(PlanningPluginParameters, UsesExperienceScopedOverrides)
{
  rclcpp::NodeOptions options;
  options.parameter_overrides({rclcpp::Parameter("experience.PlanningPlugin.model_family", "test_family"),
                               rclcpp::Parameter("experience.PlanningPlugin.max_capability_context", 3)});
  auto node = std::make_shared<rclcpp::Node>("planning_plugin_parameters_test", options);
  auto service = node->create_service<prompt_msgs::srv::Prompt>(
      "prompt/prompt", [](const std::shared_ptr<prompt_msgs::srv::Prompt::Request>,
                          std::shared_ptr<prompt_msgs::srv::Prompt::Response>) {});
  TestPlanningPlugin plugin;
  plugin.initialize(node);

  EXPECT_EQ(plugin.model_family_, "test_family");
  EXPECT_EQ(plugin.max_capability_context_, 3);
  EXPECT_FALSE(node->has_parameter("PlanningPlugin.model_family"));
}

TEST(PlanningPluginPrompt, FormatsDependsOnAsImplicitPrerequisites)
{
  TestPlanningPlugin plugin;

  fabric::capability::Descriptor capability;
  capability.interface_name = "nav2_capabilities/WaypointRunner";
  capability.provider_name = "nav2_capabilities/WaypointRunner";
  capability.interface_description = "Navigate to a target pose.";
  capability.command = "<Runner interface=nav2_capabilities/WaypointRunner provider=nav2_capabilities/WaypointRunner "
                       "x='$x' y='$y' />";
  capability.relations.push_back(
      { "nav2_capabilities/WaypointRunner", "depends_on", "nav2_capabilities/Nav2LaunchRunner" });
  capability.relations.push_back({ "nav2_capabilities/WaypointRunner", "uses", "nav2_capabilities/RobotPoseRunner" });

  TestPlanningPlugin::CapabilityGenerationContext context_item;
  context_item.capability = &capability;
  context_item.resolution = resolution();

  const std::string prompt = plugin.buildGenerationPrompt(request(), { context_item }, {}, false);

  EXPECT_NE(prompt.find("implicit_prerequisites"), std::string::npos);
  EXPECT_NE(prompt.find("[nav2_capabilities/Nav2LaunchRunner]"), std::string::npos);
  EXPECT_NE(prompt.find("auto-started by capabilities2 backend dependency handling"), std::string::npos);
  EXPECT_EQ(prompt.find("[nav2_capabilities/WaypointRunner depends_on nav2_capabilities/Nav2LaunchRunner]"),
            std::string::npos);
  EXPECT_NE(prompt.find("[nav2_capabilities/WaypointRunner uses nav2_capabilities/RobotPoseRunner]"),
            std::string::npos);
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}