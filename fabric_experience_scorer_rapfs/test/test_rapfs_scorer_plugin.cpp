#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include <fabric_experience_scorer_rapfs/rapfs_scorer_plugin.hpp>

namespace
{
class TestRapfsScorerPlugin : public fabric::RapfsScorerPlugin
{
public:
  using fabric::RapfsScorerPlugin::adaptEvidenceForEstimator;
  using fabric::RapfsScorerPlugin::annotateOutcomePolicyReason;
  using fabric::RapfsScorerPlugin::rejectedValidationScore;
  using fabric::RapfsScorerPlugin::serializeRejectedScoreComponents;
};

std::shared_ptr<rclcpp::Node> testNode(const std::string& name)
{
  return std::make_shared<rclcpp::Node>(name);
}
}  // namespace

TEST(RapfsScorerPlugin, CountsNonSuccessTerminalsAsFailureForSuccessEstimator)
{
  TestRapfsScorerPlugin plugin;
  plugin.initialize(testNode("rapfs_policy_adaptation_test"));

  fabric::experience::EstimatorEvidence evidence;
  evidence.runs_completed = 3;
  evidence.runs_failed = 1;
  evidence.runs_aborted = 2;
  evidence.runs_timed_out = 1;

  const auto adapted = plugin.adaptEvidenceForEstimator("SuccessProbabilityPlugin", evidence);

  EXPECT_EQ(adapted.runs_completed, 6);
  EXPECT_EQ(adapted.runs_failed, 4);
  EXPECT_EQ(adapted.runs_aborted, 2);
  EXPECT_EQ(adapted.runs_timed_out, 1);
}

TEST(RapfsScorerPlugin, LeavesOtherEstimatorEvidenceUntouched)
{
  TestRapfsScorerPlugin plugin;
  plugin.initialize(testNode("rapfs_non_target_estimator_test"));

  fabric::experience::EstimatorEvidence evidence;
  evidence.runs_completed = 5;
  evidence.runs_failed = 2;
  evidence.runs_aborted = 1;
  evidence.runs_timed_out = 1;

  const auto adapted = plugin.adaptEvidenceForEstimator("RuntimePlugin", evidence);

  EXPECT_EQ(adapted.runs_completed, evidence.runs_completed);
  EXPECT_EQ(adapted.runs_failed, evidence.runs_failed);
  EXPECT_EQ(adapted.runs_aborted, evidence.runs_aborted);
  EXPECT_EQ(adapted.runs_timed_out, evidence.runs_timed_out);
}

TEST(RapfsScorerPlugin, CountsNonSuccessTerminalsAsFailureForUncertaintyEstimator)
{
  TestRapfsScorerPlugin plugin;
  plugin.initialize(testNode("rapfs_uncertainty_policy_adaptation_test"));

  fabric::experience::EstimatorEvidence evidence;
  evidence.runs_completed = 2;
  evidence.runs_failed = 1;
  evidence.runs_aborted = 1;
  evidence.runs_timed_out = 2;

  const auto adapted = plugin.adaptEvidenceForEstimator("UncertaintyPlugin", evidence);

  EXPECT_EQ(adapted.runs_completed, 5);
  EXPECT_EQ(adapted.runs_failed, 4);
  EXPECT_EQ(adapted.runs_aborted, 1);
  EXPECT_EQ(adapted.runs_timed_out, 2);
}

TEST(RapfsScorerPlugin, AnnotatesOutcomePolicyReasonForSuccessEstimator)
{
  TestRapfsScorerPlugin plugin;
  plugin.initialize(testNode("rapfs_reason_annotation_test"));

  fabric::experience::EstimateResult output;
  output.reason = "beta-smoothed posterior mean";
  plugin.annotateOutcomePolicyReason("SuccessProbabilityPlugin", output);

  EXPECT_NE(output.reason.find("non-success terminal policy=count_as_failure"), std::string::npos);
}

TEST(RapfsScorerPlugin, AnnotatesOutcomePolicyReasonForUncertaintyEstimator)
{
  TestRapfsScorerPlugin plugin;
  plugin.initialize(testNode("rapfs_uncertainty_reason_annotation_test"));

  fabric::experience::EstimateResult output;
  output.reason = "beta posterior interval width";
  plugin.annotateOutcomePolicyReason("UncertaintyPlugin", output);

  EXPECT_NE(output.reason.find("non-success terminal policy=count_as_failure"), std::string::npos);
}

TEST(RapfsScorerPlugin, RejectedScoreComponentsStayJsonSafe)
{
  TestRapfsScorerPlugin plugin;
  plugin.initialize(testNode("rapfs_json_safe_rejected_score_test"));

  fabric::planning::CandidatePlan candidate;
  candidate.validation.rejected = true;
  candidate.validation.summary = "validator rejected plan";
  candidate.validation.total_penalty = 1.25;
  candidate.score = plugin.rejectedValidationScore(candidate);

  const std::string json = plugin.serializeRejectedScoreComponents(candidate);

  EXPECT_NE(json.find("\"validation_rejected\":true"), std::string::npos);
  EXPECT_NE(json.find("\"raw_score\":null"), std::string::npos);
  EXPECT_NE(json.find("\"final_score\":null"), std::string::npos);
  EXPECT_EQ(json.find("-inf"), std::string::npos);
}

int main(int argc, char** argv)
{
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}