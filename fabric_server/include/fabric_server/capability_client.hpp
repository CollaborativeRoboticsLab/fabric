#pragma once

#include <mutex>
#include <condition_variable>

#include <bondcpp/bond.hpp>
#include <rclcpp/rclcpp.hpp>

#include <fabric_base/structs.hpp>
#include <fabric_base/exception.hpp>
#include <fabric_base/xml_helper.hpp>

#include <capabilities2_msgs/srv/establish_bond.hpp>
#include <capabilities2_msgs/srv/get_interfaces.hpp>
#include <capabilities2_msgs/srv/get_semantic_interfaces.hpp>
#include <capabilities2_msgs/srv/get_providers.hpp>
#include <capabilities2_msgs/srv/use_capability.hpp>
#include <capabilities2_msgs/srv/free_capability.hpp>
#include <capabilities2_msgs/srv/connect_capability.hpp>
#include <capabilities2_msgs/srv/trigger_capability.hpp>

#include <capabilities2_msgs/msg/capability_event_code.hpp>

namespace fabric
{

/**
 * @brief Class for capability client interactions.
 *
 */
class CapabilityClient
{
public:
  using GetInterfaces = capabilities2_msgs::srv::GetInterfaces;
  using GetSemanticInterfaces = capabilities2_msgs::srv::GetSemanticInterfaces;
  using GetProviders = capabilities2_msgs::srv::GetProviders;
  using EstablishBond = capabilities2_msgs::srv::EstablishBond;
  using UseCapability = capabilities2_msgs::srv::UseCapability;
  using FreeCapability = capabilities2_msgs::srv::FreeCapability;
  using ConnectCapability = capabilities2_msgs::srv::ConnectCapability;
  using TriggerCapability = capabilities2_msgs::srv::TriggerCapability;

  using CapabilityEventCode = capabilities2_msgs::msg::CapabilityEventCode;

  using GetInterfacesClient = rclcpp::Client<GetInterfaces>;
  using GetSemanticInterfacesClient = rclcpp::Client<GetSemanticInterfaces>;
  using GetProvidersClient = rclcpp::Client<GetProviders>;
  using EstablishBondClient = rclcpp::Client<EstablishBond>;
  using UseCapabilityClient = rclcpp::Client<UseCapability>;
  using FreeCapabilityClient = rclcpp::Client<FreeCapability>;
  using ConnectCapabilityClient = rclcpp::Client<ConnectCapability>;
  using TriggerCapabilityClient = rclcpp::Client<TriggerCapability>;

  CapabilityClient() = default;
  virtual ~CapabilityClient() = default;

  /**
   * @brief Initialize the capability client with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    node_ = node;

    node_->declare_parameter<std::string>("capability_client.services.get_interfaces", "/capabilities/get_interfaces");
    node_->declare_parameter<std::string>("capability_client.services.get_semantic_interfaces", "/capabilities/get_semantic_interfaces");
    node_->declare_parameter<std::string>("capability_client.services.get_providers", "/capabilities/get_providers");
    node_->declare_parameter<std::string>("capability_client.services.establish_bond", "/capabilities/establish_bond");
    node_->declare_parameter<std::string>("capability_client.services.use_capability", "/capabilities/use_capability");
    node_->declare_parameter<std::string>("capability_client.services.free_capability", "/capabilities/free_capability");
    node_->declare_parameter<std::string>("capability_client.services.trigger_capability", "/capabilities/trigger_capability");
    node_->declare_parameter<std::string>("capability_client.services.configure_capability", "/capabilities/configure_capability");

    node_->get_parameter("capability_client.services.get_interfaces", get_interfaces_);
    node_->get_parameter("capability_client.services.get_semantic_interfaces", get_semantic_interfaces_);
    node_->get_parameter("capability_client.services.get_providers", get_providers_);
    node_->get_parameter("capability_client.services.establish_bond", establish_bond_);
    node_->get_parameter("capability_client.services.use_capability", use_capability_);
    node_->get_parameter("capability_client.services.free_capability", free_capability_);
    node_->get_parameter("capability_client.services.trigger_capability", trigger_capability_);
    node_->get_parameter("capability_client.services.configure_capability", configure_capability_);

    get_interfaces_client_ = this->create_client<GetInterfaces>(get_interfaces_);
    get_sem_interf_client_ = this->create_client<GetSemanticInterfaces>(get_semantic_interfaces_);
    get_providers_client_ = this->create_client<GetProviders>(get_providers_);
    establish_bond_client_ = this->create_client<EstablishBond>(establish_bond_);
    use_capability_client_ = this->create_client<UseCapability>(use_capability_);
    free_capability_client_ = this->create_client<FreeCapability>(free_capability_);
    trig_capability_client_ = this->create_client<TriggerCapability>(trigger_capability_);
    connect_capability_client_ = this->create_client<ConnectCapability>(connect_capability_);

    // Wait for services to become available
    check_service(!get_interfaces_client_->wait_for_service(std::chrono::seconds(1)), get_interfaces_);
    check_service(!get_sem_interf_client_->wait_for_service(std::chrono::seconds(1)), get_semantic_interfaces_);
    check_service(!get_providers_client_->wait_for_service(std::chrono::seconds(1)), get_providers_);
    check_service(!establish_bond_client_->wait_for_service(std::chrono::seconds(1)), establish_bond_);
    check_service(!use_capability_client_->wait_for_service(std::chrono::seconds(1)), use_capability_);
    check_service(!free_capability_client_->wait_for_service(std::chrono::seconds(1)), free_capability_);
    check_service(!trig_capability_client_->wait_for_service(std::chrono::seconds(1)), trigger_capability_);
    check_service(!conf_capability_client_->wait_for_service(std::chrono::seconds(1)), configure_capability_);

    RCLCPP_INFO(node_->get_logger(), "Capability client initialized.");
  }

  /**
   * @brief Get Interfaces available in the capabilities2 server via relavant service
   *
   * @param interfaces Vector to store the retrieved interfaces
   * @return true if successful, false otherwise
   */
  bool getInterfaces(std::vector<CapabilityInfo>& capabilities)
  {
    RCLCPP_INFO(node_->get_logger(), "Requesting Interface information");

    auto request_interface = std::make_shared<GetInterfaces::Request>();

    bool success = false;
    bool completed = false;
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(mtx);

    // request data from the server
    auto result_future = get_interfaces_client_->async_send_request(
        request_interface, [this, &completed, &success, &cv, &capabilities](GetInterfacesClient::SharedFuture future) {
          if (!future.valid())
          {
            success = false;
            completed = true;
            RCLCPP_INFO(node_->get_logger(), "Failed to get Interface information from server");
            return;
          }

          auto response = future.get();

          for (const auto& interface : response->interfaces)
          {
            CapabilityInfo info;
            info.interface = interface;
            info.is_semantic = false;

            capabilities.push_back(info);
          }

          success = true;
          completed = true;
          cv.notify_all();
        });

    // wait for the response
    cv.wait(lock, [&completed]() { return completed; });

    RCLCPP_INFO(node_->get_logger(), "Received Interface information for %d interfaces from server", static_cast<int>(capabilities.size()));

    return success;
  }

  /**
   * @brief Get the Semantic Interfaces from the capabilities2 server via related service client
   *
   * @param interfaces std::vector of interfaces for which the semantic interfaces will be requested
   */
  void getSemanticInterfaces(std::vector<CapabilityInfo>& capabilities)
  {
    int interface_count = capabilities.size();

    bool success = false;
    std::vector<CapabilityInfo> new_capabilities;

    for (auto& capability : capabilities)
    {
      RCLCPP_INFO(node_->get_logger(), "Checking if interface %s has semantic interfaces", capability.interface.c_str());

      auto request_semantic = std::make_shared<GetSemanticInterfaces::Request>();
      request_semantic->interface = capability.interface;

      bool completed = false;
      std::mutex mtx;
      std::condition_variable cv;
      std::unique_lock<std::mutex> lock(mtx);

      // request semantic interface from the server
      auto result_semantic_future = get_sem_interf_client_->async_send_request(
          request_semantic, [this, &new_capabilities, &success, &completed, &cv](GetSemanticInterfacesClient::SharedFuture future) {
            if (!future.valid())
            {
              success = false;
              completed = true;
              RCLCPP_INFO(node_->get_logger(), "Failed to get SemanticInterface information from server");
              return;
            }

            auto response = future.get();
            semantic_interfaces = ;

            // add semantic interfaces to the capability info
            for (const auto& interface : response->semantic_interfaces)
            {
              CapabilityInfo info;
              info.interface = interface;
              info.is_semantic = true;

              new_capabilities.push_back(info);
              RCLCPP_INFO(node_->get_logger(), "  Semantic interface: %s added", interface.c_str());
            }

            success = true;
            completed = true;
            cv.notify_all();
          });

      // wait for the response
      cv.wait(lock, [&completed]() { return completed; });
    }

    // append the new semantic interfaces to the original capabilities list
    capabilities.insert(capabilities.end(), new_capabilities.begin(), new_capabilities.end());

    return success;
  }

  /**
   * @brief Get the Provider information for the related interfaces
   *
   * @param capabilities std::vector of CapabilityInfo for which the provider information will be requested
   * @return true if successful, false otherwise
   */
  void getProvider(std::vector<CapabilityInfo>& capabilities)
  {
    bool success = false;

    for (auto& capability : capabilities)
    {
      RCLCPP_INFO(node_->get_logger(), "Requesting provider for %s", capability.interface.c_str());

      auto request_providers = std::make_shared<GetProviders::Request>();

      // request providers of the semantic interface
      request_providers->interface = capability.interface;
      request_providers->include_semantic = capability.is_semantic;

      bool completed = false;
      std::mutex mtx;
      std::condition_variable cv;
      std::unique_lock<std::mutex> lock(mtx);

      auto result_providers_future = get_providers_client_->async_send_request(
          request_providers, [this, &capability, &completed, &success, &cv](GetProvidersClient::SharedFuture future) {
            if (!future.valid())
            {
              success = false;
              completed = true;
              RCLCPP_INFO(node_->get_logger(), "Failed to get Provider information from server");
              return;
            }

            auto response = future.get();
            capability.alt_providers = response->providers;
            capability.provider = response->default_provider;
            success = true;
            completed = true;
            cv.notify_all();
          });

      // wait for the response
      cv.wait(lock, [&completed]() { return completed; });

      RCLCPP_INFO(node_->get_logger(), "Received provider information for %s: default provider: %s, number of alternative providers: %d",
                  capability.interface.c_str(), capability.provider.c_str(), static_cast<int>(capability.alt_providers.size()));

      return success;
    }
  }

  /**
   * @brief Request the bond from the capabilities2 server
   *
   */
  std::string request_bond()
  {
    RCLCPP_INFO(node_->get_logger(), "Requesting bond id");

    // create bond establishing server request
    auto request_bond = std::make_shared<EstablishBond::Request>();

    std::string bond_id;
    bool completed = false;
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(mtx);

    // send the request
    auto result_future =
        establish_bond_client_->async_send_request(request_bond, [&bond_id, &completed, &cv, this](EstablishBondClient::SharedFuture future) {
          if (!future.valid())
          {
            RCLCPP_ERROR(node_->get_logger(), "Failed to retrieve the bond id.");
            return;
          }

          auto response = future.get();
          bond_id = response->bond_id;
          RCLCPP_INFO(node_->get_logger(), "Received the bond id : %s", bond_id_.c_str());

          completed = true;
          cv.notify_all();
        });

    // wait for the response
    cv.wait(lock, [&completed]() { return completed; });

    return bond_id;
  }

  /**
   * @brief Request use of capability from capabilities2 server
   *
   * @param plan Fabric plan containing the bond id and capabilities to be used
   *
   * @throws fabric::fabric_exception if any capability fails to start
   */
  void use_capabilities(fabric::Plan& plan)
  {
    int index = 0;

    for (const auto& [id, connection] : plan.connections)
    {
      index++;

      auto request_use = std::make_shared<UseCapability::Request>();
      request_use->capability = connection.source.interface;
      request_use->preferred_provider = connection.source.provider;
      request_use->bond_id = plan.bond_id;

      RCLCPP_INFO(node_->get_logger(), "Starting capability %d : %s", index, connection.source.interface.c_str());

      bool success = false;
      bool completed = false;
      std::mutex mtx;
      std::condition_variable cv;
      std::unique_lock<std::mutex> lock(mtx);

      auto result_future =
          use_capability_client_->async_send_request(request_use, [this, &completed, &success, &cv](UseCapabilityClient::SharedFuture future) {
            if (!future.valid())
            {
              success = false;
              completed = true;
              return;
            }

            auto response = future.get();
            success = true;
            completed = true;
            cv.notify_all();
          });

      // wait for the response
      cv.wait(lock, [&completed]() { return completed; });

      if (success)
      {
        RCLCPP_INFO(node_->get_logger(), "Capability %s started successfully.", connection.source.interface.c_str());
        started_capabilities_.push_back(connection.source.interface);
      }
      else
        throw fabric::fabric_exception("Failed to start capability " + connection.source.interface);
    }
  }

  /**
   * @brief Request free of all started capabilities from capabilities2 server
   *
   * @param plan Fabric plan containing the bond id and capabilities to be freed
   *
   */
  void free_capabilities(fabric::Plan& plan)
  {
    // track freed capabilities
    std::vector<std::string> freed_capabilities;

    // prepare to free all started capabilities
    for (const auto& capability : started_capabilities_)
    {
      // check if the capability is part of the plan connections
      bool found = false;
      for (const auto& [id, connection] : plan.connections)
      {
        if (connection.source.interface == capability)
        {
          found = true;
          break;
        }
      }

      bool completed = false;
      bool success = false;
      std::mutex mtx;
      std::condition_variable cv;
      std::unique_lock<std::mutex> lock(mtx);

      if (found)
      {
        // create free capability request
        auto request_free = std::make_shared<FreeCapability::Request>();
        request_free->capability = capability;
        request_free->bond_id = plan.bond_id;

        // send the request
        auto result_future = free_capability_client_->async_send_request(
            request_free, [this, &completed, &success, &cv, &capability](FreeCapabilityClient::SharedFuture future) {
              if (!future.valid())
              {
                RCLCPP_ERROR(node_->get_logger(), "Failed to free capability %s", capability.c_str());
                completed = true;
                success = false;
                cv.notify_all();
                return;
              }

              auto response = future.get();
              RCLCPP_INFO(node_->get_logger(), "Capability %s freed successfully.", capability.c_str());
              completed = true;
              success = true;
              cv.notify_all();
            });

        // wait for the response
        cv.wait(lock, [&completed]() { return completed; });

        // track successfully freed capabilities
        if (success)
          freed_capabilities.push_back(capability);
      }
      else
      {
        RCLCPP_WARN(node_->get_logger(), "Capability %s was not started as part of the plan, skipping free request.", capability.c_str());
      }
    }

    // remove freed capabilities from started list
    for (const auto& capability : freed_capabilities)
      started_capabilities_.erase(std::remove(started_capabilities_.begin(), started_capabilities_.end(), capability), started_capabilities_.end());
  }

  /**
   * @brief Request connection between capabilities from according to the provided plan
   */
  void configure_capabilities(fabric::Plan& plan)
  {
    for (const auto& [id, connection] : plan.connections)
    {
      auto request = std::make_shared<ConnectCapability::Request>();

      RCLCPP_INFO(node_->get_logger(), "Configuring connection for capability named %s", connection.source.interface.c_str());

      if (connection.on_start.exists())
      {
        request->bond_id = plan.bond_id;
        request->event.trigger_id = connection.trigger_id;
        request->event.description = connection.description;

        request->event.connection.type.code = CapabilityEventCode::STARTED;

        request->event.connection.source.capability = connection.source.interface;
        request->event.connection.source.provider = connection.source.provider;
        request->event.connection.source.parameters = connection.source.parameter_to_string();

        request->event.connection.target.capability = connection.on_start.interface;
        request->event.connection.target.provider = connection.on_start.provider;
        request->event.connection.target.parameters = connection.on_start.parameter_to_string();
      }
    }

    std::string source_capability = capabilities[completed_configurations_].source.runner;

    // send the request
    auto result_future =
        conf_capability_client_->async_send_request(request, [this, source_capability](ConfigureCapabilityClient::SharedFuture future) {
          if (!future.valid())
          {
            result_msg->success = false;
            result_msg->message = "Failed to configure capability :" + source_capability + ". Server execution cancelled";
            event_->error(result_msg->message);
            goal_handle_->abort(result_msg);
            return;
          }

          completed_configurations_++;

          auto response = future.get();

          event_->info(std::to_string(completed_configurations_) + "/" + std::to_string(expected_configurations_) +
                       " : Successfully configured capability : " + source_capability);

          // Check if all expected calls are completed before calling verify_plan
          if (completed_configurations_ == expected_configurations_)
          {
            event_->info("All requested capabilities have been configured. Triggering the first capability");

            trigger_first_node();
          }
          else
          {
            configure_capabilities(connection_map);
          }
        });
  }

protected:
  /**
   * @brief Wait for a service to become available.
   */
  void wait_for_service(bool wait_for_logic, const std::string& service_name)
  {
    while (wait_for_logic)
    {
      RCLCPP_INFO(node_->get_logger(), "%s is not available", service_name.c_str());
      rclcpp::shutdown();
      return;
    }
    RCLCPP_INFO(node_->get_logger(), "%s connected", service_name.c_str());
  }

  /**
   * @brief pointer to the ROS2 node
   */
  rclcpp::Node::SharedPtr node_;

  /**
   * @brief service names
   */
  std::string get_interfaces_;
  std::string get_semantic_interfaces_;
  std::string get_providers_;
  std::string establish_bond_;
  std::string use_capability_;
  std::string free_capability_;
  std::string trigger_capability_;
  std::string configure_capability_;

  /**
   * @brief Heart beat bond with capabilities server
   */
  std::shared_ptr<bond::Bond> bond_;

  std::vector<std::string> started_capabilities_;

  /** Get interfaces from capabilities server */
  GetInterfacesClient::SharedPtr get_interfaces_client_;

  /** Get semantic interfaces from capabilities server */
  GetSemanticInterfacesClient::SharedPtr get_sem_interf_client_;

  /** Get providers from capabilities server */
  GetProvidersClient::SharedPtr get_providers_client_;

  /** establish bond */
  EstablishBondClient::SharedPtr establish_bond_client_;

  /** use an selected capability */
  UseCapabilityClient::SharedPtr use_capability_client_;

  /** free an selected capability */
  FreeCapabilityClient::SharedPtr free_capability_client_;

  /** configure an selected capability */
  ConnectCapabilityClient::SharedPtr connect_capability_client_;

  /** trigger an selected capability */
  TriggerCapabilityClient::SharedPtr trig_capability_client_;
};
}  // namespace fabric