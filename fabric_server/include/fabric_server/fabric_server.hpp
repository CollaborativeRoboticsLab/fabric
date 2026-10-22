#pragma once

#include <memory>
#include <stdexcept>
#include <utility>

#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <fabric_server/fabric_runtime.hpp>
#include <fabric_server/utils/score_services.hpp>
#include <fabric_server/utils/status.hpp>
#include <fabric_msgs/action/generate_plan.hpp>
#include <fabric_msgs/msg/fabric_status.hpp>
#include <fabric_msgs/srv/cancel_fabric_plan.hpp>
#include <fabric_msgs/srv/complete_fabric.hpp>
#include <fabric_msgs/srv/get_plan_status.hpp>
#include <fabric_msgs/srv/parse_plan.hpp>
#include <fabric_msgs/srv/submit_fabric_plan.hpp>

namespace fabric
{
/**
 * @brief Adapt ROS 2 requests and runtime events to the Fabric execution runtime.
 *
 * Owns the ROS endpoints and runtime. Planning and execution are delegated to
 * FabricRuntime; ROS messages and action goal handles remain in this adapter.
 */
class Fabric : public rclcpp::Node
{
public:
  using SubmitFabricPlan = fabric_msgs::srv::SubmitFabricPlan;
  using CancelFabricPlan = fabric_msgs::srv::CancelFabricPlan;
  using CompleteFabric = fabric_msgs::srv::CompleteFabric;
  using GetPlanStatus = fabric_msgs::srv::GetPlanStatus;
  using ParsePlan = fabric_msgs::srv::ParsePlan;
  using GetScoringEpoch = fabric_msgs::srv::GetScoringEpoch;
  using GetPlanScoreExplanation = fabric_msgs::srv::GetPlanScoreExplanation;
  using RecordPlanFeedback = fabric_msgs::srv::RecordPlanFeedback;
  using RecordScoringEpoch = fabric_msgs::srv::RecordScoringEpoch;
  using ScorePlan = fabric_msgs::srv::ScorePlan;
  using GeneratePlan = fabric_msgs::action::GeneratePlan;
  using GoalHandleGeneratePlan = rclcpp_action::ServerGoalHandle<GeneratePlan>;

  /**
   * @brief Construct the node without starting its runtime or creating endpoints.
   * @param options ROS node configuration and parameter overrides.
   * @note Call initialize() after placing the node in a shared_ptr.
   */
  explicit Fabric(const rclcpp::NodeOptions& options = rclcpp::NodeOptions()) : Node("Fabric", options)
  {
  }

  /** @brief Stop the runtime before destroying ROS endpoints. */
  ~Fabric() override
  {
    shutdownRuntime();
  }

  /**
   * @brief Initialize the runtime and create the status publisher, services, and action.
   * @pre The node is owned by a shared_ptr and has not been initialized before.
   * @note Runtime readiness is asynchronous; an executor must service ROS dependencies.
   */
  void initialize()
  {
    this->declare_parameter<std::string>("telemetry.fabric_status_topic", "/fabric/status");
    fabric_status_pub_ = this->create_publisher<fabric_msgs::msg::FabricStatus>(this->get_parameter("telemetry.fabric_status_topic").as_string(),
                                                                                rclcpp::QoS(rclcpp::KeepLast(10)).transient_local().reliable());
    runtime_ = std::make_unique<FabricRuntime>(*this);
    runtime_->initialize([this](const fabric::plan::Plan& plan) { fabric_status_pub_->publish(fabric::server::status::toFabricStatusMsg(plan)); });
    createFabricServices();
    createFabricExperienceServices();
    createGeneratePlanAction();
  }

  /**
   * @brief Stop runtime work and join its workers while this node remains alive.
   * @note Safe to call repeatedly, including before initialize().
   */
  void shutdownRuntime()
  {
    if (runtime_)
    {
      runtime_->shutdownRuntime();
    }
  }

  /**
   * @brief Access the runtime for additional in-process adapters.
   * @return A reference valid only while this node and its runtime remain alive.
   * @throws std::runtime_error If initialize() has not created the runtime.
   */
  FabricRuntime& runtime()
  {
    if (!runtime_)
    {
      throw std::runtime_error("Fabric has not been initialized");
    }
    return *runtime_;
  }

private:
  /** @brief Register plan submission, cancellation, completion, status, and parsing services. */
  void createFabricServices()
  {
    submit_plan_server_ = this->create_service<SubmitFabricPlan>(
        "/fabric/plan/submit", std::bind(&Fabric::submitPlanCallback, this, std::placeholders::_1, std::placeholders::_2));
    cancel_server_ = this->create_service<CancelFabricPlan>(
        "/fabric/plan/cancel", std::bind(&Fabric::cancelPlanCallback, this, std::placeholders::_1, std::placeholders::_2));
    completion_server_ = this->create_service<CompleteFabric>(
        "/fabric/plan/set_completion", std::bind(&Fabric::setCompleteCallback, this, std::placeholders::_1, std::placeholders::_2));
    get_plan_status_server_ = this->create_service<GetPlanStatus>(
        "/fabric/plan/get_status", std::bind(&Fabric::getPlanStatusCallback, this, std::placeholders::_1, std::placeholders::_2));
    parse_plan_server_ = this->create_service<ParsePlan>("/fabric/plan/parse",
                                                         std::bind(&Fabric::parsePlanCallback, this, std::placeholders::_1, std::placeholders::_2));
  }

  /** @brief Register scoring, explanation, epoch, and external-feedback services. */
  void createFabricExperienceServices()
  {
    get_plan_score_explanation_service_ = this->create_service<GetPlanScoreExplanation>(
        "/fabric/experience/score_explanation",
        std::bind(&Fabric::handleGetPlanScoreExplanation, this, std::placeholders::_1, std::placeholders::_2));
    score_plan_service_ = this->create_service<ScorePlan>("/fabric/experience/score",
                                                          std::bind(&Fabric::handleScorePlan, this, std::placeholders::_1, std::placeholders::_2));
    get_scoring_epoch_service_ = this->create_service<GetScoringEpoch>(
        "/fabric/experience/get_scoring_epoch", std::bind(&Fabric::handleGetScoringEpoch, this, std::placeholders::_1, std::placeholders::_2));
    record_plan_feedback_service_ = this->create_service<RecordPlanFeedback>(
        "/fabric/experience/record_plan_feedback", std::bind(&Fabric::handleRecordPlanFeedback, this, std::placeholders::_1, std::placeholders::_2));
    record_scoring_epoch_service_ = this->create_service<RecordScoringEpoch>(
        "/fabric/experience/record_scoring_epoch", std::bind(&Fabric::handleRecordScoringEpoch, this, std::placeholders::_1, std::placeholders::_2));
  }

  /** @brief Register the plan-generation action and its goal lifecycle callbacks. */
  void createGeneratePlanAction()
  {
    generate_plan_server_ = rclcpp_action::create_server<GeneratePlan>(
        shared_from_this(), "/fabric/plan/generate", std::bind(&Fabric::handleGeneratePlanGoal, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Fabric::handleGeneratePlanCancel, this, std::placeholders::_1),
        std::bind(&Fabric::handleGeneratePlanAccepted, this, std::placeholders::_1));
  }

  /**
   * @brief Accept nonempty generation goals only when the runtime is ready.
   * @param goal Requested task and generation options.
   * @return Whether ROS should accept and execute the goal.
   */
  rclcpp_action::GoalResponse handleGeneratePlanGoal(const rclcpp_action::GoalUUID&, std::shared_ptr<const GeneratePlan::Goal> goal)
  {
    return runtime_->ready() && !goal->task.empty() ? rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE : rclcpp_action::GoalResponse::REJECT;
  }

  /** @brief Accept cancellation; the runtime observes it through the goal callback. */
  rclcpp_action::CancelResponse handleGeneratePlanCancel(const std::shared_ptr<GoalHandleGeneratePlan>)
  {
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  /**
   * @brief Convert an internal generation stage to a ROS FabricStatus code.
   * @param stage Runtime progress stage.
   * @return Corresponding status code, or UNKNOWN for an unrecognized stage.
   */
  static uint8_t generationStatusCode(GenerationStage stage)
  {
    using Status = fabric_msgs::msg::FabricStatus;
    switch (stage)
    {
      case GenerationStage::Requested:
        return Status::GENERATION_REQUESTED;
      case GenerationStage::InformationCollected:
        return Status::INFORMATION_COLLECTED;
      case GenerationStage::PlansScored:
        return Status::PLANS_SCORED;
      case GenerationStage::PlanSelected:
        return Status::PLAN_SELECTED;
      case GenerationStage::Complete:
        return Status::GENERATION_COMPLETE;
    }
    return Status::UNKNOWN;
  }

  /**
   * @brief Translate an accepted goal into runtime work and wire ROS feedback/results.
   * @param goal_handle Goal retained by callbacks until generation completes.
   * @note Runtime workers invoke progress and completion; no detached thread is created here.
   */
  void handleGeneratePlanAccepted(const std::shared_ptr<GoalHandleGeneratePlan> goal_handle)
  {
    const auto goal = goal_handle->get_goal();
    const auto context = this->get_node_base_interface()->get_context();
    GenerationCallbacks callbacks;
    callbacks.cancelled = [goal_handle]() { return goal_handle->is_canceling(); };
    callbacks.progress = [goal_handle, context](const GenerationProgress& progress) {
      if (rclcpp::ok(context) && goal_handle->is_active())
      {
        auto feedback = std::make_shared<GeneratePlan::Feedback>();
        feedback->status.code = generationStatusCode(progress.stage);
        feedback->status.plan_id = progress.plan_id;
        goal_handle->publish_feedback(feedback);
      }
    };
    callbacks.complete = [goal_handle, context, header = goal->header](const GenerationResult& generated) {
      if (!rclcpp::ok(context) || !goal_handle->is_active())
      {
        return;
      }
      auto result = std::make_shared<GeneratePlan::Result>();
      result->header = header;
      result->plan = generated.plan;
      result->reasoning = generated.reasoning;
      result->plan_id = generated.plan_id;
      if (goal_handle->is_canceling())
      {
        goal_handle->canceled(result);
      }
      else if (generated.state == GenerationResult::State::Succeeded)
      {
        goal_handle->succeed(result);
      }
      else
      {
        goal_handle->abort(result);
      }
    };
    try
    {
      if (runtime_->startGeneration(GenerationRequest{ goal->task, goal->uuid, goal->flush, goal->auto_queue }, callbacks))
      {
        return;
      }
    }
    catch (const std::exception& error)
    {
      RCLCPP_ERROR(this->get_logger(), "Unable to start generation: %s", error.what());
    }
    callbacks.complete(GenerationResult{});
  }

  /**
   * @brief Convert a submission to an internal plan and enqueue it through the runtime.
   * @param request Plan XML and optional generation metadata.
   * @param response Receives the execution plan ID or a submission error.
   */
  void submitPlanCallback(const std::shared_ptr<SubmitFabricPlan::Request> request, std::shared_ptr<SubmitFabricPlan::Response> response)
  {
    fabric::plan::Plan plan;
    plan.plan = request->plan;
    plan.metadata.planning_request_id = request->metadata.planning_request_id;
    plan.metadata.candidate_plan_id = request->metadata.candidate_plan_id;
    plan.metadata.graph_hash = request->metadata.graph_hash;
    try
    {
      response->plan_id = runtime_->submitPlan(std::move(plan));
      response->error.clear();
    }
    catch (const std::exception& error)
    {
      response->plan_id.clear();
      response->error = error.what();
    }
  }

  /**
   * @brief Convert parsed internal connections to ROS connection messages without dispatch.
   * @param request Plan XML to parse.
   * @param response Receives connections or a parsing error.
   */
  void parsePlanCallback(const std::shared_ptr<ParsePlan::Request> request, std::shared_ptr<ParsePlan::Response> response)
  {
    try
    {
      const auto plan = runtime_->parsePlan(request->plan);
      response->success = true;
      response->error.clear();
      for (const auto& entry : plan.connections)
      {
        const auto& connection = entry.second;
        fabric_msgs::msg::FabricConnection message;
        const auto convert = [](const fabric::plan::node& source, auto& target) {
          target.interface = source.interface;
          target.provider = source.provider;
          target.instance_id = source.instance_id;
        };
        convert(connection.source, message.source);
        convert(connection.on_start, message.on_start);
        convert(connection.on_stop, message.on_stop);
        convert(connection.on_success, message.on_success);
        convert(connection.on_failure, message.on_failure);
        message.description = connection.description;
        response->connections.push_back(std::move(message));
      }
    }
    catch (const std::exception& error)
    {
      response->success = false;
      response->error = error.what();
      response->connections.clear();
    }
  }

  /**
   * @brief Forward execution-plan cancellation to the runtime.
   * @param request Execution plan ID to cancel.
   * @param response Reports whether a current or queued plan matched.
   */
  void cancelPlanCallback(const std::shared_ptr<CancelFabricPlan::Request> request, std::shared_ptr<CancelFabricPlan::Response> response)
  {
    response->success = runtime_->cancelPlan(request->plan_id);
  }

  /**
   * @brief Forward a completion notification for the current execution plan.
   * @param request Plan ID reported as complete.
   */
  void setCompleteCallback(const std::shared_ptr<CompleteFabric::Request> request, std::shared_ptr<CompleteFabric::Response>)
  {
    if (!runtime_->completePlan(request->plan_id))
    {
      RCLCPP_WARN(this->get_logger(), "[server] Received plan id does not match the current plan id");
    }
  }

  /**
   * @brief Convert a runtime plan snapshot to a timestamped ROS status response.
   * @param request Plan or bond ID; both empty selects the current plan.
   * @param response Receives status and any applicable validation rejections.
   */
  void getPlanStatusCallback(const std::shared_ptr<GetPlanStatus::Request> request, std::shared_ptr<GetPlanStatus::Response> response)
  {
    response->header.stamp = this->now();
    const auto plan = runtime_->getPlanStatus(request->plan_id, request->bond_id);
    response->rejected_list.clear();
    if (!plan)
    {
      response->status.code = fabric_msgs::msg::FabricStatus::UNKNOWN;
      return;
    }
    response->status = fabric::server::status::toFabricStatusMsg(*plan);
    if (fabric::server::status::shouldExposeRejectedList(*plan))
    {
      response->rejected_list = plan->rejected_list;
    }
  }

  /**
   * @brief Serve stored internal score explanations as ROS messages.
   * @param request Candidate identity and optional node ID.
   * @param response Receives plan/node explanations or availability information.
   */
  void handleGetPlanScoreExplanation(const std::shared_ptr<GetPlanScoreExplanation::Request> request,
                                     std::shared_ptr<GetPlanScoreExplanation::Response> response)
  {
    namespace services = fabric::server::score_services;
    if (!runtime_->experienceReady())
    {
      services::setGetPlanScoreExplanationUnavailable(*response);
      return;
    }
    const auto bundle = runtime_->readScoreExplanation({ request->planning_request_id, request->candidate_plan_id, request->graph_hash });
    if (!bundle)
    {
      services::setGetPlanScoreExplanationNotFound(*response);
      return;
    }
    std::optional<fabric::experience::PlanNodeScoreExplanationRecord> node;
    if (!request->candidate_node_id.empty())
    {
      node = runtime_->readNodeScoreExplanation(
          { request->planning_request_id, request->candidate_plan_id, request->graph_hash, request->candidate_node_id });
    }
    services::populateGetPlanScoreExplanationResponse(*bundle, node, *response);
    if (!request->candidate_node_id.empty() && !node)
    {
      services::setGetPlanScoreExplanationNodeMissing(*response);
    }
  }

  /**
   * @brief Convert a score request and return runtime scoring results without execution.
   * @param request Task, plan XML, and optional candidate/node identities.
   * @param response Receives converted explanations or a scoring error.
   */
  void handleScorePlan(const std::shared_ptr<ScorePlan::Request> request, std::shared_ptr<ScorePlan::Response> response)
  {
    namespace services = fabric::server::score_services;
    if (!runtime_->experienceReady())
    {
      services::setScorePlanUnavailable(*response);
      return;
    }
    if (const auto error = services::validateScorePlanRequest(*request))
    {
      services::setScorePlanFailure(*response, *error);
      return;
    }
    try
    {
      const auto now = this->now();
      fabric::planning::Request scoring_request;
      scoring_request.planning_request_id =
          request->planning_request_id.empty() ? fabric::server::feedback::generatePlanningRequestId(now) : request->planning_request_id;
      scoring_request.task_description = request->task_description;
      scoring_request.recorded_at_sec = static_cast<int32_t>(now.seconds());
      scoring_request.recorded_at_nanosec = static_cast<uint32_t>(now.nanoseconds() % 1000000000ULL);
      const auto result = runtime_->scorePlan(scoring_request, request->candidate_plan_id, request->plan_xml);
      services::populateScorePlanResponse(fabric::server::feedback::buildScoreExplanationBundle(scoring_request, result), result,
                                          request->candidate_node_id, *response);
    }
    catch (const std::exception& error)
    {
      services::setScorePlanFailure(*response, error.what());
    }
  }

  /**
   * @brief Convert a stored scoring epoch and its feedback summaries to a ROS response.
   * @param request Epoch ID to retrieve.
   * @param response Receives the epoch or a missing/unavailable status.
   */
  void handleGetScoringEpoch(const std::shared_ptr<GetScoringEpoch::Request> request, std::shared_ptr<GetScoringEpoch::Response> response)
  {
    namespace services = fabric::server::score_services;
    if (!runtime_->experienceReady())
    {
      services::setGetScoringEpochUnavailable(*response);
      return;
    }
    if (request->epoch_id.empty())
    {
      services::setGetScoringEpochMissingId(*response);
      return;
    }
    const auto record = runtime_->readScoringEpoch(request->epoch_id);
    if (!record)
    {
      services::setGetScoringEpochNotFound(*response);
      return;
    }
    response->found = true;
    response->message = "scoring epoch loaded";
    response->epoch_id = record->epoch_id;
    response->strategy_name = record->strategy_name;
    response->calibration_version = record->calibration_version;
    response->normalizer_version = record->normalizer_version;
    response->estimator_version = record->estimator_version;
    response->weight_version = record->weight_version;
    response->uncertainty_weight_locked = record->uncertainty_weight_locked;
    response->calibration_summary = record->calibration_summary;
    response->selection_bias_summary = record->selection_bias_summary;
    response->active = record->active;
    response->start_sec = record->start_sec;
    response->start_nanosec = record->start_nanosec;
    response->end_sec = record->end_sec;
    response->end_nanosec = record->end_nanosec;
    response->recorded_at_sec = record->recorded_at_sec;
    response->recorded_at_nanosec = record->recorded_at_nanosec;
    response->provenance = record->provenance;
  }

  /**
   * @brief Convert an epoch update while retaining unspecified stored fields.
   * @param request Epoch identity and metadata updates.
   * @param response Reports whether the epoch existed and was stored.
   */
  void handleRecordScoringEpoch(const std::shared_ptr<RecordScoringEpoch::Request> request, std::shared_ptr<RecordScoringEpoch::Response> response)
  {
    namespace services = fabric::server::score_services;
    if (!runtime_->experienceReady())
    {
      services::setRecordScoringEpochUnavailable(*response);
      return;
    }
    if (request->epoch_id.empty())
    {
      services::setRecordScoringEpochMissingId(*response);
      return;
    }
    const auto existing = runtime_->readRawScoringEpoch(request->epoch_id);
    const auto now = this->now();
    const auto record = services::buildScoringEpochRecord(existing, *request, static_cast<int32_t>(now.seconds()),
                                                          static_cast<uint32_t>(now.nanoseconds() % 1000000000ULL));
    services::populateRecordScoringEpochResponse(existing.has_value(), runtime_->recordScoringEpoch(record), *response);
  }

  /**
   * @brief Convert external plan feedback while retaining prior score and runtime fields.
   * @param request Candidate identity, observed outcome, and optional feedback fields.
   * @param response Reports whether prior feedback existed and the update was stored.
   */
  void handleRecordPlanFeedback(const std::shared_ptr<RecordPlanFeedback::Request> request, std::shared_ptr<RecordPlanFeedback::Response> response)
  {
    namespace services = fabric::server::score_services;
    if (!runtime_->experienceReady())
    {
      services::setRecordPlanFeedbackUnavailable(*response);
      return;
    }
    if (const auto error = services::validateRecordPlanFeedbackRequest(*request))
    {
      response->stored = false;
      response->found_existing = false;
      response->message = *error;
      return;
    }
    const auto existing = runtime_->readScoreExplanation(services::buildPlanFeedbackQuery(*request));
    const auto now = this->now();
    const auto record = services::buildPlanFeedbackRecord(existing, *request, static_cast<int32_t>(now.seconds()),
                                                          static_cast<uint32_t>(now.nanoseconds() % 1000000000ULL));
    services::populateRecordPlanFeedbackResponse(existing.has_value(), runtime_->recordPlanFeedback(record), *response);
  }

  rclcpp::Publisher<fabric_msgs::msg::FabricStatus>::SharedPtr fabric_status_pub_;
  rclcpp::Service<SubmitFabricPlan>::SharedPtr submit_plan_server_;
  rclcpp::Service<CancelFabricPlan>::SharedPtr cancel_server_;
  rclcpp::Service<CompleteFabric>::SharedPtr completion_server_;
  rclcpp::Service<GetPlanStatus>::SharedPtr get_plan_status_server_;
  rclcpp::Service<ParsePlan>::SharedPtr parse_plan_server_;
  rclcpp::Service<GetPlanScoreExplanation>::SharedPtr get_plan_score_explanation_service_;
  rclcpp::Service<ScorePlan>::SharedPtr score_plan_service_;
  rclcpp::Service<GetScoringEpoch>::SharedPtr get_scoring_epoch_service_;
  rclcpp::Service<RecordPlanFeedback>::SharedPtr record_plan_feedback_service_;
  rclcpp::Service<RecordScoringEpoch>::SharedPtr record_scoring_epoch_service_;
  rclcpp_action::Server<GeneratePlan>::SharedPtr generate_plan_server_;
  std::unique_ptr<FabricRuntime> runtime_;
};
}  // namespace fabric
