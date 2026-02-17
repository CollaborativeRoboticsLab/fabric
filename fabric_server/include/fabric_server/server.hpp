#pragma once

#include <map>
#include <deque>
#include <string>
#include <thread>

#include <tinyxml2.h>
#include <uuid/uuid.h>
#include <rclcpp/rclcpp.hpp>
#include <pluginlib/class_loader.hpp>

#include <fabric_base/utils/xml_helper.hpp>
#include <fabric_base/utils/structs.hpp>
#include <fabric_base/validation_base.hpp>
#include <fabric_base/parser_base.hpp>

#include <fabric_server/capability_client.hpp>
#include <fabric_server/bond_client.hpp>

#include <fabric_msgs/srv/set_fabric_plan.hpp>
#include <fabric_msgs/srv/cancel_fabric_plan.hpp>
#include <fabric_msgs/srv/complete_fabric.hpp>
#include <fabric_msgs/srv/get_plan_status.hpp>
#include <fabric_msgs/msg/fabric_status.hpp>

namespace fabric
{

/**
 * @brief Class representing the main server of the fabric.
 *
 */
class Fabric : public rclcpp::Node
{
public:
  using SetFabricPlan = fabric_msgs::srv::SetFabricPlan;
  using CancelFabricPlan = fabric_msgs::srv::CancelFabricPlan;
  using CompleteFabric = fabric_msgs::srv::CompleteFabric;
  using GetPlanStatus = fabric_msgs::srv::GetPlanStatus;

  /**
   * @brief Construct a new Fabric object
   *
   * @param options Node options for the Fabric node
   */
  Fabric(const rclcpp::NodeOptions& options = rclcpp::NodeOptions())
    : Node("Fabric", options)
    , validation_loader_("fabric_base", "fabric::ValidationBase")
    , parsing_loader_("fabric_base", "fabric::ParserBase")
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
    this->declare_parameter("plan_file_path", "install/fabric/share/fabric/plans/default.xml");
    plan_file_path_ = this->get_parameter("plan_file_path").as_string();

    /*************************************************************************
     * Fabric services
     ************************************************************************/
    plan_server_ = this->create_service<SetFabricPlan>("/fabric/set_plan",
                                                       std::bind(&Fabric::setPlanCallback, this, std::placeholders::_1, std::placeholders::_2));

    cancel_server_ = this->create_service<CancelFabricPlan>(
        "/fabric/cancel_plan", std::bind(&Fabric::cancelPlanCallback, this, std::placeholders::_1, std::placeholders::_2));

    completion_server_ = this->create_service<CompleteFabric>(
        "/fabric/set_completion", std::bind(&Fabric::setCompleteCallback, this, std::placeholders::_1, std::placeholders::_2));

    get_plan_status_server_ = this->create_service<GetPlanStatus>(
        "/fabric/get_plan_status", std::bind(&Fabric::getPlanStatusCallback, this, std::placeholders::_1, std::placeholders::_2));

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

      // get the capabilities required for the plan
      try
      {
        RCLCPP_INFO(this->get_logger(), "[server] Getting capabilities required for the plan.");
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

      // validate the plan for compatibility as per the validation plugin
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

      current_plan_.status = PlanStatus::COMPLETED;
      RCLCPP_INFO(this->get_logger(), "[server] Fabric processing completed. Waiting for next plan.");
    }
  }

  /**
   * @brief Callback function to set a new fabric plan.
   */
  void setPlanCallback(const std::shared_ptr<SetFabricPlan::Request> request, std::shared_ptr<SetFabricPlan::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "[server] Received the request with a plan");

    fabric::Plan new_plan;
    new_plan.plan = request->plan;
    new_plan.plan_id = generate_uuid();
    new_plan.status = PlanStatus::QUEUED;

    if (!parsing_plugin_->check_compatibility(new_plan))
    {
      RCLCPP_ERROR(this->get_logger(), "[server] Plan received via service request not compatible with the loaded parser.");
      response->plan_id = "";
      response->error = "[server] Plan is not compatible with the loaded parser.";
      return;
    }
    RCLCPP_INFO(this->get_logger(), "[server] Plan accepted from service request message");

    plan_queue_.push_back(new_plan);
    response->plan_id = new_plan.plan_id;
  }

  /**
   * @brief Callback function to cancel a fabric plan.
   */
  void cancelPlanCallback(const std::shared_ptr<CancelFabricPlan::Request> request, std::shared_ptr<CancelFabricPlan::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "[server] Plan canncelling requested");
    std::string plan_id = request->plan_id;
    std::string bond_id_to_cancel;

    // search for the bond id associated with the plan id from current plan or plan queue
    if (current_plan_.plan_id == plan_id)
    {
      bond_id_to_cancel = current_plan_.bond_id;
    }
    else
    {
      for (const auto& plan : plan_queue_)
      {
        if (plan.plan_id == plan_id)
        {
          bond_id_to_cancel = plan.bond_id;
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

    response->success = true;
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

    // Search current plan
    if (current_plan_.plan_id == request->plan_id)
    {
      response->status = status_msg(current_plan_.status);

      if (current_plan_.status == PlanStatus::VALIDATION_FAILED || current_plan_.status == PlanStatus::PARSE_FAILED)
        response->rejected_list = current_plan_.rejected_list;

      return;
    }

    // Search plan queue
    for (const auto& plan : plan_queue_)
    {
      if (plan.plan_id == request->plan_id)
      {
        response->status = status_msg(plan.status);

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
  fabric_msgs::msg::FabricStatus status_msg(PlanStatus status) const
  {
    fabric_msgs::msg::FabricStatus status_msg;
    switch (status)
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

  /** shared pointer for parsing plugin */
  std::shared_ptr<fabric::ParserBase> parsing_plugin_;

  /** shared pointer for compatibility validation plugin */
  std::shared_ptr<fabric::ValidationBase> compatibility_validation_plugin_;

  /** Bond id */
  std::string bond_id_;

  /** Manages bond between capabilities server and this client */
  std::map<std::string, std::shared_ptr<BondClient>> bond_client_cache_;

  /** File Path link */
  std::string plan_file_path_;

  /** server to set a new plan to the capabilities2 fabric */
  rclcpp::Service<SetFabricPlan>::SharedPtr plan_server_;

  /** server to cancel the current plan in the capabilities2 fabric */
  rclcpp::Service<CancelFabricPlan>::SharedPtr cancel_server_;

  /** server to get the status of the capabilities2 fabric */
  rclcpp::Service<CompleteFabric>::SharedPtr completion_server_;

  /** server to get the status of a fabric plan */
  rclcpp::Service<GetPlanStatus>::SharedPtr get_plan_status_server_;

  /** Thread to manage sending goal */
  std::thread process_thread;

};  // class Fabric

}  // namespace fabric
