#include <future>

#include <gtest/gtest.h>

#include <fabric_parser_xml/xml_parser_plugin.hpp>

class InternalParsingTest : public ::testing::Test
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
    node_ = std::make_shared<rclcpp::Node>("internal_parsing_test");
    parser_.initialize(node_);
  }

  rclcpp::Node::SharedPtr node_;
  fabric::XMLParser parser_;
};

TEST_F(InternalParsingTest, ParsesConnectionsWithoutAService)
{
  const auto result = parser_.parsePlanDefinition(
    "<Plan><Control type=\"sequential\">"
    "<Runner interface=\"demo/First\" provider=\"demo/FirstProvider\"/>"
    "<Runner interface=\"demo/Second\" provider=\"demo/SecondProvider\"/>"
    "</Control></Plan>");
  ASSERT_TRUE(result.success) << result.error;
  ASSERT_GE(result.connections.size(), 2u);
  bool found_transition = false;
  for (const auto & connection : result.connections) {
    if (connection.source.interface == "demo/First") {
      EXPECT_EQ(connection.source.provider, "demo/FirstProvider");
      EXPECT_EQ(connection.on_success.interface, "demo/Second");
      EXPECT_EQ(connection.on_success.provider, "demo/SecondProvider");
      found_transition = true;
    }
  }
  EXPECT_TRUE(found_transition);
}

TEST_F(InternalParsingTest, ReturnsErrorsForMalformedOrInvalidPlans)
{
  for (const auto & xml : {"", "<Plan>", "<Other/>",
      "<Plan><Runner interface=\"demo/First\"/></Plan>"})
  {
    const auto result = parser_.parsePlanDefinition(xml);
    EXPECT_FALSE(result.success) << xml;
    EXPECT_FALSE(result.error.empty()) << xml;
    EXPECT_TRUE(result.connections.empty()) << xml;
  }
}

TEST_F(InternalParsingTest, KeepsConsecutivePlansIndependent)
{
  ASSERT_TRUE(parser_.parsePlanDefinition(
    "<Plan><Runner interface=\"demo/First\" provider=\"demo/FirstProvider\"/></Plan>").success);
  const auto result = parser_.parsePlanDefinition(
    "<Plan><Runner interface=\"demo/Second\" provider=\"demo/SecondProvider\"/></Plan>");
  ASSERT_TRUE(result.success) << result.error;
  for (const auto & connection : result.connections) {
    EXPECT_NE(connection.source.interface, "demo/First");
  }
}

TEST_F(InternalParsingTest, PreservesTypedRunnerParameters)
{
  auto result = parser_.parsePlanDefinition(
    "<Plan><Runner interface=\"demo/Move\" provider=\"demo/Provider\" "
    "speed=\"0.5\" attempts=\"3\" enabled=\"true\" target=\"dock\"/></Plan>");
  ASSERT_TRUE(result.success) << result.error;
  bool found_runner = false;
  for (auto & connection : result.connections) {
    if (connection.source.interface == "demo/Move") {
      auto & parameters = connection.source.parameters;
      EXPECT_DOUBLE_EQ(std::any_cast<double>(parameters.get_value("speed", 0.0)), 0.5);
      EXPECT_EQ(std::any_cast<int>(parameters.get_value("attempts", 0)), 3);
      EXPECT_TRUE(std::any_cast<bool>(parameters.get_value("enabled", false)));
      EXPECT_EQ(std::any_cast<std::string>(parameters.get_value("target", std::string())), "dock");
      found_runner = true;
    }
  }
  EXPECT_TRUE(found_runner);
}

TEST_F(InternalParsingTest, SerializesPlanningAndDispatchParsing)
{
  auto planning = std::async(std::launch::async, [this]() {
        for (int attempt = 0; attempt < 5; ++attempt) {
          const auto result = parser_.parsePlanDefinition(
          "<Plan><Runner interface=\"demo/Planning\" provider=\"demo/Provider\"/></Plan>");
          if (!result.success || result.connections.empty()) {
            return false;
          }
          for (const auto & connection : result.connections) {
            if (connection.source.interface == "demo/Dispatch") {
              return false;
            }
          }
        }
        return true;
    });
  auto dispatch = std::async(std::launch::async, [this]() {
        for (int attempt = 0; attempt < 5; ++attempt) {
          fabric::plan::Plan plan;
          plan.plan_id = "dispatch-plan";
          plan.plan =
          "<Plan><Runner interface=\"demo/Dispatch\" provider=\"demo/Provider\"/></Plan>";
          parser_.parsePlan(plan);
          if (plan.connections.empty()) {
            return false;
          }
          for (const auto & entry : plan.connections) {
            if (entry.second.source.interface == "demo/Planning") {
              return false;
            }
          }
        }
        return true;
    });
  EXPECT_TRUE(planning.get());
  EXPECT_TRUE(dispatch.get());
}
