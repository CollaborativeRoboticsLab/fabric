#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <cstdlib>

#include <fabric_base/core_base.hpp>
#include <fabric_experience_core_grag/graph/core_graphrag_utils.hpp>
#include <pluginlib/class_loader.hpp>
#include <rclcpp/rclcpp.hpp>

namespace
{
class StubLlmAnalysis : public fabric::LlmAnalysisBase
{
public:
  fabric::LlmPromptResult prompt(const fabric::LlmPromptRequest&) override { return {}; }
  fabric::LlmEmbeddingResult embed(const fabric::LlmEmbeddingRequest&) override { return {}; }
  fabric::LlmTokenizationResult tokenize(const fabric::LlmTokenizationRequest&) override { return {}; }
};

class TemporaryWorkingDirectory
{
public:
  TemporaryWorkingDirectory() : original_(std::filesystem::current_path())
  {
    char directory[] = "/tmp/fabric-knowledge-core-test-XXXXXX";
    const char* created = mkdtemp(directory);
    if (!created)
      throw std::runtime_error("Failed to create temporary core test directory");
    temporary_ = created;
    std::filesystem::current_path(temporary_);
  }

  ~TemporaryWorkingDirectory()
  {
    std::filesystem::current_path(original_);
    std::filesystem::remove_all(temporary_);
  }

private:
  std::filesystem::path original_;
  std::filesystem::path temporary_;
};
}  // namespace

TEST(KnowledgeCoreGraphRag, PluginDiscoveryAndParameterNamespace)
{
  rclcpp::init(0, nullptr);
  {
    pluginlib::ClassLoader<fabric::CoreBase> loader("fabric_base", "fabric::CoreBase");
    const auto classes = loader.getDeclaredClasses();
    EXPECT_NE(std::find(classes.begin(), classes.end(), "fabric::KnowledgeCoreGraphRag"), classes.end());
    EXPECT_EQ(std::find(classes.begin(), classes.end(), "fabric::CoreGraphRag"), classes.end());
    EXPECT_EQ(std::find(classes.begin(), classes.end(), "fabric::GraphKnowledgeCore"), classes.end());

    auto plugin = loader.createSharedInstance("fabric::KnowledgeCoreGraphRag");
    rclcpp::NodeOptions options;
    options.parameter_overrides({rclcpp::Parameter("experience.KnowledgeCoreGraphRag.edge.parameter.min_score", 0.42)});
    auto node = std::make_shared<rclcpp::Node>("knowledge_core_plugin_test", options);
    EXPECT_ANY_THROW(plugin->initialize(node));
    EXPECT_DOUBLE_EQ(node->get_parameter("experience.KnowledgeCoreGraphRag.edge.parameter.min_score").as_double(), 0.42);
    EXPECT_FALSE(node->has_parameter("experience.CoreGraphRag.edge.parameter.min_score"));
  }
  rclcpp::shutdown();
}

TEST(KnowledgeCoreGraphRag, InitializesInternalParametersUnderExperienceNamespace)
{
  rclcpp::init(0, nullptr);
  {
    TemporaryWorkingDirectory directory;
    pluginlib::ClassLoader<fabric::CoreBase> loader("fabric_base", "fabric::CoreBase");
    auto plugin = loader.createSharedInstance("fabric::KnowledgeCoreGraphRag");
    rclcpp::NodeOptions options;
    options.parameter_overrides({
        rclcpp::Parameter("experience.KnowledgeCoreGraphRag.embed.model_name", "test_embedding"),
        rclcpp::Parameter("experience.KnowledgeCoreGraphRag.embedding_dim", 12),
        rclcpp::Parameter("experience.KnowledgeCoreGraphRag.graph.num_clusters", 7)});
    auto node = std::make_shared<rclcpp::Node>("knowledge_core_internal_parameters_test", options);
    plugin->configureAnalysis(std::make_shared<StubLlmAnalysis>());
    ASSERT_NO_THROW(plugin->initialize(node));

    EXPECT_EQ(node->get_parameter("experience.KnowledgeCoreGraphRag.embed.model_name").as_string(), "test_embedding");
    EXPECT_EQ(node->get_parameter("experience.KnowledgeCoreGraphRag.embedding_dim").as_int(), 12);
    EXPECT_EQ(node->get_parameter("experience.KnowledgeCoreGraphRag.graph.num_clusters").as_int(), 7);
    EXPECT_FALSE(node->has_parameter("core_graphrag.embedding_dim"));
    EXPECT_FALSE(node->has_parameter("graph_rag.num_clusters"));
  }
  rclcpp::shutdown();
}

TEST(CoreGraphRagUtils, TagsDeclaredDependencyEdges)
{
  EXPECT_EQ(fabric::experience::graph::core_graphrag::declaredRelationEdgeType("depends_on"), "declared_dependency");
  EXPECT_EQ(fabric::experience::graph::core_graphrag::declaredRelationEdgeType("uses"), "declared_relation");
}

TEST(CoreGraphRagUtils, ClassifiesDeclaredDependencyAsDependency)
{
  fabric::experience::SemanticContextItem item;
  item.edge_type = "declared_dependency";
  item.relationship = "depends_on";

  EXPECT_EQ(fabric::experience::graph::core_graphrag::classifySemanticEdge(item), "dependency");
}

TEST(CoreGraphRagUtils, ClassifiesDeclaredRelationAsRelated)
{
  fabric::experience::SemanticContextItem item;
  item.edge_type = "declared_relation";
  item.relationship = "uses";

  EXPECT_EQ(fabric::experience::graph::core_graphrag::classifySemanticEdge(item), "related");
}

TEST(CoreGraphRagUtils, PreservesPatternAndFaultClassificationForDeclaredRelations)
{
  fabric::experience::SemanticContextItem pattern_item;
  pattern_item.edge_type = "declared_relation";
  pattern_item.relationship = "recovery";
  EXPECT_EQ(fabric::experience::graph::core_graphrag::classifySemanticEdge(pattern_item), "pattern");

  fabric::experience::SemanticContextItem fault_item;
  fault_item.edge_type = "declared_relation";
  fault_item.relationship = "failure";
  EXPECT_EQ(fabric::experience::graph::core_graphrag::classifySemanticEdge(fault_item), "fault");
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}