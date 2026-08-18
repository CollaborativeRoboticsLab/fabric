#pragma once
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <fabric_base/generation_base.hpp>
#include <experience_msgs/action/generate_plan.hpp>

namespace fabric
{
/**
 * @brief Syntax validation plugin for XML plans.
 *
 * This plugin checks the XML plan for basic syntax errors (well-formedness, root tag, etc).
 */
class ExperienceGenerator : public GenerationBase
{
  using GeneratePlan = experience_msgs::action::GeneratePlan;
  using GoalHandleGeneratePlan = rclcpp_action::ClientGoalHandle<GeneratePlan>;

public:
  ExperienceGenerator() = default;
  virtual ~ExperienceGenerator() = default;

  /**
   * @brief Initialize the XML parser plugin.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "ExperienceGeneratorPlugin");

    experience_action_client_ = rclcpp_action::create_client<GeneratePlan>(node_, "experience/plan/generate");

    if (!experience_action_client_->wait_for_action_server(std::chrono::seconds(10)))
    {
      RCLCPP_ERROR(node_->get_logger(), "Experience action server not available after waiting at experience/plan/generate");
      throw fabric::fabric_exception("Experience action server not available after waiting");
    }
  }

  /**
   * @brief Parse the given XML plan.
   *
   * @param document The XMLDocument representing the plan.
   * @param plan The parsed plan.
   */
  fabric::Plan generate(const std::string& task, const std::string& uuid, bool flush) override
  {
    auto goal_msg = GeneratePlan::Goal();
    goal_msg.task_description = task;
    goal_msg.use_optimiztion = false;
    goal_msg.uuid = uuid;
    goal_msg.flush = flush;

    std::mutex block_mutex;
    std::condition_variable cv;
    bool completed = false;
    std::unique_lock<std::mutex> lock(block_mutex);

    rclcpp_action::Client<GeneratePlan>::SendGoalOptions send_goal_options_;

    // trigger the action client with goal
    send_goal_options_.goal_response_callback = [this](const rclcpp_action::ClientGoalHandle<GeneratePlan>::SharedPtr& goal_handle) {
      // store goal handle to be used with stop funtion
      goal_handle_ = goal_handle;
    };

    send_goal_options_.feedback_callback = [this](rclcpp_action::ClientGoalHandle<GeneratePlan>::SharedPtr goal_handle,
                                                  const GeneratePlan::Feedback::ConstSharedPtr feedback_msg) {
      (void)goal_handle;

      RCLCPP_INFO(node_->get_logger(), "received feedback status: %u", feedback_msg->status.code);
    };

    send_goal_options_.result_callback = [this, &completed, &cv](const rclcpp_action::ClientGoalHandle<GeneratePlan>::WrappedResult& wrapped_result) {
            if (wrapped_result.code == rclcpp_action::ResultCode::SUCCEEDED)
      {
        RCLCPP_INFO(node_->get_logger(), "Generation succeeded");
      }
      else
      {
        RCLCPP_ERROR(node_->get_logger(), "Generation failed");
      }

      result_ = wrapped_result.result;
      completed = true;
      cv.notify_all();
    };

    experience_action_client_->async_send_goal(goal_msg, send_goal_options_);
    RCLCPP_INFO(node_->get_logger(), "Generation request sent. Waiting for acceptance.");

    // Conditional wait
    cv.wait(lock, [&completed] { return completed; });
    RCLCPP_INFO(node_->get_logger(), "Generation completed. Result received.");

    fabric::Plan plan;
    plan.plan = result_->plan;
    plan.reasoning = result_->reasoning;
    plan.metadata.planning_request_id = result_->planning_request_id;
    plan.metadata.candidate_plan_id = result_->candidate_plan_id;
    plan.metadata.graph_hash = result_->graph_hash;
    return plan;
  }

protected:
  /**
   * @brief Shared pointer to the experience action client.
   */
  rclcpp_action::Client<GeneratePlan>::SharedPtr experience_action_client_;

  /**
   * @brief goal handle parameter to capture goal response from goal_response_callback
   */
  rclcpp_action::ClientGoalHandle<GeneratePlan>::SharedPtr goal_handle_;

  /**
   * @brief Shared pointer the result.
   */
  GeneratePlan::Result::SharedPtr result_;
};
}  // namespace fabric
