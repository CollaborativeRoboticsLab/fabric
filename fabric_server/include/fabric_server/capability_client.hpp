#pragma once

#include <mutex>
#include <condition_variable>

#include <bondcpp/bond.hpp>
#include <rclcpp/rclcpp.hpp>

#include <fabric_base/structs.hpp>

#include <capabilities2_msgs/srv/establish_bond.hpp>
#include <capabilities2_msgs/srv/get_interfaces.hpp>
#include <capabilities2_msgs/srv/get_semantic_interfaces.hpp>
#include <capabilities2_msgs/srv/get_providers.hpp>
#include <capabilities2_msgs/srv/use_capability.hpp>
#include <capabilities2_msgs/srv/free_capability.hpp>
#include <capabilities2_msgs/srv/configure_capability.hpp>
#include <capabilities2_msgs/srv/trigger_capability.hpp>

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
  using ConfigureCapability = capabilities2_msgs::srv::ConfigureCapability;
  using TriggerCapability = capabilities2_msgs::srv::TriggerCapability;

  using GetInterfacesClient = rclcpp::Client<GetInterfaces>;
  using GetSemanticInterfacesClient = rclcpp::Client<GetSemanticInterfaces>;
  using GetProvidersClient = rclcpp::Client<GetProviders>;
  using EstablishBondClient = rclcpp::Client<EstablishBond>;
  using UseCapabilityClient = rclcpp::Client<UseCapability>;
  using FreeCapabilityClient = rclcpp::Client<FreeCapability>;
  using ConfigureCapabilityClient = rclcpp::Client<ConfigureCapability>;
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
    conf_capability_client_ = this->create_client<ConfigureCapability>(configure_capability_);

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
    std::vector<std::string> interfaces;

    // request data from the server
    auto result_future = get_interfaces_client_->async_send_request(
        request_interface, [this, &completed, &success, &cv, &interfaces](GetInterfacesClient::SharedFuture future) {
          if (!future.valid())
          {
            success = false;
            completed = true;
            RCLCPP_INFO(node_->get_logger(), "Failed to get Interface information from server");
            return;
          }

          auto response = future.get();
          interfaces = response->interfaces;
          success = true;
          completed = true;
          cv.notify_all();
        });

    // wait for the response
    cv.wait(lock, [&completed]() { return completed; });
    RCLCPP_INFO(node_->get_logger(), "Received Interface information for %d interfaces from server", static_cast<int>(interfaces.size()));

    for (const auto& interface_info : interfaces)
    {
      CapabilityInfo info;
      info.interface = interface_info.interface;

      capabilities.push_back(info);
    }

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

    for (int i = 0; i < interface_count; i++)
    {
      RCLCPP_INFO(node_->get_logger(), "Checking if interface %s has semantic interfaces", capabilities[i].interface.c_str());

      auto request_semantic = std::make_shared<GetSemanticInterfaces::Request>();
      request_semantic->interface = capabilities[i].interface;

      bool completed = false;
      std::mutex mtx;
      std::condition_variable cv;
      std::unique_lock<std::mutex> lock(mtx);
      std::vector<std::string> semantic_interfaces;

      // request semantic interface from the server
      auto result_semantic_future = get_sem_interf_client_->async_send_request(
          request_semantic, [this, &semantic_interfaces, &success, &completed, &cv](GetSemanticInterfacesClient::SharedFuture future) {
            if (!future.valid())
            {
              success = false;
              completed = true;
              RCLCPP_INFO(node_->get_logger(), "Failed to get SemanticInterface information from server");
              return;
            }

            auto response = future.get();
            semantic_interfaces = response->semantic_interfaces;
            success = true;
            completed = true;
            cv.notify_all();
          });

      // wait for the response
      cv.wait(lock, [&completed]() { return completed; });

      if (semantic_interfaces.size() > 0)
      {
        RCLCPP_INFO(node_->get_logger(), "Interface %s has %d semantic interfaces", capabilities[i].interface.c_str(),
                    static_cast<int>(semantic_interfaces.size()));

        capabilities[i].has_semantic = true;
        capabilities[i].semantic_interfaces = semantic_interfaces;
      }
      else
      {
        RCLCPP_INFO(node_->get_logger(), "Interface %s has no semantic interfaces", capabilities[i].interface.c_str());

        capabilities[i].has_semantic = false;
      }
    }

    return success;
  }

  /**
   * @brief Get the Provider information for the related interfaces
   *
   * @param interfaces std::vector of interfaces
   * @param is_semantic std::vector of masks about interfaces with true value for semantic interfaces
   */
  void getProvider(std::vector<CapabilityInfo>& capabilities)
  {
    bool success = false;

    for (size_t i = 0; i < capabilities.size(); ++i)
    {
      RCLCPP_INFO(node_->get_logger(), "Requesting provider for %s", capabilities[i].interface.c_str());

      auto request_providers = std::make_shared<GetProviders::Request>();

      // request providers of the semantic interface
      request_providers->interface = capabilities[i].interface;
      ;
      request_providers->include_semantic = capabilities[i].has_semantic;

      bool completed = false;
      std::mutex mtx;
      std::condition_variable cv;
      std::unique_lock<std::mutex> lock(mtx);
      std::vector<std::string> providers;
      std::string default_provider;

      auto result_providers_future = get_providers_client_->async_send_request(
          request_providers, [this, &providers, &default_provider, &completed, &success, &cv](GetProvidersClient::SharedFuture future) {
            if (!future.valid())
            {
              success = false;
              completed = true;
              RCLCPP_INFO(node_->get_logger(), "Failed to get Provider information from server");
              return;
            }

            auto response = future.get();
            providers = response->providers;
            default_provider = response->default_provider;
            success = true;
            completed = true;
            cv.notify_all();
          });

      // wait for the response
      cv.wait(lock, [&completed]() { return completed; });

      capabilities[i].provider = default_provider;
      capabilities[i].alt_providers = providers;
      RCLCPP_INFO(node_->get_logger(), "Received provider information for %s: default provider: %s, number of alternative providers: %d",
                  capabilities[i].interface.c_str(), default_provider.c_str(), static_cast<int>(providers.size()));

      return success;
    }
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
   * @brief service mutexs
   */
  std::mutex mutex_get_interfaces_;
  std::mutex mutex_get_semantic_interfaces_;
  std::mutex mutex_get_providers_;
  std::mutex mutex_establish_bond_;
  std::mutex mutex_use_capability_;
  std::mutex mutex_free_capability_;
  std::mutex mutex_trigger_capability_;
  std::mutex mutex_configure_capability_;

  /**
   * @brief service conditional variables
   */
  std::condition_variable cv_get_interfaces_;
  std::condition_variable cv_get_semantic_interfaces_;
  std::condition_variable cv_get_providers_;
  std::condition_variable cv_establish_bond_;
  std::condition_variable cv_use_capability_;
  std::condition_variable cv_free_capability_;
  std::condition_variable cv_trigger_capability_;
  std::condition_variable cv_configure_capability_;

  /**
   * @brief Heart beat bond with capabilities server
   */
  std::shared_ptr<bond::Bond> bond_;

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
  ConfigureCapabilityClient::SharedPtr conf_capability_client_;

  /** trigger an selected capability */
  TriggerCapabilityClient::SharedPtr trig_capability_client_;
};
}  // namespace fabric