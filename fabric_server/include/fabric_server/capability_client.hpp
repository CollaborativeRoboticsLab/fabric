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
    node_->declare_parameter<std::string>("capability_client.services.connect_capability", "/capabilities/connect_capability");

    node_->get_parameter("capability_client.services.get_interfaces", get_interfaces_);
    node_->get_parameter("capability_client.services.get_semantic_interfaces", get_semantic_interfaces_);
    node_->get_parameter("capability_client.services.get_providers", get_providers_);
    node_->get_parameter("capability_client.services.establish_bond", establish_bond_);
    node_->get_parameter("capability_client.services.use_capability", use_capability_);
    node_->get_parameter("capability_client.services.free_capability", free_capability_);
    node_->get_parameter("capability_client.services.trigger_capability", trigger_capability_);
    node_->get_parameter("capability_client.services.configure_capability", configure_capability_);
    node_->get_parameter("capability_client.services.connect_capability", connect_capability_);

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
   * @brief Get the Interfaces from the capabilities2 server via related service client.
   * @throws `fabric::fabric_exception` if the service call fails
   *
   * @param interfaces std::vector of interfaces for which the information will be requested
   *
   */
  void getInterfaces(std::vector<CapabilityInfo>& capabilities)
  {
    RCLCPP_INFO(node_->get_logger(), "Requesting Interface information");

    auto request_interface = std::make_shared<GetInterfaces::Request>();

    bool completed = false;
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(mtx);

    // request data from the server
    auto result_future = get_interfaces_client_->async_send_request(request_interface,
                                                                    [this, &completed, &cv, &capabilities](GetInterfacesClient::SharedFuture future) {
                                                                      if (!future.valid())
                                                                      {
                                                                        throw fabric::fabric_exception("Failed to get Interface information from "
                                                                                                       "server");
                                                                      }

                                                                      auto response = future.get();

                                                                      for (const auto& interface : response->interfaces)
                                                                      {
                                                                        CapabilityInfo info;
                                                                        info.interface = interface;
                                                                        info.is_semantic = false;

                                                                        capabilities.push_back(info);
                                                                      }

                                                                      completed = true;
                                                                      cv.notify_all();
                                                                    });

    // wait for the response
    cv.wait(lock, [&completed]() { return completed; });

    RCLCPP_INFO(node_->get_logger(), "Received Interface information for %d interfaces from server", static_cast<int>(capabilities.size()));
  }

  /**
   * @brief Get the Semantic Interfaces from the capabilities2 server via related service client.
   * @throws `fabric::fabric_exception` if the service call fails
   *
   * @param interfaces std::vector of interfaces for which the semantic interfaces will be requested
   */
  void getSemanticInterfaces(std::vector<CapabilityInfo>& capabilities)
  {
    int interface_count = capabilities.size();

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
          request_semantic, [this, &new_capabilities, &completed, &cv](GetSemanticInterfacesClient::SharedFuture future) {
            if (!future.valid())
            {
              throw fabric::fabric_exception("Failed to get Semantic Interface information from server");
            }

            auto response = future.get();

            // add semantic interfaces to the capability info
            for (const auto& interface : response->semantic_interfaces)
            {
              CapabilityInfo info;
              info.interface = interface;
              info.is_semantic = true;

              new_capabilities.push_back(info);
              RCLCPP_INFO(node_->get_logger(), "  Semantic interface: %s added", interface.c_str());
            }

            completed = true;
            cv.notify_all();
          });

      // wait for the response
      cv.wait(lock, [&completed]() { return completed; });
    }

    // append the new semantic interfaces to the original capabilities list
    capabilities.insert(capabilities.end(), new_capabilities.begin(), new_capabilities.end());
  }

  /**
   * @brief Get the Provider information for the related interfaces. T
   * @throws `fabric::fabric_exception` if the service call fails
   *
   * @param capabilities std::vector of CapabilityInfo for which the provider information will be requested
   *
   */
  void getProvider(std::vector<CapabilityInfo>& capabilities)
  {
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
              throw fabric::fabric_exception("Failed to get Provider information from server");
            }

            auto response = future.get();
            capability.alt_providers = response->providers;
            capability.provider = response->default_provider;
            completed = true;
            cv.notify_all();
          });

      // wait for the response
      cv.wait(lock, [&completed]() { return completed; });

      RCLCPP_INFO(node_->get_logger(), "Received provider information for %s: default provider: %s, number of alternative providers: %d",
                  capability.interface.c_str(), capability.provider.c_str(), static_cast<int>(capability.alt_providers.size()));
    }
  }

  /**
   * @brief Request the bond from the capabilities2 server.
   * @throws `fabric::fabric_exception` if the service call fails
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
            throw fabric::fabric_exception("Failed to establish bond with capabilities2 server");
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
   * @throws fabric::fabric_exception if any capability fails to start
   *
   * @param plan Fabric plan containing the bond id and capabilities to be used
   *
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

      bool completed = false;
      std::mutex mtx;
      std::condition_variable cv;
      std::unique_lock<std::mutex> lock(mtx);

      auto result_future = use_capability_client_->async_send_request(request_use, [this, &completed, &cv](UseCapabilityClient::SharedFuture future) {
        if (!future.valid())
        {
          throw fabric::fabric_exception("Failed to use capability");
        }

        auto response = future.get();
        completed = true;
        cv.notify_all();
      });

      // wait for the response
      cv.wait(lock, [&completed]() { return completed; });

      RCLCPP_INFO(node_->get_logger(), "Capability %s started successfully.", connection.source.interface.c_str());
      started_capabilities_.push_back(connection.source.interface);
    }
  }

  /**
   * @brief Request free of all started capabilities from capabilities2 server
   * @throws fabric::fabric_exception if any capability fails to free
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
            request_free, [this, &completed, &cv, &capability](FreeCapabilityClient::SharedFuture future) {
              if (!future.valid())
              {
                throw fabric::fabric_exception("Failed to free capability " + capability);
              }

              auto response = future.get();
              RCLCPP_INFO(node_->get_logger(), "Capability %s freed successfully.", capability.c_str());
              completed = true;
              cv.notify_all();
            });

        // wait for the response
        cv.wait(lock, [&completed]() { return completed; });

        // track successfully freed capabilities
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
   * @throws fabric::fabric_exception if any connection fails
   * 
   * @param plan Fabric plan containing the bond id and connections to be established
   */
  void connect_capabilities(fabric::Plan& plan)
  {
    for (const auto& [id, connection] : plan.connections)
    {
      RCLCPP_INFO(node_->get_logger(), "Configuring connection for capability named %s", connection.source.interface.c_str());

      if (connection.on_start.exists())
      {
        auto request = std::make_shared<ConnectCapability::Request>();

        request->bond_id = plan.bond_id;
        request->trigger_id = connection.trigger_id;

        request->connection.type.code = CapabilityEventCode::STARTED;

        request->connection.source.capability = connection.source.interface;
        request->connection.source.provider = connection.source.provider;
        request->connection.source.parameters = connection.source.parameter_to_string();

        request->connection.target.capability = connection.on_start.interface;
        request->connection.target.provider = connection.on_start.provider;
        request->connection.target.parameters = connection.on_start.parameter_to_string();

        bool completed = false;
        std::mutex mtx;
        std::condition_variable cv;
        std::unique_lock<std::mutex> lock(mtx);

        // send the request
        auto result_future =
            conf_capability_client_->async_send_request(request, [this, &completed, &cv](ConfigureCapabilityClient::SharedFuture future) {
              if (!future.valid())
              {
                throw fabric::fabric_exception("Failed to configure capability connection on STARTED event");
              }

              auto response = future.get();

              RCLCPP_INFO(node_->get_logger(), "Capability connection on STARTED event configured successfully.");
              completed = true;
              cv.notify_all();
            });

        // wait for the response
        cv.wait(lock, [&completed]() { return completed; });
      }

      if (connection.on_stop.exists())
      {
        auto request = std::make_shared<ConnectCapability::Request>();

        request->bond_id = plan.bond_id;
        request->trigger_id = connection.trigger_id;

        request->connection.type.code = CapabilityEventCode::STOPPED;

        request->connection.source.capability = connection.source.interface;
        request->connection.source.provider = connection.source.provider;
        request->connection.source.parameters = connection.source.parameter_to_string();

        request->connection.target.capability = connection.on_stop.interface;
        request->connection.target.provider = connection.on_stop.provider;
        request->connection.target.parameters = connection.on_stop.parameter_to_string();

        bool completed = false;
        std::mutex mtx;
        std::condition_variable cv;
        std::unique_lock<std::mutex> lock(mtx);

        // send the request
        auto result_future =
            conf_capability_client_->async_send_request(request, [this, &completed, &cv](ConfigureCapabilityClient::SharedFuture future) {
              if (!future.valid())
              {
                throw fabric::fabric_exception("Failed to configure capability connection on STOPPED event");
              }

              auto response = future.get();

              RCLCPP_INFO(node_->get_logger(), "Capability connection on STOPPED event configured successfully.");
              completed = true;
              cv.notify_all();
            });

        // wait for the response
        cv.wait(lock, [&completed]() { return completed; });
      }

      if (connection.on_success.exists())
      {
        auto request = std::make_shared<ConnectCapability::Request>();

        request->bond_id = plan.bond_id;
        request->trigger_id = connection.trigger_id;

        request->connection.type.code = CapabilityEventCode::SUCCEEDED;

        request->connection.source.capability = connection.source.interface;
        request->connection.source.provider = connection.source.provider;
        request->connection.source.parameters = connection.source.parameter_to_string();

        request->connection.target.capability = connection.on_success.interface;
        request->connection.target.provider = connection.on_success.provider;
        request->connection.target.parameters = connection.on_success.parameter_to_string();

        bool completed = false;
        std::mutex mtx;
        std::condition_variable cv;
        std::unique_lock<std::mutex> lock(mtx);

        // send the request
        auto result_future =
            conf_capability_client_->async_send_request(request, [this, &completed, &cv](ConfigureCapabilityClient::SharedFuture future) {
              if (!future.valid())
              {
                throw fabric::fabric_exception("Failed to configure capability connection on SUCCEEDED event");
              }

              auto response = future.get();

              RCLCPP_INFO(node_->get_logger(), "Capability connection on SUCCEEDED event configured successfully.");
              completed = true;
              cv.notify_all();
            });

        // wait for the response
        cv.wait(lock, [&completed]() { return completed; });
      }

      if (connection.on_failure.exists())
      {
        auto request = std::make_shared<ConnectCapability::Request>();

        request->bond_id = plan.bond_id;
        request->trigger_id = connection.trigger_id;

        request->connection.type.code = CapabilityEventCode::FAILED;

        request->connection.source.capability = connection.source.interface;
        request->connection.source.provider = connection.source.provider;
        request->connection.source.parameters = connection.source.parameter_to_string();

        request->connection.target.capability = connection.on_failure.interface;
        request->connection.target.provider = connection.on_failure.provider;
        request->connection.target.parameters = connection.on_failure.parameter_to_string();

        bool completed = false;
        std::mutex mtx;
        std::condition_variable cv;
        std::unique_lock<std::mutex> lock(mtx);

        // send the request
        auto result_future =
            conf_capability_client_->async_send_request(request, [this, &completed, &cv](ConfigureCapabilityClient::SharedFuture future) {
              if (!future.valid())
              {
                throw fabric::fabric_exception("Failed to configure capability connection on FAILED event");
              }

              auto response = future.get();

              RCLCPP_INFO(node_->get_logger(), "Capability connection on FAILED event configured successfully.");
              completed = true;
              cv.notify_all();
            });

        // wait for the response
        cv.wait(lock, [&completed]() { return completed; });
      }
    }
  }

  /**
   * @brief Trigger the first node
   * @throws fabric::fabric_exception if the first capability fails to trigger
   * 
   * @param plan Fabric plan containing the bond id and connections to be triggered
   */
  void trigger_first_node(fabric::Plan& plan)
  {
    auto request_trigger = std::make_shared<TriggerCapability::Request>();

    bool completed = false;
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(mtx);

    request_trigger->bond_id = plan.bond_id;
    request_trigger->capability = plan.connections[0].source.interface;
    request_trigger->parameters = plan.connections[0].source.parameter_to_string();

    // send the request
    auto result_future = trig_capability_client_->async_send_request(request_trigger, [this](TriggerCapabilityClient::SharedFuture future) {
      if (!future.valid())
      {
        throw fabric::fabric_exception("Failed to trigger the first capability.");
      }

      auto response = future.get();
      completed = true;
      cv.notify_all();
      RCLCPP_INFO(node_->get_logger(), "First capability triggered successfully.");
    });

    // wait for the response
    cv.wait(lock, [&completed]() { return completed; });
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
  std::string connect_capability_;

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