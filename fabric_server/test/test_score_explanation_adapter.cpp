#include <gtest/gtest.h>

#include <fabric_server/utils/score_explanation_adapter.hpp>
#include <fabric_server/utils/score_services.hpp>

namespace
{
struct FeedbackCore
{
  std::optional<fabric::experience::SelectedPlanScoreExplanationBundle> existing;
  fabric::experience::PlanFeedbackRecord stored;
  int writes{0};
  bool store_success{true};

  auto readSelectedPlanScoreExplanationBundle(
    const fabric::experience::PlanNodeScoreExplanationQuery &)
  {
    return existing;
  }

  bool recordPlanFeedback(const fabric::experience::PlanFeedbackRecord & record)
  {
    ++writes;
    stored = record;
    return store_success;
  }

  bool recordCandidatePlanSkillAttribution(
    const fabric::experience::CandidatePlanSkillAttribution &)
  {
    return true;
  }

  bool recordCandidatePlanEdgeAttribution(const fabric::experience::CandidatePlanEdgeAttribution &)
  {
    return true;
  }
};
}  // namespace

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
  node_record.semantic_relevant_capabilities = {"demo/DetectObject", "demo/EstimatePose"};
  node_record.semantic_related_entities = {"apple", "table"};
  node_record.semantic_dependencies = {"camera_ready", "workspace_clear"};
  node_record.semantic_plan_patterns = {"detect_then_pick"};
  node_record.semantic_fault_concepts = {"occlusion"};
  node_record.semantic_community_summaries = {"vision pipeline summary"};
  node_record.estimator_outputs = {
    fabric::experience::EstimateResult{"success_probability", 0.8, true, "beta posterior"},
    fabric::experience::EstimateResult{"runtime_seconds", 4.2, true, "mean observed runtime"},
    fabric::experience::EstimateResult{"uncertainty", 0.1, true, "beta interval width"},
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
  const auto & node_msg = msg.node_explanations.front();
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

TEST(FeedbackPersistence, ExternalFeedbackPreservesScoringFields)
{
  FeedbackCore core;
  core.existing.emplace();
  core.existing->feedback.final_rapfs = 0.62;
  core.existing->feedback.expected_runtime_seconds = 4.2;
  fabric_msgs::srv::RecordPlanFeedback::Request request;
  request.planning_request_id = "request";
  request.candidate_plan_id = "candidate";
  request.graph_hash = "graph";
  request.terminal_outcome = "completed";
  request.terminal_success = true;
  fabric_msgs::srv::RecordPlanFeedback::Response response;
  fabric::server::score_services::recordPlanFeedback(core, request, response, 12, 34);
  EXPECT_TRUE(response.stored);
  EXPECT_TRUE(response.found_existing);
  EXPECT_EQ(core.writes, 1);
  EXPECT_DOUBLE_EQ(core.stored.final_rapfs, 0.62);
  EXPECT_DOUBLE_EQ(core.stored.expected_runtime_seconds, 4.2);
  EXPECT_EQ(core.stored.terminal_outcome, "completed");
}

TEST(FeedbackPersistence, RejectsIncompleteIdentityWithoutWriting)
{
  FeedbackCore core;
  fabric_msgs::srv::RecordPlanFeedback::Request request;
  fabric_msgs::srv::RecordPlanFeedback::Response response;
  fabric::server::score_services::recordPlanFeedback(core, request, response, 12, 34);
  EXPECT_FALSE(response.stored);
  EXPECT_EQ(core.writes, 0);
}

TEST(FeedbackPersistence, FailedTerminalWriteCanRetryAndSuccessIsFinalizedOnce)
{
  FeedbackCore core;
  core.store_success = false;
  fabric::planning::PendingGeneratedPlan pending;
  pending.planning_request_id = "request";
  pending.candidate_plan_id = "candidate";
  pending.graph_hash = "graph";
  std::vector<fabric::planning::PendingGeneratedPlan> pending_plans{pending};
  std::unordered_set<std::string> finalized;
  std::mutex state_mutex;
  fabric::status::SystemStatus status;
  status.plan_id = "runtime";
  status.planning_request_id = "request";
  status.candidate_plan_id = "candidate";
  status.graph_hash = "graph";
  status.capability_plan_state = 13;

  EXPECT_FALSE(fabric::server::feedback::finalizeTerminalPlanFeedback(
      core, state_mutex, pending_plans, finalized, status));
  EXPECT_EQ(pending_plans.size(), 1u);
  EXPECT_TRUE(finalized.empty());
  core.store_success = true;
  EXPECT_TRUE(fabric::server::feedback::finalizeTerminalPlanFeedback(
      core, state_mutex, pending_plans, finalized, status));
  EXPECT_TRUE(pending_plans.empty());
  EXPECT_EQ(finalized.count("runtime"), 1u);
  EXPECT_FALSE(fabric::server::feedback::finalizeTerminalPlanFeedback(
      core, state_mutex, pending_plans, finalized, status));
  EXPECT_EQ(core.writes, 2);
}

TEST(FeedbackPersistence, PersistsCapabilityTerminalOutcomes)
{
  struct Outcome
  {
    std::string label;
    int succeeded;
    int failed;
    int timed_out;
    int aborted;
  };
  const std::vector<Outcome> outcomes{
    {"completed", 1, 0, 0, 0},
    {"failed", 0, 1, 0, 0},
    {"timed_out", 0, 0, 1, 0},
    {"cancelled", 0, 0, 0, 1},
  };
  for (const auto & outcome : outcomes) {
    SCOPED_TRACE(outcome.label);
    FeedbackCore core;
    fabric::planning::PendingGeneratedPlan pending;
    pending.planning_request_id = "request";
    pending.candidate_plan_id = "candidate";
    pending.graph_hash = "graph";
    fabric::status::SystemStatus status;
    status.plan_id = "runtime";
    status.capability_plan_state = 13;
    fabric::status::CapabilityStatus capability;
    capability.has_run_evidence = true;
    capability.run_evidence.runs_completed = 1;
    capability.run_evidence.runs_succeeded = outcome.succeeded;
    capability.run_evidence.runs_failed = outcome.failed;
    capability.run_evidence.runs_timed_out = outcome.timed_out;
    capability.run_evidence.runs_aborted = outcome.aborted;
    status.capabilities.push_back(capability);
    EXPECT_TRUE(fabric::server::feedback::recordTerminalPlanFeedback(core, pending, status));
    EXPECT_EQ(core.stored.terminal_outcome, outcome.label);
    EXPECT_EQ(core.stored.terminal_success, outcome.label == "completed");
  }
}
