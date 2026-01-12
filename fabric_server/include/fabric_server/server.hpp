#pragma once

#include <map>
#include <deque>
#include <string>
#include <thread>

#include <tinyxml2.h>
#include <rclcpp/rclcpp.hpp>
#include <rclcpp_action/rclcpp_action.hpp>
#include <pluginlib/class_loader.hpp>

#include <fabric_base/xml_helper.hpp>
#include <fabric_base/validation_base.hpp>
#include <fabric_base/parser_base.hpp>
#include <fabric_base/structs.hpp>

#include <fabric_server/capability_client.hpp>
#include <fabric_server/bond_client.hpp>

#include <fabric_msgs/srv/set_fabric_plan.hpp>
#include <fabric_msgs/srv/cancel_fabric_plan.hpp>
#include <fabric_msgs/srv/get_fabric_status.hpp>
#include <fabric_msgs/srv/complete_fabric.hpp>

namespace fabric
{

/**
 * @brief Class representing the main server of the fabric.
 *
 */
class Fabric : public rclcpp::Node
{
public:
  using GetFabricStatus = fabric_msgs::srv::GetFabricStatus;
  using SetFabricPlan = fabric_msgs::srv::SetFabricPlan;
  using CancelFabricPlan = fabric_msgs::srv::CancelFabricPlan;
  using CompleteFabric = fabric_msgs::srv::CompleteFabric;

  /**
   * @brief Construct a new Fabric object
   *
   * @param options Node options for the Fabric node
   */
  Fabric(const rclcpp::NodeOptions& options = rclcpp::NodeOptions())
    : Node("Fabric", options), validation_loader_("fabric_base", "fabric::ValidationBase"), parsing_loader_("fabric_base", "fabric::ParserBase")
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

    /*************************************************************************
     * Initialize Plugins
     ************************************************************************/

    if (use_dynamics_monitor_)
    {
      this->declare_parameter("dynamics_monitor", "supervisor::DynamicsMonitor");
      std::string dynamics_monitor_name = this->get_parameter("dynamics_monitor").as_string();

      RCLCPP_INFO(this->get_logger(), "Loading dynamics monitor plugin: %s", dynamics_monitor_name.c_str());

      dynamics_monitor_ = monitor_loader_.createSharedInstance(dynamics_monitor_name);
      dynamics_monitor_->initialize(shared_from_this());
      dynamics_monitor_->start();

      RCLCPP_INFO(this->get_logger(), "Started dynamics monitor plugin: %s", dynamics_monitor_name.c_str());
    }
    else
    {
      RCLCPP_INFO(this->get_logger(), "Dynamics monitor plugin not loaded.");
    }

    /*************************************************************************
     * Initialize Parsing Plugins
     ************************************************************************/

    this->declare_parameter("parsing_plugin", "fabric::XMLParser");
    std::string parsing_plugin_name = this->get_parameter("parsing_plugin").as_string();

    RCLCPP_INFO(this->get_logger(), "Loading parsing plugin: %s", parsing_plugin_name.c_str());

    parsing_plugin_ = parsing_loader_.createSharedInstance(parsing_plugin_name);
    parsing_plugin_->initialize(shared_from_this());

    RCLCPP_INFO(this->get_logger(), "Started parsing plugin: %s", parsing_plugin_name.c_str());

    /*************************************************************************
     * Initialize internal components
     ************************************************************************/
    capability_client_ = std::make_shared<CapabilityClient>();
    capability_client_->initialize(this->shared_from_this());

    /*************************************************************************
     * Load default plan from file
     ************************************************************************/
    tinyxml2::XMLDocument default_document;
    tinyxml2::XMLError xml_status = default_document.LoadFile(plan_file_path_.c_str());

    // check if the file loading failed
    if (xml_status != tinyxml2::XMLError::XML_SUCCESS)
    {
      RCLCPP_ERROR(this->get_logger(), "Error loading plan: %s, Error: %s", plan_file_path_.c_str(), document.ErrorName());
      rclcpp::shutdown();
    }
    RCLCPP_INFO(this->get_logger(), "Plan loaded from : %s", plan_file_path_.c_str());

    fabric::Plan default_plan;
    convert_to_string(default_document, default_plan.plan);
    plan_queue_.push_back(default_plan);

    RCLCPP_INFO(this->get_logger(), "Fabric node initialized");

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
    while (plan_queue.size() > 0)
    {
      // reset internal data structures
      reset();

      RCLCPP_INFO(this->get_logger(), "A new Fabric plan processing starting");

      // get the next plan and parse it into a XML document
      current_plan_ = plan_queue_.front();
      plan_queue_.pop_front();
      current_document_.Parse(current_plan_.plan.c_str());

      // parse the plan to extract connections
      try
      {
        parsing_plugin_->parse(current_document_, current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "Fabric plan parsing failed with error: %s", e.what());

        if (current_plan_.rejected_list.size() > 0)
        {
          RCLCPP_ERROR(this->get_logger(), "Rejected elements in the plan:");

          for (const auto& rejected_element : current_plan_.rejected_list)
            RCLCPP_ERROR(this->get_logger(), "  %s", rejected_element.c_str());
        }
        continue;
      }
      RCLCPP_INFO(this->get_logger(), "Fabric plan parsing completed successfully.");

      // get the capabilities required for the plan
      capability_client_->getInterfaces(capability_list_);
      capability_client_->getSemanticInterfaces(capability_list_);
      capability_client_->getProviders(capability_list_);

      RCLCPP_INFO(this->get_logger(), "Capability information retrieval completed successfully.");

      // Request bond from capabilities2 server
      RCLCPP_INFO(this->get_logger(), "Requesting bond from capabilities2 server.");
      current_plan_.bond_id = capability_client_->requestBond();

      // Start new bond client for the new bond id
      RCLCPP_INFO(this->get_logger(), "Establishing bond with id : %s", current_plan_.bond_id.c_str());
      bond_client_cache_[current_plan_.bond_id] = std::make_unique<BondClient>(shared_from_this(), current_plan_.bond_id);
      bond_client_cache_[current_plan_.bond_id]->start();

      // Remove old bonds if any
      if (bond_client_cache_.size() > 1)
        for (auto& [old_bond_id, bond_client] : bond_client_cache_)
          if (old_bond_id != current_plan_.bond_id)
          {
            bond_client->stop();
            RCLCPP_INFO(this->get_logger(), "Stopping and removing old bond with id : %s", old_bond_id.c_str());
          }

      RCLCPP_INFO(this->get_logger(), "Bond established with id : %s", current_plan_.bond_id.c_str());
      
      // Request use of capabilities for the plan
      RCLCPP_INFO(this->get_logger(), "Requesting use of capabilities for the plan.");

      try
      {
        capability_client_->use_capabilities(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(this->get_logger(), "Capability usage failed with error: %s", e.what());
        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      // connect the capabilities as per the plan
      RCLCPP_INFO(this->get_logger(), "Connecting capabilities as per the plan.");
      
      parsing_plugin_->connect_capabilities(current_plan_);

      RCLCPP_INFO(this->get_logger(), "Fabric processing completed. Waiting for next plan.");
    }
  }

  /**
   * @brief Callback function to set a new fabric plan.
   */
  void setPlanCallback(const std::shared_ptr<SetFabricPlan::Request> request, std::shared_ptr<SetFabricPlan::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "Received the request with a plan");

    // XML Document to check the validity of the plan
    tinyxml2::XMLDocument documentChecking;

    // try to parse the std::string plan from fabric_msgs/Plan to the to a XMLDocument file
    tinyxml2::XMLError xml_status = documentChecking.Parse(request->plan.c_str());

    // check if the file parsing failed
    if (xml_status != tinyxml2::XMLError::XML_SUCCESS)
    {
      RCLCPP_INFO(this->get_logger(), "Parsing the plan from service request message failed with error: %s", documentChecking.ErrorName());
      response->success = false;
    }
    RCLCPP_INFO(this->get_logger(), "Plan accepted from service request message");

    fabric::Plan new_plan;
    new_plan.plan = request->plan;
    plan_queue_.push_back(new_plan);

    response->success = true;
  }

  void cancelPlanCallback(const std::shared_ptr<CancelFabricPlan::Request> request, std::shared_ptr<CancelFabricPlan::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "Plan canncelling requested");

    response->success = true;
  }

  void setCompleteCallback(const std::shared_ptr<CompleteFabric::Request> request, std::shared_ptr<CompleteFabric::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "Plan completed successfully");
    completed_ = true;
    cv_.notify_all();
  }

  /**
   * @brief Reset internal data structures for processing a new plan.
   *
   */
  void reset()
  {
    current_plan_ = fabric::Plan();
    current_document_.Clear();
    current_plan_element_ = nullptr;
    capability_list_.clear();
    bond_id_.clear();
  }

  /** Vector of plans */
  std::deque<fabric::Plan> plan_queue_;

  /** Current plan being processed */
  fabric::Plan current_plan_;

  /** XML Document to hold the current plan */
  tinyxml2::XMLDocument current_document_;

  /** XML Element to hold the current plan */
  tinyxml2::XMLElement* current_plan_element_;

  /** Capability client to interact with capability server */
  std::shared_ptr<CapabilityClient> capability_client_;

  /** Capability List */
  std::vector<CapabilityInfo> capability_list_;

  /** Plugin loaders for each module package */
  pluginlib::ClassLoader<fabric::ValidationBase> validation_loader_;
  pluginlib::ClassLoader<fabric::ParserBase> parsing_loader_;

  /** shared pointer for parsing plugin */
  std::shared_ptr<fabric::ParserBase> parsing_plugin_;

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

  /** Thread to manage sending goal */
  std::thread process_thread;

};  // class Fabric

}  // namespace fabric
