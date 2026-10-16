#include <gtest/gtest.h>

#include <fabric_server/utils/score_explanation_adapter.hpp>

TEST(ScoreExplanationAdapter, PreservesSemanticExplanationFields)
{
  fabric::experience::PlanNodeScoreExplanationRecord node_record;
  node_record.planning_request_id = "req-1";
  node_record.candidate_plan_id = "plan-1";
  node_record.graph_hash = "graph-1";
  node_record.candidate_node_id = "runner_1";
  node_record.skill_name = "demo/DetectObject";
  node_record.skill_id = "demo_detectobject";
  node_record.requested_context_key = "task:pick_apple";
  node_record.requested_context_class = "task:pick";
  node_record.selected_skill_id = "demo_detectobject";
  node_record.selected_key = "task:pick_apple";
  node_record.estimator_window = "empirical";
  node_record.fallback_level = "context_key";
  node_record.proxy_source = "requested_skill";
  node_record.reason = "matched semantic context";
  node_record.semantic_familiarity = 0.75;
  node_record.semantic_relevant_capabilities = { "demo/DetectObject", "demo/EstimatePose" };
  node_record.semantic_related_entities = { "apple", "table" };
  node_record.semantic_dependencies = { "camera_ready", "workspace_clear" };
  node_record.semantic_plan_patterns = { "detect_then_pick" };
  node_record.semantic_fault_concepts = { "occlusion" };
  node_record.semantic_community_summaries = { "vision pipeline summary" };
  node_record.estimator_outputs = {
    fabric::experience::EstimateResult{ "success_probability", 0.8, true, "beta posterior" },
    fabric::experience::EstimateResult{ "runtime_seconds", 4.2, true, "mean observed runtime" },
    fabric::experience::EstimateResult{ "uncertainty", 0.1, true, "beta interval width" },
  };

  fabric::experience::SelectedPlanScoreExplanationBundle bundle;
  bundle.feedback.planning_request_id = "req-1";
  bundle.feedback.candidate_plan_id = "plan-1";
  bundle.feedback.graph_hash = "graph-1";
  bundle.feedback.selected = true;
  bundle.feedback.scoring_supported = true;
  bundle.feedback.feasible = true;
  bundle.feedback.sequential_graph = true;
  bundle.feedback.terminal_success_estimate = 0.8;
  bundle.feedback.expected_runtime_seconds = 4.2;
  bundle.feedback.expected_uncertainty = 0.1;
  bundle.feedback.final_rapfs = 0.62;
  bundle.node_explanations.push_back(node_record);

  const auto msg = fabric::server::adapter::toPlanScoreExplanationMsg(bundle);

  ASSERT_EQ(msg.node_explanations.size(), 1u);
  const auto& node_msg = msg.node_explanations.front();
  EXPECT_EQ(node_msg.semantic_relevant_capabilities.size(), 2u);
  EXPECT_EQ(node_msg.semantic_related_entities.size(), 2u);
  EXPECT_EQ(node_msg.semantic_dependencies.size(), 2u);
  EXPECT_EQ(node_msg.semantic_plan_patterns.size(), 1u);
  EXPECT_EQ(node_msg.semantic_fault_concepts.size(), 1u);
  EXPECT_EQ(node_msg.semantic_community_summaries.size(), 1u);
  EXPECT_EQ(node_msg.semantic_community_summaries.front(), "vision pipeline summary");
  EXPECT_DOUBLE_EQ(node_msg.semantic_familiarity, 0.75);
  EXPECT_EQ(node_msg.estimator_outputs.size(), 3u);
  EXPECT_TRUE(node_msg.success_probability_available);
  EXPECT_TRUE(node_msg.uncertainty_available);
  EXPECT_DOUBLE_EQ(node_msg.feature_vector[0], 0.8);
  EXPECT_DOUBLE_EQ(node_msg.feature_vector[1], 4.2);
  EXPECT_DOUBLE_EQ(node_msg.feature_vector[5], 0.1);
}