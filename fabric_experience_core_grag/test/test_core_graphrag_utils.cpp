#include <gtest/gtest.h>

#include <fabric_experience_core_grag/graph/core_graphrag_utils.hpp>

TEST(CoreGraphRagUtils, TagsDeclaredDependencyEdges)
{
  EXPECT_EQ(experience::graph::core_graphrag::declaredRelationEdgeType("depends_on"), "declared_dependency");
  EXPECT_EQ(experience::graph::core_graphrag::declaredRelationEdgeType("uses"), "declared_relation");
}

TEST(CoreGraphRagUtils, ClassifiesDeclaredDependencyAsDependency)
{
  fabric::experience::SemanticContextItem item;
  item.edge_type = "declared_dependency";
  item.relationship = "depends_on";

  EXPECT_EQ(experience::graph::core_graphrag::classifySemanticEdge(item), "dependency");
}

TEST(CoreGraphRagUtils, ClassifiesDeclaredRelationAsRelated)
{
  fabric::experience::SemanticContextItem item;
  item.edge_type = "declared_relation";
  item.relationship = "uses";

  EXPECT_EQ(experience::graph::core_graphrag::classifySemanticEdge(item), "related");
}

TEST(CoreGraphRagUtils, PreservesPatternAndFaultClassificationForDeclaredRelations)
{
  fabric::experience::SemanticContextItem pattern_item;
  pattern_item.edge_type = "declared_relation";
  pattern_item.relationship = "recovery";
  EXPECT_EQ(experience::graph::core_graphrag::classifySemanticEdge(pattern_item), "pattern");

  fabric::experience::SemanticContextItem fault_item;
  fault_item.edge_type = "declared_relation";
  fault_item.relationship = "failure";
  EXPECT_EQ(experience::graph::core_graphrag::classifySemanticEdge(fault_item), "fault");
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}