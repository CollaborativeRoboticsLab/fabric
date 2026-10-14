#pragma once

#include <any>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/utils/metadata_structs.hpp>
#include <fabric_base/utils/planning_structs.hpp>
#include <fabric_base/utils/plan_structs.hpp>
#include <fabric_base/utils/exception.hpp>
#include <fabric_base/utils/xml_helper.hpp>

namespace fabric
{

/**
 * @brief Abstract base class for XML plan validation plugins.
 *
 * Implementations should provide a validate() method that checks the XML plan for errors.
 */
class ValidationBase
{
public:
  virtual ~ValidationBase() = default;

  /**
   * @brief Initialize the validation plugin with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "ValidationBasePlugin");
  }

  /**
   * @brief Validate the given XML plan.
   *
    * @param plan The fabric::plan::Plan to validate.
    * @param capabilities Support data for evaluation as a vector of fabric::capability::Info.
   */
    virtual void validate(fabric::plan::Plan& plan, std::vector<fabric::capability::Info>& capabilities) = 0;

  /**
   * @brief Validate one parsed candidate plan in place.
   *
   * Experience-derived validators can override this when operating on candidate plans
   * instead of the lower-level XML graph representation.
   *
   * @param request The planning request.
   * @param capabilities The available capabilities.
   * @param candidate The candidate plan to validate.
   */
  virtual void validate(const fabric::planning::Request& request, const std::vector<fabric::capability::Descriptor>& capabilities,
                        fabric::planning::CandidatePlan& candidate) const
  {
    (void)request;
    (void)capabilities;
    (void)candidate;
  }

protected:
  /**
   * @brief Base initialization method for derived classes.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name of the validation plugin.
   */
  void initialize_base(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name)
  {
    node_ = node;
    plugin_name_ = plugin_name;
  }

  /**
   * @brief Add a rejection finding to the candidate plan.
   *
   * @param candidate The candidate plan to update.
   * @param message The rejection message.
   * @param candidate_node_id Optional ID of the candidate node related to the finding.
   * @param parameter_name Optional name of the parameter related to the finding.
   * @param source_node_id Optional ID of the source node related to the finding.
   * @param target_node_id Optional ID of the target node related to the finding.
   */
  void addReject(fabric::planning::CandidatePlan& candidate, const std::string& message, const std::string& candidate_node_id = {},
                 const std::string& parameter_name = {}, const std::string& source_node_id = {}, const std::string& target_node_id = {}) const
  {
    fabric::planning::CandidateValidationFinding finding;
    finding.plugin_name = plugin_name_;
    finding.level = fabric::planning::ValidationFindingLevel::Reject;
    finding.candidate_node_id = candidate_node_id;
    finding.source_node_id = source_node_id;
    finding.target_node_id = target_node_id;
    finding.parameter_name = parameter_name;
    finding.message = message;
    candidate.validation.findings.push_back(finding);
    candidate.validation.rejected = true;
    if (candidate.validation.summary.empty())
      candidate.validation.summary = message;
  }

  /**
   * @brief Add a warning finding to the candidate plan.
   *
   * @param candidate The candidate plan to update.
   * @param message The warning message.
   * @param candidate_node_id Optional ID of the candidate node related to the finding.
   * @param parameter_name Optional name of the parameter related to the finding.
   * @param source_node_id Optional ID of the source node related to the finding.
   * @param target_node_id Optional ID of the target node related to the finding.
   */
  void addWarning(fabric::planning::CandidatePlan& candidate, const std::string& message, const std::string& candidate_node_id = {},
                  const std::string& parameter_name = {}, const std::string& source_node_id = {}, const std::string& target_node_id = {}) const
  {
    fabric::planning::CandidateValidationFinding finding;
    finding.plugin_name = plugin_name_;
    finding.level = fabric::planning::ValidationFindingLevel::Warning;
    finding.candidate_node_id = candidate_node_id;
    finding.source_node_id = source_node_id;
    finding.target_node_id = target_node_id;
    finding.parameter_name = parameter_name;
    finding.message = message;
    candidate.validation.findings.push_back(finding);
  }

  /**
   * @brief Add a penalty finding to the candidate plan.
   *
   * @param candidate The candidate plan to update.
   * @param penalty The penalty value.
   * @param message The penalty message.
   * @param candidate_node_id Optional ID of the candidate node related to the finding.
   * @param parameter_name Optional name of the parameter related to the finding.
   * @param source_node_id Optional ID of the source node related to the finding.
   * @param target_node_id Optional ID of the target node related to the finding.
   */
  void addPenalty(fabric::planning::CandidatePlan& candidate, double penalty, const std::string& message, const std::string& candidate_node_id = {},
                  const std::string& parameter_name = {}, const std::string& source_node_id = {}, const std::string& target_node_id = {}) const
  {
    fabric::planning::CandidateValidationFinding finding;
    finding.plugin_name = plugin_name_;
    finding.level = fabric::planning::ValidationFindingLevel::Penalty;
    finding.candidate_node_id = candidate_node_id;
    finding.source_node_id = source_node_id;
    finding.target_node_id = target_node_id;
    finding.parameter_name = parameter_name;
    finding.message = message;
    finding.penalty = penalty;
    candidate.validation.findings.push_back(finding);
    candidate.validation.total_penalty += penalty;
  }

  /**
   * @brief Shared pointer to the ROS2 node.
   */
  rclcpp::Node::SharedPtr node_;

  /**
   * @brief Name of the validation plugin.
   */
  std::string plugin_name_;
};

}  // namespace fabric
