#pragma once

#include <map>
#include <deque>
#include <string>
#include <thread>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <tinyxml2.h>
#include <uuid/uuid.h>
#include <rclcpp/rclcpp.hpp>
#include "rclcpp_action/rclcpp_action.hpp"
#include <pluginlib/class_loader.hpp>

#include <fabric_base/utils/xml_helper.hpp>
#include <fabric_base/utils/structs.hpp>
#include <fabric_base/validation_base.hpp>
#include <fabric_base/parser_base.hpp>
#include <fabric_base/generation_base.hpp>

#include <fabric_server/capability_client.hpp>
#include <fabric_server/bond_client.hpp>

#include <fabric_msgs/srv/submit_fabric_plan.hpp>
#include <fabric_msgs/srv/cancel_fabric_plan.hpp>
#include <fabric_msgs/srv/complete_fabric.hpp>
#include <fabric_msgs/srv/get_plan_status.hpp>
#include <fabric_msgs/srv/parse_plan.hpp>
#include <fabric_msgs/msg/fabric_status.hpp>
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
     * Fabric generate plan action
     ************************************************************************/

    generate_plan_server_ = rclcpp_action::create_server<GeneratePlan>(
        shared_from_this(), "/fabric/plan/generate", std::bind(&Fabric::handleGeneratePlanGoal, this, std::placeholders::_1, std::placeholders::_2),
        std::bind(&Fabric::handleGeneratePlanCancel, this, std::placeholders::_1),
        std::bind(&Fabric::handleGeneratePlanAccepted, this, std::placeholders::_1));

    /*************************************************************************
     * Initialize Compatibility Validation Plugin
     ************************************************************************/

    this->declare_parameter("compatibility_validation_plugin", "fabric::CompatibilityValidation");
    std::string compatibility_validation_plugin_name = this->get_parameter("compatibility_validation_plugin").as_string();

    RCLCPP_INFO(this->get_logger(), "[server] Loading compatibility validation plugin: %s", compatibility_validation_plugin_name.c_str());

    compatibility_validation_plugin_ = validation_loader_.createSharedInstance(compatibility_validation_plugin_name);
    compatibility_validation_plugin_->initialize(shared_from_this());

    RCLCPP_INFO(this->get_logger(), "[server] Initialized compatibility validation plugin: %s", compatibility_validation_plugin_name.c_str());

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
     * Initialize internal components
     ************************************************************************/
    capability_client_ = std::make_shared<CapabilityClient>();
    capability_client_->initialize(this->shared_from_this());

    /*************************************************************************
     * Load Starter plan from file
     ************************************************************************/
    fabric::Plan starter_plan;
    if (!parsing_plugin_->load_file(plan_file_path_, starter_plan))
    {
      RCLCPP_ERROR(this->get_logger(), "[server] Failed to load default plan from file: %s", plan_file_path_.c_str());
      throw fabric::fabric_exception("Failed to load default plan");
    }
    starter_plan.plan_id = generate_uuid();
    starter_plan.status = PlanStatus::QUEUED;

    plan_queue_.push_back(starter_plan);

    RCLCPP_INFO(this->get_logger(), "[server] Fabric node initialized");

    /*************************************************************************
     * Initialize process thread
     ************************************************************************/

    process_thread = std::thread(&Fabric::process, this);
  }

protected:
  /**
   * @brief The main process thread of the fabric.
   *
   */
  void process()
  {
    while (plan_queue_.size() > 0)
    {
      // reset internal data structures
      reset();

      RCLCPP_INFO(this->get_logger(), "[server] A new Fabric plan processing starting");

      // get the next plan and parse it into a XML document
      current_plan_ = plan_queue_.front();
      plan_queue_.pop_front();

      if (current_plan_.status == PlanStatus::CANCELLED)
      {
        RCLCPP_INFO(this->get_logger(), "[server] Skipping cancelled plan with id: %s", current_plan_.plan_id.c_str());
        continue;
      }

      // parse the plan to extract connections (Fabric::Plan) as per the parsing plugin
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Parsing the fabric plan.");
        current_plan_.status = PlanStatus::PARSING;

        parsing_plugin_->parse(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Fabric plan parsing failed with error: %s", e.what());
        current_plan_.status = PlanStatus::PARSE_FAILED;
        continue;
      }
      RCLCPP_INFO(this->get_logger(), "[server] Fabric plan parsing completed successfully.");

      // get the capabilities available in the system
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Getting capabilities available in the system");
        current_plan_.status = PlanStatus::VALIDATING;

        capability_client_->getInterfaces(capability_list_);
        capability_client_->getSemanticInterfaces(capability_list_);
        capability_client_->getProviders(capability_list_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability information retrieval failed with error: %s", e.what());
        current_plan_.status = PlanStatus::VALIDATION_FAILED;
        continue;
      }

      RCLCPP_INFO(this->get_logger(), "[server] Capability information retrieval completed successfully.");

      // validate the plan for compatibility based on the capabilities available in the system
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Validating the fabric plan for compatibility.");

        compatibility_validation_plugin_->validate(current_plan_, capability_list_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Compatibility validation failed with error: %s", e.what());
        current_plan_.status = PlanStatus::VALIDATION_FAILED;
        continue;
      }

      // Request bond from capabilities2 server
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Requesting bond from capabilities2 server.");
        current_plan_.status = PlanStatus::BONDING;

        current_plan_.bond_id = capability_client_->request_bond();
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability bond request failed with error: %s", e.what());
        current_plan_.status = PlanStatus::BOND_FAILED;
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
        current_plan_.status = PlanStatus::CAPABILITY_STARTING;

        capability_client_->use_capabilities(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability usage failed with error: %s", e.what());
        current_plan_.status = PlanStatus::CAPABILITY_START_FAILED;

        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      // connect the capabilities as per the plan
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Connecting capabilities as per the plan.");
        current_plan_.status = PlanStatus::CAPABILITY_CONNECTING;

        capability_client_->connect_capabilities(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability connection failed with error: %s", e.what());
        current_plan_.status = PlanStatus::CAPABILITY_CONNECT_FAILED;

        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      RCLCPP_INFO(this->get_logger(), "[server] Capabilities connected successfully.");

      // trigger the first capability in the plan
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Triggering the first capability in the plan.");
        current_plan_.status = PlanStatus::RUNNING;

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
        plan_cv_.wait(lock, [this]() { return plan_completed_; });
      }

      if (current_plan_.status == PlanStatus::CANCELLED)
      {
        RCLCPP_INFO(this->get_logger(), "[server] Fabric plan cancelled. Releasing capabilities.");
        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      current_plan_.status = PlanStatus::COMPLETED;
      RCLCPP_INFO(this->get_logger(), "[server] Fabric processing completed. Waiting for next plan.");
    }
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
    fabric::Plan generated_plan;
    try
    {
      generated_plan = generation_plugin_->generate(goal->task, goal->uuid, goal->flush);
      generated_plan.plan_id = generate_uuid();
      generated_plan.status = PlanStatus::QUEUED;
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

      plan_queue_.push_back(generated_plan);
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

  void submitPlanCallback(const std::shared_ptr<SubmitFabricPlan::Request> request,
                          std::shared_ptr<SubmitFabricPlan::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "[server] Received the request with a plan and extended metadata");

    fabric::Plan new_plan;
    new_plan.plan = request->plan;
    new_plan.plan_id = generate_uuid();
    new_plan.status = PlanStatus::QUEUED;
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

    plan_queue_.push_back(new_plan);
    response->plan_id = new_plan.plan_id;
    response->error.clear();
  }

  void parsePlanCallback(const std::shared_ptr<ParsePlan::Request> request, std::shared_ptr<ParsePlan::Response> response)
  {
    fabric::Plan plan;
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
      current_plan_.status = PlanStatus::CANCELLED;
      bond_id_to_cancel = current_plan_.bond_id;
      plan_completed_ = true;
      plan_cv_.notify_all();
      found = true;
    }
    else
    {
      for (auto& plan : plan_queue_)
      {
        if (plan.plan_id == plan_id)
        {
          plan.status = PlanStatus::CANCELLED;
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

      if (current_plan_.status == PlanStatus::VALIDATION_FAILED || current_plan_.status == PlanStatus::PARSE_FAILED)
        response->rejected_list = current_plan_.rejected_list;

      return;
    }

    // If plan id or bond id provided, search the plan queue
    for (const auto& plan : plan_queue_)
    {
      if (plan.plan_id == request->plan_id || plan.bond_id == request->bond_id)
      {
        response->status = status_msg(plan);

        if (plan.status == PlanStatus::VALIDATION_FAILED || plan.status == PlanStatus::PARSE_FAILED)
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
  fabric_msgs::msg::FabricStatus status_msg(Plan plan) const
  {
    fabric_msgs::msg::FabricStatus status_msg;

    status_msg.plan_id = plan.plan_id;
    status_msg.metadata.planning_request_id = plan.metadata.planning_request_id;
    status_msg.metadata.candidate_plan_id = plan.metadata.candidate_plan_id;
    status_msg.metadata.graph_hash = plan.metadata.graph_hash;

    switch (plan.status)
    {
      case PlanStatus::UNKNOWN:
        status_msg.code = fabric_msgs::msg::FabricStatus::UNKNOWN;
        break;
      case PlanStatus::QUEUED:
        status_msg.code = fabric_msgs::msg::FabricStatus::QUEUED;
        break;
      case PlanStatus::PARSING:
        status_msg.code = fabric_msgs::msg::FabricStatus::PARSING;
        break;
      case PlanStatus::PARSE_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::PARSE_FAILED;
        break;
      case PlanStatus::VALIDATING:
        status_msg.code = fabric_msgs::msg::FabricStatus::VALIDATING;
        break;
      case PlanStatus::VALIDATION_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::VALIDATION_FAILED;
        break;
      case PlanStatus::BONDING:
        status_msg.code = fabric_msgs::msg::FabricStatus::BONDING;
        break;
      case PlanStatus::BOND_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::BOND_FAILED;
        break;
      case PlanStatus::CAPABILITY_STARTING:
        status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_STARTING;
        break;
      case PlanStatus::CAPABILITY_START_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_START_FAILED;
        break;
      case PlanStatus::CAPABILITY_CONNECTING:
        status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_CONNECTING;
        break;
      case PlanStatus::CAPABILITY_CONNECT_FAILED:
        status_msg.code = fabric_msgs::msg::FabricStatus::CAPABILITY_CONNECT_FAILED;
        break;
      case PlanStatus::RUNNING:
        status_msg.code = fabric_msgs::msg::FabricStatus::RUNNING;
        break;
      case PlanStatus::COMPLETED:
        status_msg.code = fabric_msgs::msg::FabricStatus::COMPLETED;
        break;
      case PlanStatus::CANCELLED:
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
    current_plan_ = fabric::Plan();
    capability_list_.clear();
    bond_id_.clear();
    plan_completed_ = false;
  }

  /** Vector of plans */
  std::deque<fabric::Plan> plan_queue_;

  /** Current plan being processed */
  fabric::Plan current_plan_;

  /** Current plan related synchronization */
  bool plan_completed_;
  std::mutex plan_mutex_;
  std::condition_variable plan_cv_;

  /** Capability client to interact with capability server */
  std::shared_ptr<CapabilityClient> capability_client_;

  /** Capability List */
  std::vector<CapabilityInfo> capability_list_;

  /** Plugin loaders for each module package */
  pluginlib::ClassLoader<fabric::ValidationBase> validation_loader_;
  pluginlib::ClassLoader<fabric::ParserBase> parsing_loader_;
  pluginlib::ClassLoader<fabric::GenerationBase> generation_loader_;

  /** shared pointer for parsing plugin */
  std::shared_ptr<fabric::ParserBase> parsing_plugin_;

  /** shared pointer for compatibility validation plugin */
  std::shared_ptr<fabric::ValidationBase> compatibility_validation_plugin_;

  /** shared pointer for generation plugin */
  std::shared_ptr<fabric::GenerationBase> generation_plugin_;

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

  /** Thread to manage sending goal */
  std::thread process_thread;

  /** action server to generate a plan */
  rclcpp_action::Server<GeneratePlan>::SharedPtr generate_plan_server_;

};  // class Fabric

}  // namespace fabric