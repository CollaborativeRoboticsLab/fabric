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