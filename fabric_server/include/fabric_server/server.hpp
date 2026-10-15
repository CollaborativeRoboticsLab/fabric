#pragma once

#include <string>
#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <iomanip>
#include <unordered_map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <deque>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <unordered_set>
#include <map>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <tinyxml2.h>
#include <uuid/uuid.h>
#include <rclcpp/rclcpp.hpp>
#include "rclcpp_action/rclcpp_action.hpp"
#include <pluginlib/class_loader.hpp>

#include <fabric_base/utils/metadata_structs.hpp>
#include <fabric_base/utils/plan_structs.hpp>
#include <fabric_base/utils/planning_structs.hpp>
#include <fabric_base/utils/xml_helper.hpp>

#include <fabric_base/validation_base.hpp>
#include <fabric_base/parser_base.hpp>
#include <fabric_base/generation_base.hpp>
#include <fabric_base/behaviour_interface_base.hpp>
#include <fabric_base/metadata_base.hpp>
#include <fabric_base/planner_base.hpp>
#include <fabric_base/scorer_base.hpp>
#include <fabric_base/validation_base.hpp>

#include <fabric_server/capability_client.hpp>
#include <fabric_server/bond_client.hpp>

#include <fabric_msgs/srv/submit_fabric_plan.hpp>
#include <fabric_msgs/srv/cancel_fabric_plan.hpp>
#include <fabric_msgs/srv/complete_fabric.hpp>
#include <fabric_msgs/srv/get_plan_status.hpp>
#include <fabric_msgs/srv/parse_plan.hpp>
#include <fabric_msgs/srv/get_scoring_epoch.hpp>
#include <fabric_msgs/srv/get_plan_score_explanation.hpp>
#include <fabric_msgs/srv/record_plan_feedback.hpp>
#include <fabric_msgs/srv/record_scoring_epoch.hpp>
#include <fabric_msgs/srv/score_plan.hpp>

#include <fabric_msgs/msg/fabric_status.hpp>

#include <supervisor_msgs/msg/system_status.hpp>

#include <fabric_msgs/action/generate_plan.hpp>

namespace fabric
{

/**
 * @brief Class representing the main server of the fabric.
 *
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
   * @brief Construct a new Fabric object
   *
   * @param options Node options for the Fabric node
   */
  Fabric(const rclcpp::NodeOptions& options = rclcpp::NodeOptions())
    : Node("Fabric", options)
    , validation_loader_("fabric_base", "fabric::ValidationBase")
    , parsing_loader_("fabric_base", "fabric::ParserBase")
    , generation_loader_("fabric_base", "fabric::GenerationBase")
    , metadata_loader_("fabric_base", "fabric::MetadataBase")
    , core_loader_("fabric_base", "fabric::CoreBase")
    , behaviour_loader_("fabric_base", "fabric::BehaviourInterfaceBase")
    , planner_loader_("fabric_base", "fabric::PlannerBase")
    , scorer_loader_("fabric_base", "fabric::ScorerBase")
    , estimator_loader_("fabric_base", "fabric::EstimateBase")
  {
    try
    {
      // Only call setup if this object is already owned by a shared_ptr
      if (shared_from_this())
      {
        initialize();
      }
    }
    catch (const std::bad_weak_ptr&)
    {
      // Not yet safe — probably standalone without make_shared
    }
  }

  ~Fabric()
  {
    shutting_down_.store(true);

    {
      std::lock_guard<std::mutex> lock(plan_mutex_);
      plan_completed_ = true;
    }
    plan_cv_.notify_all();

    if (startup_thread_.joinable())
    {
      startup_thread_.join();
    }

    if (process_thread_.joinable())
    {
      process_thread_.join();
    }

    if (core_plugin_)
    {
      core_plugin_->shutdown();
    }
  }

  /**
   * @brief Initialize the Fabric node, action server and event client.
   *
   */
  void initialize()
  {
    /*************************************************************************
     * Parameters
     ************************************************************************/
    const std::string default_plan_file_path = ament_index_cpp::get_package_share_directory("fabric_server") + "/plans/default.xml";

    this->declare_parameter("plan_file_path", default_plan_file_path);
    plan_file_path_ = this->get_parameter("plan_file_path").as_string();

    this->declare_parameter("use_experience", false);
    use_experience_ = this->get_parameter("use_experience").as_bool();

    this->declare_parameter<bool>("rebuild_experience", false);
    rebuild_experience_ = this->get_parameter("rebuild_experience").as_bool();

    this->declare_parameter<bool>("consume_feedback", false);
    consume_feedback_ = this->get_parameter("consume_feedback").as_bool();

    this->declare_parameter<std::string>("user_feedback_root_dir", "");
    user_feedback_root_dir_ = this->get_parameter("user_feedback_root_dir").as_string();

    this->declare_parameter<bool>("dispatch_terminal_success_only", false);
    dispatch_terminal_success_only_ = this->get_parameter("dispatch_terminal_success_only").as_bool();

    /*************************************************************************
     * Fabric services
     ************************************************************************/

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

    /*************************************************************************
     * Fabric experience services
     ************************************************************************/

    get_plan_score_explanation_service_ = this->create_service<GetPlanScoreExplanation>(
        "/fabric/experience/score_explanation",
        std::bind(&Server::handleGetPlanScoreExplanation, this, std::placeholders::_1, std::placeholders::_2));

    score_plan_service_ = this->create_service<ScorePlan>("/fabric/experience/score",
                                                          std::bind(&Server::handleScorePlan, this, std::placeholders::_1, std::placeholders::_2));

    get_scoring_epoch_service_ = this->create_service<GetScoringEpoch>(
        "/fabric/experience/get_scoring_epoch", std::bind(&Server::handleGetScoringEpoch, this, std::placeholders::_1, std::placeholders::_2));

    record_plan_feedback_service_ = this->create_service<RecordPlanFeedback>(
        "/fabric/experience/record_plan_feedback", std::bind(&Server::handleRecordPlanFeedback, this, std::placeholders::_1, std::placeholders::_2));

    record_scoring_epoch_service_ = this->create_service<RecordScoringEpoch>(
        "/fabric/experience/record_scoring_epoch", std::bind(&Server::handleRecordScoringEpoch, this, std::placeholders::_1, std::placeholders::_2));

    /*************************************************************************
     * Fabric plan generate action
     ************************************************************************/

    generate_plan_server_ = rclcpp_action::create_server<GeneratePlan>(
        shared_from_this(), "/fabric/plan/generate", std::bind(&Fabric::handleGeneratePlanGoal, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Fabric::handleGeneratePlanCancel, this, std::placeholders::_1),
        std::bind(&Fabric::handleGeneratePlanAccepted, this, std::placeholders::_1));

    /*************************************************************************
     * Subscribe to system status updates from supervisor server
     ************************************************************************/

    system_status_sub_ = this->create_subscription<supervisor_msgs::msg::SystemStatus>(
        "system_status", rclcpp::QoS(rclcpp::KeepLast(10)).best_effort(), std::bind(&Server::systemStatusCallback, this, std::placeholders::_1));

    /*************************************************************************
     * Initialize Validation Plugins
     ************************************************************************/

    this->declare_parameter<std::vector<std::string>>("validation_plugins", std::vector<std::string>{ "fabric::CompatibilityValidation", "fabric::"
                                                                                                                                         "ParameterVa"
                                                                                                                                         "lidatio"
                                                                                                                                         "n" });
    std::vector<std::string> validation_plugin_names = this->get_parameter("validation_plugins").as_string_array();
    if (validation_plugin_names.empty())
    {
      throw std::runtime_error("Parameter 'validation_plugins' must contain at least one validation plugin");
    }

    for (const auto& validation_plugin_name : validation_plugin_names)
    {
      RCLCPP_INFO(this->get_logger(), "[server] Loading validation plugin: %s", validation_plugin_name.c_str());

      auto validation_plugin = validation_loader_.createSharedInstance(validation_plugin_name);
      validation_plugin->initialize(shared_from_this());
      validation_plugins_.push_back(validation_plugin);

      RCLCPP_INFO(this->get_logger(), "[server] Initialized validation plugin: %s", validation_plugin_name.c_str());
    }

    /*************************************************************************
     * Initialize Parsing Plugins
     ************************************************************************/

    this->declare_parameter("parsing_plugin", "fabric::XMLParser");
    std::string parsing_plugin_name = this->get_parameter("parsing_plugin").as_string();

    RCLCPP_INFO(this->get_logger(), "[server] Loading parsing plugin: %s", parsing_plugin_name.c_str());

    parsing_plugin_ = parsing_loader_.createSharedInstance(parsing_plugin_name);
    parsing_plugin_->initialize(shared_from_this());

    RCLCPP_INFO(this->get_logger(), "[server] Initialized parsing plugin: %s", parsing_plugin_name.c_str());

    /*************************************************************************
     * Initialize Generation Plugins
     ************************************************************************/

    this->declare_parameter("generation_plugin", "fabric::PromptToolsGenerator");
    std::string generation_plugin_name = this->get_parameter("generation_plugin").as_string();

    RCLCPP_INFO(this->get_logger(), "[server] Loading generation plugin: %s", generation_plugin_name.c_str());

    generation_plugin_ = generation_loader_.createSharedInstance(generation_plugin_name);
    generation_plugin_->initialize(shared_from_this());

    RCLCPP_INFO(this->get_logger(), "[server] Initialized generation plugin: %s", generation_plugin_name.c_str());

    /*************************************************************************
     * Initialize Knowledge Core Plugins
     ************************************************************************/

    if (use_experience_)
    {
      this->declare_parameter("core_plugin", "fabric::CoreGraphRag");
      std::string core_plugin_name = this->get_parameter("core_plugin").as_string();

      RCLCPP_INFO(this->get_logger(), "[server] Loading knowledge core plugin: %s", core_plugin_name.c_str());

      const bool should_attempt_persisted_load = !rebuild_experience_ && hasPersistedCoreDb();

      if (rebuild_experience_)
      {
        removePersistedCoreDbIfSupported();
      }      
      
      core_plugin_ = core_loader_.createSharedInstance(core_plugin_name);
      core_plugin_->initialize(shared_from_this());

      RCLCPP_INFO(this->get_logger(), "[server] Initialized knowledge core plugin: %s", core_plugin_name.c_str());

      bool loaded_from_db = false;
      if (should_attempt_persisted_load)
      {
        RCLCPP_INFO(this->get_logger(),
                    "Reusing persisted core database; skipping startup capability metadata collection.");
        try
        {
          core_plugin_->load(capabilities_);
          loaded_from_db = true;
          RCLCPP_INFO(this->get_logger(), "Core plugin loaded from persisted database.");
        }
        catch (const experience::exception& ex)
        {
          RCLCPP_WARN(this->get_logger(), "Failed to load core plugin from persisted database: %s", ex.what());
        }
      }

      if (!loaded_from_db)
      {
        collect_capabilities_if_needed();
        core_plugin_->build(capabilities_);
        RCLCPP_INFO(this->get_logger(), "Core plugin built with capability information.");
      }

      loadPlannerPlugin();
      loadBehaviourInterfacePlugin();
      loadValidationPlugins();
      loadPlanningRefinementConfig();
      loadEstimatorPlugins();
      loadScorerPlugin();
      consumePersistedUserFeedback();
    }

    /*************************************************************************
     * Initialize internal components
     ************************************************************************/

    capability_client_ = std::make_shared<CapabilityClient>();
    capability_client_->initialize(this->shared_from_this());

    /*************************************************************************
     * Load Starter plan from file
     ************************************************************************/
    fabric::plan::Plan starter_plan;
    if (!parsing_plugin_->load_file(plan_file_path_, starter_plan))
    {
      RCLCPP_ERROR(this->get_logger(), "[server] Failed to load default plan from file: %s", plan_file_path_.c_str());
      throw fabric::fabric_exception("Failed to load default plan");
    }
    starter_plan.plan_id = generate_uuid();
    starter_plan.status = fabric::plan::Status::QUEUED;

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      plan_queue_.push_back(starter_plan);
    }

    RCLCPP_INFO(this->get_logger(), "[server] Fabric node initialized. Waiting for capabilities2 readiness.");

    /*************************************************************************
     * Initialize deferred startup thread
     ************************************************************************/
    startup_thread_ = std::thread(&Fabric::wait_for_capabilities_ready, this);
  }

protected:
  /**
   * @brief The main process thread of the fabric.
   *
   */
  void process()
  {
    while (!shutting_down_.load())
    {
      // Reset per-run state before taking the next queued plan.
      reset();

      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (plan_queue_.empty())
        {
          break;
        }

        current_plan_ = plan_queue_.front();
        plan_queue_.pop_front();
      }

      RCLCPP_INFO(this->get_logger(), "[server] A new Fabric plan processing starting");

      if (current_plan_.status == fabric::plan::Status::CANCELLED)
      {
        RCLCPP_INFO(this->get_logger(), "[server] Skipping cancelled plan with id: %s", current_plan_.plan_id.c_str());
        continue;
      }

      // Parse the plan into fabric::plan::Plan connections using the active parsing plugin.
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Parsing the fabric plan.");
        current_plan_.status = fabric::plan::Status::PARSING;

        parsing_plugin_->parse(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Fabric plan parsing failed with error: %s", e.what());
        current_plan_.status = fabric::plan::Status::PARSE_FAILED;
        continue;
      }
      RCLCPP_INFO(this->get_logger(), "[server] Fabric plan parsing completed successfully.");

      // get the capabilities available in the system
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Getting capabilities available in the system");
        current_plan_.status = fabric::plan::Status::VALIDATING;

        capability_client_->getInterfaces(capability_list_);
        capability_client_->getSemanticInterfaces(capability_list_);
        capability_client_->getProviders(capability_list_);
        capability_client_->getRunnableSpecs(capability_list_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability information retrieval failed with error: %s", e.what());
        current_plan_.status = fabric::plan::Status::VALIDATION_FAILED;
        continue;
      }

      RCLCPP_INFO(this->get_logger(), "[server] Capability information retrieval completed successfully.");

      // validate the plan for compatibility based on the capabilities available in the system
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Validating the fabric plan for compatibility.");

        for (const auto& validation_plugin : validation_plugins_)
        {
          validation_plugin->validate(current_plan_, capability_list_);
        }
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Compatibility validation failed with error: %s", e.what());
        current_plan_.status = fabric::plan::Status::VALIDATION_FAILED;
        continue;
      }

      // Request bond from capabilities2 server
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Requesting bond from capabilities2 server.");
        current_plan_.status = fabric::plan::Status::BONDING;

        current_plan_.bond_id = capability_client_->request_bond();
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability bond request failed with error: %s", e.what());
        current_plan_.status = fabric::plan::Status::BOND_FAILED;
        continue;
      }

      // Start new bond client for the new bond id
      RCLCPP_INFO(this->get_logger(), "[server] Establishing bond with id : %s", current_plan_.bond_id.c_str());
      bond_client_cache_[current_plan_.bond_id] = std::make_unique<BondClient>(shared_from_this(), current_plan_.bond_id);
      bond_client_cache_[current_plan_.bond_id]->start();

      // Remove old bonds if any
      if (bond_client_cache_.size() > 1)
        for (auto& [old_bond_id, bond_client] : bond_client_cache_)
          if (old_bond_id != current_plan_.bond_id)
          {
            bond_client->stop();
            RCLCPP_INFO(this->get_logger(), "[server] Stopping and removing old bond with id : %s", old_bond_id.c_str());
          }

      RCLCPP_INFO(this->get_logger(), "[server] Bond established with id : %s", current_plan_.bond_id.c_str());

      // Request use of capabilities for the plan
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Requesting use of capabilities for the plan.");
        current_plan_.status = fabric::plan::Status::CAPABILITY_STARTING;

        capability_client_->use_capabilities(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability usage failed with error: %s", e.what());
        current_plan_.status = fabric::plan::Status::CAPABILITY_START_FAILED;

        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      // connect the capabilities as per the plan
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Connecting capabilities as per the plan.");
        current_plan_.status = fabric::plan::Status::CAPABILITY_CONNECTING;

        capability_client_->connect_capabilities(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability connection failed with error: %s", e.what());
        current_plan_.status = fabric::plan::Status::CAPABILITY_CONNECT_FAILED;

        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      RCLCPP_INFO(this->get_logger(), "[server] Capabilities connected successfully.");

      // trigger the first capability in the plan
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Triggering the first capability in the plan.");
        current_plan_.status = fabric::plan::Status::RUNNING;

        capability_client_->trigger_first_node(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability trigger failed with error: %s", e.what());
      }

      RCLCPP_INFO(this->get_logger(), "[server] First capability triggered successfully.");

      // wait for the plan to complete
      {
        std::unique_lock<std::mutex> lock(plan_mutex_);
        plan_cv_.wait(lock, [this]() { return plan_completed_ || shutting_down_.load(); });
      }

      if (shutting_down_.load())
      {
        return;
      }

      if (current_plan_.status == fabric::plan::Status::CANCELLED)
      {
        RCLCPP_INFO(this->get_logger(), "[server] Fabric plan cancelled. Releasing capabilities.");
        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      current_plan_.status = fabric::plan::Status::COMPLETED;
      RCLCPP_INFO(this->get_logger(), "[server] Fabric processing completed. Waiting for next plan.");
    }
  }

  void wait_for_capabilities_ready()
  {
    try
    {
      capability_client_->wait_until_ready();
      capabilities_ready_.store(true);
      if (use_experience_)
      {
        RCLCPP_INFO(this->get_logger(), "[server] Capabilities2 is ready. Waiting for experience readiness.");
        wait_for_experience_ready();
        experience_ready_.store(true);
      }

      RCLCPP_INFO(this->get_logger(), "[server] Capabilities2 is ready. Fabric processing is enabled.");
      maybe_start_process_thread();
    }
    catch (const std::exception& ex)
    {
      if (!shutting_down_.load())
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Waiting for capabilities2 readiness failed: %s", ex.what());
      }
    }
  }

  void wait_for_experience_ready()
  {
    auto ready_client = this->create_client<Trigger>(experience_ready_service_);

    while (!shutting_down_.load())
    {
      if (!ready_client->wait_for_service(std::chrono::seconds(1)))
      {
        RCLCPP_INFO(this->get_logger(), "[server] Waiting for experience ready service: %s", experience_ready_service_.c_str());
        continue;
      }

      auto request = std::make_shared<Trigger::Request>();
      auto future = ready_client->async_send_request(request);

      if (future.wait_for(std::chrono::seconds(1)) != std::future_status::ready)
      {
        RCLCPP_INFO(this->get_logger(), "[server] Waiting for experience readiness response from %s", experience_ready_service_.c_str());
        continue;
      }

      const auto response = future.get();
      if (response->success)
      {
        RCLCPP_INFO(this->get_logger(), "[server] Experience is ready: %s", response->message.c_str());
        return;
      }

      RCLCPP_INFO(this->get_logger(), "[server] Experience not ready yet: %s", response->message.c_str());
    }
  }

  void maybe_start_process_thread()
  {
    if (!capabilities_ready_.load() || shutting_down_.load())
    {
      return;
    }

    if (use_experience_ && !experience_ready_.load())
    {
      return;
    }

    {
      std::lock_guard<std::mutex> queue_lock(queue_mutex_);
      if (plan_queue_.empty())
      {
        return;
      }
    }

    std::lock_guard<std::mutex> process_lock(process_thread_mutex_);
    if (process_thread_running_.load())
    {
      return;
    }

    if (process_thread_.joinable())
    {
      process_thread_.join();
    }

    process_thread_running_.store(true);
    process_thread_ = std::thread([this]() {
      process();
      process_thread_running_.store(false);
    });
  }

  /**
   * @brief Callback function to set a new fabric plan.
   */
  rclcpp_action::GoalResponse handleGeneratePlanGoal(const rclcpp_action::GoalUUID& uuid, std::shared_ptr<const GeneratePlan::Goal> goal)
  {
    (void)uuid;

    if (goal->task.empty())
    {
      RCLCPP_WARN(this->get_logger(), "[server] Rejecting generation request with empty task");
      return rclcpp_action::GoalResponse::REJECT;
    }

    RCLCPP_INFO(this->get_logger(), "[server] Accepted generation request");
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse handleGeneratePlanCancel(const std::shared_ptr<GoalHandleGeneratePlan> goal_handle)
  {
    (void)goal_handle;
    RCLCPP_INFO(this->get_logger(), "[server] Generation cancellation requested");
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void handleGeneratePlanAccepted(const std::shared_ptr<GoalHandleGeneratePlan> goal_handle)
  {
    std::thread{ std::bind(&Fabric::executeGeneratePlan, this, std::placeholders::_1), goal_handle }.detach();
  }

  void executeGeneratePlan(const std::shared_ptr<GoalHandleGeneratePlan> goal_handle)
  {
    const auto goal = goal_handle->get_goal();
    auto feedback = std::make_shared<GeneratePlan::Feedback>();
    auto result = std::make_shared<GeneratePlan::Result>();

    // Update feedback to indicate that plan generation has been requested
    feedback->status.code = fabric_msgs::msg::FabricStatus::GENERATION_REQUESTED;
    goal_handle->publish_feedback(feedback);

    // Check if the goal has been canceled before proceeding
    if (goal_handle->is_canceling())
    {
      goal_handle->canceled(result);
      return;
    }

    // Generate the plan using the generation plugin
    fabric::plan::Plan generated_plan;
    try
    {
      generated_plan = generation_plugin_->generate(goal->task, goal->uuid, goal->flush);
      generated_plan.plan_id = generate_uuid();
      generated_plan.status = fabric::plan::Status::QUEUED;
    }
    catch (const std::exception& e)
    {
      // If plan generation fails, log the error and abort the goal
      RCLCPP_ERROR(this->get_logger(), "[server] Plan generation failed with error: %s", e.what());
      goal_handle->abort(result);
      return;
    }

    // Update feedback to indicate that plan generation has been completed
    if (goal_handle->is_canceling())
    {
      goal_handle->canceled(result);
      return;
    }

    // If auto_queue is true, check compatibility and queue the generated plan
    if (goal->auto_queue)
    {
      if (!parsing_plugin_->check_compatibility(generated_plan))
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Generated plan is not compatible with the loaded parser");
        goal_handle->abort(result);
        return;
      }

      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        plan_queue_.push_back(generated_plan);
      }
      maybe_start_process_thread();
      RCLCPP_INFO(this->get_logger(), "[server] Generated plan queued with id: %s", generated_plan.plan_id.c_str());
    }

    feedback->status.code = fabric_msgs::msg::FabricStatus::GENERATION_COMPLETE;
    feedback->status.plan_id = generated_plan.plan_id;
    goal_handle->publish_feedback(feedback);

    result->plan = generated_plan.plan;
    result->reasoning = generated_plan.reasoning;
    result->plan_id = generated_plan.plan_id;
    goal_handle->succeed(result);
  }

  void submitPlanCallback(const std::shared_ptr<SubmitFabricPlan::Request> request, std::shared_ptr<SubmitFabricPlan::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "[server] Received the request with a plan and extended metadata");

    fabric::plan::Plan new_plan;
    new_plan.plan = request->plan;
    new_plan.plan_id = generate_uuid();
    new_plan.status = fabric::plan::Status::QUEUED;
    new_plan.metadata.planning_request_id = request->metadata.planning_request_id;
    new_plan.metadata.candidate_plan_id = request->metadata.candidate_plan_id;
    new_plan.metadata.graph_hash = request->metadata.graph_hash;

    if (!parsing_plugin_->check_compatibility(new_plan))
    {
      RCLCPP_ERROR(this->get_logger(), "[server] Plan received via submit request not compatible with the loaded parser.");
      response->plan_id = "";
      response->error = "[server] Plan is not compatible with the loaded parser.";
      return;
    }

    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      plan_queue_.push_back(new_plan);
    }
    maybe_start_process_thread();
    response->plan_id = new_plan.plan_id;
    response->error.clear();
  }

  void parsePlanCallback(const std::shared_ptr<ParsePlan::Request> request, std::shared_ptr<ParsePlan::Response> response)
  {
    fabric::plan::Plan plan;
    plan.plan = request->plan;
    plan.plan_id = "experience_parse_request";

    try
    {
      parsing_plugin_->parse(plan);
      response->success = true;
      response->error.clear();
      for (const auto& [connection_id, connection] : plan.connections)
      {
        (void)connection_id;
        fabric_msgs::msg::FabricConnection message;
        message.source.interface = connection.source.interface;
        message.source.provider = connection.source.provider;
        message.source.instance_id = connection.source.instance_id;
        message.on_start.interface = connection.on_start.interface;
        message.on_start.provider = connection.on_start.provider;
        message.on_start.instance_id = connection.on_start.instance_id;
        message.on_stop.interface = connection.on_stop.interface;
        message.on_stop.provider = connection.on_stop.provider;
        message.on_stop.instance_id = connection.on_stop.instance_id;
        message.on_success.interface = connection.on_success.interface;
        message.on_success.provider = connection.on_success.provider;
        message.on_success.instance_id = connection.on_success.instance_id;
        message.on_failure.interface = connection.on_failure.interface;
        message.on_failure.provider = connection.on_failure.provider;
        message.on_failure.instance_id = connection.on_failure.instance_id;
        message.description = connection.description;
        response->connections.push_back(message);
      }
    }
    catch (const std::exception& ex)
    {
      response->success = false;
      response->error = ex.what();
      response->connections.clear();
    }
  }

  /**
   * @brief Callback function to cancel a fabric plan.
   */
  void cancelPlanCallback(const std::shared_ptr<CancelFabricPlan::Request> request, std::shared_ptr<CancelFabricPlan::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "[server] Plan cancelling requested");
    std::string plan_id = request->plan_id;
    std::string bond_id_to_cancel;
    bool found = false;

    // search for the bond id associated with the plan id from current plan or plan queue
    if (current_plan_.plan_id == plan_id)
    {
      current_plan_.status = fabric::plan::Status::CANCELLED;
      bond_id_to_cancel = current_plan_.bond_id;
      plan_completed_ = true;
      plan_cv_.notify_all();
      found = true;
    }
    else
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      for (auto& plan : plan_queue_)
      {
        if (plan.plan_id == plan_id)
        {
          plan.status = fabric::plan::Status::CANCELLED;
          bond_id_to_cancel = plan.bond_id;
          found = true;
          break;
        }
      }
    }

    // if bond id is found, break the bond and cancel the plan
    if (!bond_id_to_cancel.empty())
    {
      RCLCPP_INFO(this->get_logger(), "[server] Cancelling plan with id: %s", plan_id.c_str());

      for (auto& [bond_id, bond_client] : bond_client_cache_)
      {
        if (bond_id == bond_id_to_cancel)
        {
          bond_client->stop();
          RCLCPP_INFO(this->get_logger(), "[server] Bond with id : %s stopped", bond_id.c_str());
          break;
        }
      }
    }

    response->success = found;
  }

  /**
   * @brief Callback function to set the completion of a fabric plan.
   */
  void setCompleteCallback(const std::shared_ptr<CompleteFabric::Request> request, std::shared_ptr<CompleteFabric::Response> response)
  {
    (void)response;

    // mark the plan as completed using plan id
    RCLCPP_INFO(this->get_logger(), "[server] Plan completion received for plan id: %s", request->plan_id.c_str());

    if (current_plan_.plan_id != request->plan_id)
    {
      RCLCPP_WARN(this->get_logger(), "[server] Received plan id does not match the current plan id");
    }
    else
    {
      RCLCPP_INFO(this->get_logger(), "[server] Current plan id matches the received plan id. Proceeding to complete the plan.");
      plan_completed_ = true;
      plan_cv_.notify_all();
    }
  }

  /**
   * @brief Callback function to get the status of a fabric plan.
   */
  void getPlanStatusCallback(const std::shared_ptr<GetPlanStatus::Request> request, std::shared_ptr<GetPlanStatus::Response> response)
  {
    response->header.stamp = this->now();

    // If the request plan_id and bond_id are empty, return the current plan status
    if (request->plan_id.empty() && request->bond_id.empty())
    {
      response->status = status_msg(current_plan_);

      if (current_plan_.status == fabric::plan::Status::VALIDATION_FAILED || current_plan_.status == fabric::plan::Status::PARSE_FAILED)
        response->rejected_list = current_plan_.rejected_list;

      return;
    }

    // If plan id or bond id provided, search the plan queue
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (const auto& plan : plan_queue_)
    {
      if (plan.plan_id == request->plan_id || plan.bond_id == request->bond_id)
      {
        response->status = status_msg(plan);

        if (plan.status == fabric::plan::Status::VALIDATION_FAILED || plan.status == fabric::plan::Status::PARSE_FAILED)
          response->rejected_list = plan.rejected_list;

        return;
      }
    }

    // Not found
    response->status.code = fabric_msgs::msg::FabricStatus::UNKNOWN;
    response->rejected_list.clear();
  }

  /**
   * @brief Convert internal plan status to fabric_msgs/PlanStatus message
   */
  fabric_msgs::msg::FabricStatus status_msg(fabric::plan::Plan plan) const
  {
    fabric_msgs::msg::FabricStatus status_msg;

    status_msg.plan_id = plan.plan_id;
    status_msg.metadata.planning_request_id = plan.metadata.planning_request_id;
    status_msg.metadata.candidate_plan_id = plan.metadata.candidate_plan_id;
    status_msg.metadata.graph_hash = plan.metadata.graph_hash;

    switch (plan.status)
    {
      case fabric::plan::Status::UNKNOWN:
        status_msg.code = fabric_msgs::msg::FabricStatus::UNKNOWN;
        break;
      case fabric::plan::Status::QUEUED:
        status_msg.code = fabric_msgs::msg::FabricStatus::QUEUED;
        break;
      case fabric::plan::Status::PARSING:
        status_msg.code = fabric_msgs::msg::FabricStatus::PARSING;
        break;
      case fabric::plan::Status::PARSE_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::PARSE_FAILED;
        break;
      case fabric::plan::Status::VALIDATING:
        status_msg.code = fabric_msgs::msg::FabricStatus::VALIDATING;
        break;
      case fabric::plan::Status::VALIDATION_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::VALIDATION_FAILED;
        break;
      case fabric::plan::Status::BONDING:
        status_msg.code = fabric_msgs::msg::FabricStatus::BONDING;
        break;
      case fabric::plan::Status::BOND_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::BOND_FAILED;
        break;
      case fabric::plan::Status::CAPABILITY_STARTING:
        status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_STARTING;
        break;
      case fabric::plan::Status::CAPABILITY_START_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_START_FAILED;
        break;
      case fabric::plan::Status::CAPABILITY_CONNECTING:
        status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_CONNECTING;
        break;
      case fabric::plan::Status::CAPABILITY_CONNECT_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_CONNECT_FAILED;
        break;
      case fabric::plan::Status::RUNNING:
        status_msg.code = fabric_msgs::msg::FabricStatus::RUNNING;
        break;
      case fabric::plan::Status::COMPLETED:
        status_msg.code = fabric_msgs::msg::FabricStatus::COMPLETED;
        break;
      case fabric::plan::Status::CANCELLED:
        status_msg.code = fabric_msgs::msg::FabricStatus::CANCELLED;
        break;
      default:
        status_msg.code = fabric_msgs::msg::FabricStatus::UNKNOWN;
        break;
    }
    return status_msg;
  }
  /**
   * @brief Generate a UUID string for plan tracking
   */
  static std::string generate_uuid()
  {
    uuid_t uuid;
    uuid_generate_random(uuid);
    char uuid_str[40];
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);
  }

  /**
   * @brief Reset internal data structures for processing a new plan.
   *
   */
  void reset()
  {
    current_plan_ = fabric::plan::Plan();
    capability_list_.clear();
    bond_id_.clear();
    plan_completed_ = false;
  }

  /**
   * @brief Load and initialize the configured estimator plugins.
    *
    * Estimators are loaded dynamically so deployments can add or remove feature
    * generators without recompiling the server.
   */
  void loadEstimatorPlugins()
  {
    const std::vector<std::string> default_estimators = {
      "experience::SuccessProbabilityPlugin", "experience::RuntimePlugin",  "experience::EnergyPlugin",
      "experience::FaultBurdenPlugin",        "experience::ResourcePlugin", "experience::UncertaintyPlugin"
    };

    const std::vector<std::string> default_estimator_names = {
      "success", "time", "energy", "fault", "resource", "uncertainty"
    };

    const std::unordered_map<std::string, std::string> default_estimator_types = {
      { "success", "experience::SuccessProbabilityPlugin" },
      { "time", "experience::RuntimePlugin" },
      { "energy", "experience::EnergyPlugin" },
      { "fault", "experience::FaultBurdenPlugin" },
      { "resource", "experience::ResourcePlugin" },
      { "uncertainty", "experience::UncertaintyPlugin" },
    };

    this->declare_parameter<std::vector<std::string>>("RapfsScorerPlugin.estimator_names", default_estimator_names);
    std::vector<std::string> configured_estimator_names;
    this->get_parameter("RapfsScorerPlugin.estimator_names", configured_estimator_names);

    std::vector<std::string> configured_plugins;
    for (const auto& estimator_name : configured_estimator_names)
    {
      const auto default_type_it = default_estimator_types.find(estimator_name);
      const std::string default_type =
        default_type_it != default_estimator_types.end() ? default_type_it->second : std::string();
      const std::string type_key = "RapfsScorerPlugin.estimators." + estimator_name + ".type";
      this->declare_parameter<std::string>(type_key, default_type);
      const std::string plugin_type = this->get_parameter(type_key).as_string();
      if (!plugin_type.empty())
        configured_plugins.push_back(plugin_type);
    }

    this->declare_parameter<std::vector<std::string>>("estimator_plugins", default_estimators);
    this->get_parameter("estimator_plugins", estimator_plugin_names_);
    if (!configured_plugins.empty())
      estimator_plugin_names_ = configured_plugins;

    estimator_plugins_.clear();
    for (const auto& plugin_name : estimator_plugin_names_)
    {
      try
      {
        auto plugin = estimator_loader_.createSharedInstance(plugin_name);
        plugin->initialize(shared_from_this());
        estimator_plugins_.push_back(plugin);
        RCLCPP_INFO(this->get_logger(), "Loaded estimator plugin: %s", plugin_name.c_str());
      }
      catch (const pluginlib::PluginlibException& ex)
      {
        RCLCPP_WARN(this->get_logger(), "Failed to load estimator plugin '%s': %s", plugin_name.c_str(), ex.what());
      }
    }
  }

  /**
   * @brief Load and initialize the configured scorer plugin.
    *
    * The scorer is configured after estimators so it can immediately consume the
    * active feature pipeline during planning requests.
   */
  void loadScorerPlugin()
  {
    this->declare_parameter<std::string>("scorer_plugin", "experience::RapfsScorerPlugin");
    this->get_parameter("scorer_plugin", scorer_plugin_name_);

    scorer_plugin_ = scorer_loader_.createSharedInstance(scorer_plugin_name_);
    scorer_plugin_->initialize(shared_from_this());
    scorer_plugin_->configure(core_plugin_, &estimator_plugins_);
    RCLCPP_INFO(this->get_logger(), "Loaded scorer plugin: %s", scorer_plugin_name_.c_str());
  }

  /**
   * @brief Load and initialize the configured validation plugins.
   *
   * Validators run after parsing and before scoring so malformed or
   * unsatisfied candidate plans never win final selection.
   */
  void loadValidationPlugins()
  {
    this->declare_parameter<std::vector<std::string>>(
      "validation_plugins",
      std::vector<std::string>{ "experience::CompatibilityValidation", "experience::DependencyValidation",
                                "experience::DataflowValidation" });
    this->get_parameter("validation_plugins", validation_plugin_names_);

    validation_plugins_.clear();
    for (const auto& plugin_name : validation_plugin_names_)
    {
      try
      {
        auto plugin = validation_loader_.createSharedInstance(plugin_name);
        plugin->initialize(shared_from_this());
        validation_plugins_.push_back(plugin);
        RCLCPP_INFO(this->get_logger(), "Loaded validation plugin: %s", plugin_name.c_str());
      }
      catch (const pluginlib::PluginlibException& ex)
      {
        RCLCPP_WARN(this->get_logger(), "Failed to load validation plugin '%s': %s", plugin_name.c_str(), ex.what());
      }
    }
  }

  /**
   * @brief Load refinement retry policy driven by validation outcomes.
   */
  void loadPlanningRefinementConfig()
  {
    const std::string max_attempts_key = planner_plugin_name_ + ".refinement.max_attempts";
    const std::string retry_on_penalty_key = planner_plugin_name_ + ".refinement.retry_on_penalty_findings";
    const std::string retry_on_warning_key = planner_plugin_name_ + ".refinement.retry_on_warning_findings";

    this->declare_parameter<int>(max_attempts_key, planning_refinement_max_attempts_);
    this->declare_parameter<bool>(retry_on_penalty_key,
                                  planning_refinement_retry_on_penalty_findings_);
    this->declare_parameter<bool>(retry_on_warning_key,
                                  planning_refinement_retry_on_warning_findings_);

    planning_refinement_max_attempts_ =
      std::max(0, static_cast<int>(this->get_parameter(max_attempts_key).as_int()));
    planning_refinement_retry_on_penalty_findings_ =
      this->get_parameter(retry_on_penalty_key).as_bool();
    planning_refinement_retry_on_warning_findings_ =
      this->get_parameter(retry_on_warning_key).as_bool();
  }

    /**
   * @brief Load and initialize the configured planner plugin.
    *
    * Planning is pluginized so generation strategies can change independently of
    * the server lifecycle and persistence code.
   */
  void loadPlannerPlugin()
  {
    this->declare_parameter<std::string>("planner_plugin", "experience::PlanningPlugin");
    this->get_parameter("planner_plugin", planner_plugin_name_);

    planner_plugin_ = planner_loader_.createSharedInstance(planner_plugin_name_);
    planner_plugin_->initialize(shared_from_this());
    RCLCPP_INFO(this->get_logger(), "Loaded planner plugin: %s", planner_plugin_name_.c_str());
  }

  /**
   * @brief Load and initialize the configured behaviour-interface plugin.
    *
    * The behaviour interface isolates Fabric-specific parsing from the rest of
    * the server so other execution backends can be swapped in later.
   */
  void loadBehaviourInterfacePlugin()
  {
    this->declare_parameter<std::string>("behaviour_interface_plugin", "experience::FabricPlugin");
    this->get_parameter("behaviour_interface_plugin", behaviour_interface_plugin_name_);

    behaviour_plugin_ = behaviour_loader_.createSharedInstance(behaviour_interface_plugin_name_);
    behaviour_plugin_->initialize(shared_from_this());
    RCLCPP_INFO(this->get_logger(), "Loaded behaviour interface plugin: %s", behaviour_interface_plugin_name_.c_str());
  }


  /** Vector of plans */
  std::deque<fabric::plan::Plan> plan_queue_;

  /** Protect queued plan access across callbacks and processing thread */
  std::mutex queue_mutex_;

  /** Current plan being processed */
  fabric::plan::Plan current_plan_;

  /** Current plan related synchronization */
  bool plan_completed_{ false };
  std::mutex plan_mutex_;
  std::condition_variable plan_cv_;

  /** Deferred startup and process thread lifecycle */
  std::thread startup_thread_;
  std::thread process_thread_;
  std::mutex process_thread_mutex_;
  std::atomic<bool> capabilities_ready_{ false };
  std::atomic<bool> experience_ready_{ false };

  bool use_experience_{ false };
  bool rebuild_experience_{ false };

  std::string experience_ready_service_;
  std::atomic<bool> process_thread_running_{ false };
  std::atomic<bool> shutting_down_{ false };

  /** Capability client to interact with capability server */
  std::shared_ptr<CapabilityClient> capability_client_;

  /** Capability List */
  std::vector<fabric::capability::Info> capability_list_;

  /** Plugin loaders for each module package */
  pluginlib::ClassLoader<fabric::ValidationBase> validation_loader_;
  pluginlib::ClassLoader<fabric::ParserBase> parsing_loader_;
  pluginlib::ClassLoader<fabric::GenerationBase> generation_loader_;

  /** shared pointer for parsing plugin */
  std::shared_ptr<fabric::ParserBase> parsing_plugin_;

  /** ordered validation plugins applied to each parsed plan */
  std::vector<std::shared_ptr<fabric::ValidationBase>> validation_plugins_;

  /** shared pointer for generation plugin */
  std::shared_ptr<fabric::GenerationBase> generation_plugin_;

  /**
   * @brief Plugin loader for capability metadata adapters.
   */
  pluginlib::ClassLoader<experience::MetadataBase> metadata_loader_;

  /**
   * @brief Shared pointer to the active metadata plugin.
   */
  std::shared_ptr<experience::MetadataBase> metadata_plugin_;

  /**
   * @brief Plugin loader for the core knowledge management plugin.
   */
  pluginlib::ClassLoader<experience::CoreBase> core_loader_;

  /**
   * @brief Plugin loader for fabric interface plugins.
   */
  pluginlib::ClassLoader<experience::BehaviourInterfaceBase> behaviour_loader_;

  /**
   * @brief Plugin loader for planner plugins.
   */
  pluginlib::ClassLoader<experience::PlannerBase> planner_loader_;

  /**
   * @brief Plugin loader for candidate-plan scorer plugins.
   */
  pluginlib::ClassLoader<experience::ScorerBase> scorer_loader_;

  /**
   * @brief Plugin loader for candidate-plan validation plugins.
   */
  pluginlib::ClassLoader<experience::ValidationBase> validation_loader_;

  /**
   * @brief Plugin loader for capability feature estimators.
   */
  pluginlib::ClassLoader<experience::EstimateBase> estimator_loader_;

  /**
   * @brief Shared pointer to the core knowledge management plugin.
   */
  std::shared_ptr<experience::CoreBase> core_plugin_;

  /**
   * @brief Shared pointer to the active fabric interface plugin.
   */
  std::shared_ptr<experience::BehaviourInterfaceBase> behaviour_plugin_;

  /**
   * @brief Shared pointer to the active planner plugin.
   */
  std::shared_ptr<experience::PlannerBase> planner_plugin_;

  /**
   * @brief Shared pointer to the active candidate-plan scorer plugin.
   */
  std::shared_ptr<experience::ScorerBase> scorer_plugin_;

  /**
   * @brief Loaded validation plugins used before candidate scoring.
   */
  std::vector<std::shared_ptr<experience::ValidationBase>> validation_plugins_;

  /**
   * @brief Loaded estimator plugins used to compute RAPFS node features.
   */
  std::vector<std::shared_ptr<experience::EstimateBase>> estimator_plugins_;

  /**
   * @brief Name of the core plugin to load.
   */
  std::string core_plugin_name;

  /**
   * @brief Configured validation plugin class names.
   */
  std::vector<std::string> validation_plugin_names_;

  /**
   * @brief Name of the metadata plugin to load.
   */
  std::string metadata_plugin_name_;

  /**
   * @brief Name of the planner plugin to load.
   */
  std::string planner_plugin_name_;

  /**
   * @brief Name of the scorer plugin to load.
   */
  std::string scorer_plugin_name_;

  /**
   * @brief Name of the fabric interface plugin to load.
   */
  std::string behaviour_interface_plugin_name_;

  /**
   * @brief Names of estimator plugins requested by ROS parameter.
   */
  std::vector<std::string> estimator_plugin_names_;

  /** Bond id */
  std::string bond_id_;

  /** Manages bond between capabilities server and this client */
  std::map<std::string, std::shared_ptr<BondClient>> bond_client_cache_;

  /** File Path link */
  std::string plan_file_path_;

  /** server to submit a plan with optional Fabric-owned metadata */
  rclcpp::Service<SubmitFabricPlan>::SharedPtr submit_plan_server_;

  /** server to cancel the current plan in the capabilities2 fabric */
  rclcpp::Service<CancelFabricPlan>::SharedPtr cancel_server_;

  /** server to get the status of the capabilities2 fabric */
  rclcpp::Service<CompleteFabric>::SharedPtr completion_server_;

  /** server to get the status of a fabric plan */
  rclcpp::Service<GetPlanStatus>::SharedPtr get_plan_status_server_;

  /** service to parse Fabric XML into connection graph data */
  rclcpp::Service<ParsePlan>::SharedPtr parse_plan_server_;

  /**
   * @brief Debug service for selected plan score explanations.
   */
  rclcpp::Service<GetPlanScoreExplanation>::SharedPtr get_plan_score_explanation_service_;

  /**
   * @brief Service for scoring a supplied plan XML without execution dispatch.
   */
  rclcpp::Service<ScorePlan>::SharedPtr score_plan_service_;

  /**
   * @brief Read service for scorer epoch/version checkpoints.
   */
  rclcpp::Service<GetScoringEpoch>::SharedPtr get_scoring_epoch_service_;

  /**
   * @brief Service for recording external whole-plan feedback.
   */
  rclcpp::Service<RecordPlanFeedback>::SharedPtr record_plan_feedback_service_;

  /**
   * @brief Service for recording scorer epoch/version checkpoints.
   */
  rclcpp::Service<RecordScoringEpoch>::SharedPtr record_scoring_epoch_service_;

  /** action server to generate a plan */
  rclcpp_action::Server<GeneratePlan>::SharedPtr generate_plan_server_;

};  // class Fabric

}  // namespace fabric