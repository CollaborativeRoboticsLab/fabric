#pragma once

#include <memory>
#include <string>
#include <optional>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/llm_analysis_base.hpp>
#include <fabric_base/utils/experience_structs.hpp>
#include <fabric_base/utils/metadata_structs.hpp>
#include <fabric_base/utils/plan_structs.hpp>
#include <fabric_base/utils/status_structs.hpp>

namespace fabric
{

/**
 * @brief Base class for core knowledge handling plugins.
 *
 * Implementations should reimplement methods that push new data to the storage.
 */
class CoreBase
{
public:
  virtual ~CoreBase() = default;

  /**
   * @brief Initialize the storage plugin with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   * @return true if initialization was successful, false otherwise.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "CoreBasePlugin");
  }

  virtual void configureAnalysis(const std::shared_ptr<LlmAnalysisBase>& analysis_plugin)
  {
    analysis_plugin_ = analysis_plugin;
  }

  /**
   * @brief Build the knowledge core from capability information
   *
   * @param info Vector containing capability information.
   */
  virtual void build(std::vector<fabric::capability::Descriptor>& info) = 0;

  /**
   * @brief Update the knowledge core with runtime supervisor evidence.
   *
   * @param status Internal system status record converted from supervisor telemetry.
   */
  virtual void update(const fabric::status::SystemStatus& status) = 0;

  /**
   * @brief Load the knowledge core from the storage and verify its integrity
   * against capability information.
   *
   * @param info Vector containing capability information.
   *
   * @return true if the graph was loaded successfully, false otherwise.
   */
  virtual void load(std::vector<fabric::capability::Descriptor>& info) = 0;

  /**
   * @brief Flush and release any owned resources before process shutdown.
   */
  virtual void shutdown()
  {
  }

  /**
   * @brief Resolve statistical evidence for a candidate skill and apply fallback selection.
   *
   * @param request Candidate skill identity and context.
   * @return CandidateSkillResolution Selected evidence summary and fallback trace.
   */
  virtual fabric::experience::CandidateSkillResolution resolveCandidateSkill(const fabric::experience::CandidateSkillRequest& request) = 0;

  /**
   * @brief Persist skill attribution for a candidate behaviour-plan node.
   *
   * @param attribution Candidate behaviour-plan node attribution record.
   * @return true if the record was stored successfully.
   */
  virtual bool recordCandidatePlanSkillAttribution(const fabric::experience::CandidatePlanSkillAttribution& attribution) = 0;

  /**
   * @brief Persist edge attribution for a candidate plan edge.
   *
   * @param attribution Candidate plan edge attribution record.
   * @return true if the record was stored successfully.
   */
  virtual bool recordCandidatePlanEdgeAttribution(const fabric::experience::CandidatePlanEdgeAttribution& attribution) = 0;

  /**
   * @brief Persist a selected-node score explanation artifact.
   *
   * @param record Candidate plan node score explanation record.
   * @return true if the record was stored successfully.
   */
  virtual bool recordPlanNodeScoreExplanation(const fabric::experience::PlanNodeScoreExplanationRecord& record) = 0;

  /**
   * @brief Read selected-node score explanations for a candidate plan.
   *
   * @param query Candidate plan identity and graph hash.
   * @return std::vector<PlanNodeScoreExplanationRecord> List of node explanations for the candidate plan.
   */
  virtual std::vector<fabric::experience::PlanNodeScoreExplanationRecord>
  readPlanNodeScoreExplanations(const fabric::experience::PlanNodeScoreExplanationQuery& query) = 0;

  /**
   * @brief Retrieve semantic context relevant to a task and optional skill.
   *
   * This is used to provide context for skill selection and fallback reasoning.
   *
   * @param task_description Natural-language task description.
   * @param skill_name Optional skill name to focus the context retrieval.
   * @param limit Maximum number of context items to return.
   * @return SemanticContextBundle Retrieved semantic context items and familiarity score.
   */
  virtual fabric::experience::SemanticContextBundle retrieveSemanticContext(const std::string& task_description, const std::string& skill_name = "",
                                                                            size_t limit = 8)
  {
    (void)task_description;
    (void)skill_name;
    (void)limit;
    return {};
  }

  /**
   * @brief Estimate how familiar a candidate skill is to the current task from semantic retrieval.
   * @param task_description Natural-language task description.
   * @param skill_name Optional skill name to focus the context retrieval.
   * @param limit Maximum number of context items to return.
   * @return double Familiarity score.
   */
  virtual double semanticFamiliarity(const std::string& task_description, const std::string& skill_name, size_t limit = 8)
  {
    return retrieveSemanticContext(task_description, skill_name, limit).familiarity;
  }

  /**
   * @brief Read one selected-node score explanation by full candidate-node identity.
   *
   * This is used to retrieve the explanation for a specific node in a candidate plan.
   *
   * @param query Candidate plan and node identity.
   * @return std::optional<PlanNodeScoreExplanationRecord> Explanation record if found, otherwise std::nullopt.
   */
  virtual std::optional<fabric::experience::PlanNodeScoreExplanationRecord>
  readPlanNodeScoreExplanation(const fabric::experience::PlanNodeScoreExplanationNodeQuery& query) = 0;

  /**
   * @brief Read selected plan feedback together with all node explanations for the same plan.
   *
   * This is used to retrieve the full explanation bundle for a selected candidate plan.
   *
   * @param query Candidate plan identity and graph hash.
   * @return std::optional<SelectedPlanScoreExplanationBundle> Explanation bundle if found, otherwise std::nullopt.
   */
  virtual std::optional<fabric::experience::SelectedPlanScoreExplanationBundle>
  readSelectedPlanScoreExplanationBundle(const fabric::experience::PlanNodeScoreExplanationQuery& query) = 0;

  /**
   * @brief Persist plan-level terminal feedback.
   *
   * This is used to record the outcome of a selected plan execution, including success/failure, runtime, energy, and other metrics.
   *
   * @param feedback Plan feedback record to persist.
   * @return true if the record was stored successfully.
   */
  virtual bool recordPlanFeedback(const fabric::experience::PlanFeedbackRecord& feedback) = 0;

  /**
   * @brief Persist or update a scoring epoch/version record.
   *
   * This is used to track the versions of scoring components used for plan evaluation, including calibration, normalization, estimation, and
   * weighting.
   *
   * @param record Scoring epoch/version record to persist.
   * @return true if the record was stored successfully.
   */
  virtual bool recordScoringEpoch(const fabric::experience::ScoringEpochRecord& record) = 0;

  /**
   * @brief Read a scoring epoch/version record by epoch ID.
   *
   * This is used to retrieve the details of a specific scoring epoch, including the versions of calibration, normalization, estimation, and weighting
   * components.
   *
   * @param epoch_id The unique identifier of the scoring epoch to retrieve.
   * @return std::optional<ScoringEpochRecord> The scoring epoch record if found, otherwise std::nullopt.
   * @throws std::runtime_error if there is an error accessing the storage or if the record cannot be retrieved.
   */
  virtual std::optional<fabric::experience::ScoringEpochRecord> readScoringEpoch(const std::string& epoch_id) = 0;

  /**
   * @brief Summarize recorded plan feedback for one scorer-version tuple.
   *
   * This is used to compute aggregate statistics on plan feedback, including success rates, runtime errors, energy errors, and other metrics.
   *
   * @param calibration_version The version of the calibration component.
   * @param normalizer_version The version of the normalization component.
   * @param estimator_version The version of the estimation component.
   * @param weight_version The version of the weighting component.
   * @return PlanFeedbackCalibrationSummary Summary of plan feedback for the specified scorer-version tuple.
   */
  virtual fabric::experience::PlanFeedbackCalibrationSummary summarizePlanFeedbackCalibration(const std::string& calibration_version,
                                                                                              const std::string& normalizer_version,
                                                                                              const std::string& estimator_version,
                                                                                              const std::string& weight_version) = 0;

  /**
   * @brief Summarize candidate exposure and selection for one scorer-version tuple.
   *
   * This is used to compute aggregate statistics on candidate exposure and selection, including bias metrics and other relevant indicators.
   *
   * @param calibration_version The version of the calibration component.
   * @param normalizer_version The version of the normalization component.
   * @param estimator_version The version of the estimation component.
   * @param weight_version The version of the weighting component.
   * @return PlanFeedbackSelectionBiasSummary Summary of candidate exposure and selection for the specified scorer-version tuple.
   */
  virtual fabric::experience::PlanFeedbackSelectionBiasSummary summarizePlanFeedbackSelectionBias(const std::string& calibration_version,
                                                                                                  const std::string& normalizer_version,
                                                                                                  const std::string& estimator_version,
                                                                                                  const std::string& weight_version) = 0;

protected:
  /**
   * @brief Base initialization method for derived classes.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name of the storage plugin.
   */
  void initialize_base(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name)
  {
    node_ = node;
    plugin_name_ = plugin_name;
  }

  /**
   * @brief Shared pointer to the ROS2 node.
   */
  rclcpp::Node::SharedPtr node_;

  /**
   * @brief Optional shared LLM-analysis plugin available to core implementations.
   */
  std::shared_ptr<LlmAnalysisBase> analysis_plugin_;

  /**
   * @brief Name of the storage plugin.
   */
  std::string plugin_name_;
};

}  // namespace fabric