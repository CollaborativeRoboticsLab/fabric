#pragma once

#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <unordered_map>

#include <bondcpp/bond.hpp>
#include <rclcpp/rclcpp.hpp>
#include <yaml-cpp/yaml.h>

#include <fabric_base/utils/structs.hpp>
#include <fabric_base/utils/exception.hpp>
#include <fabric_base/utils/xml_helper.hpp>

#include <capabilities2_msgs/srv/establish_bond.hpp>
#include <capabilities2_msgs/srv/get_interfaces.hpp>
#include <capabilities2_msgs/srv/get_semantic_interfaces.hpp>
#include <capabilities2_msgs/srv/get_providers.hpp>
#include <capabilities2_msgs/srv/get_runnable_specs.hpp>
#include <capabilities2_msgs/srv/use_capability.hpp>
#include <capabilities2_msgs/srv/free_capability.hpp>
#include <capabilities2_msgs/srv/connect_capability.hpp>
#include <capabilities2_msgs/srv/trigger_capability.hpp>

#include <capabilities2_msgs/msg/capability_event_code.hpp>
#include <capabilities2_msgs/msg/capability_parameter.hpp>

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
  using GetRunnableSpecs = capabilities2_msgs::srv::GetRunnableSpecs;
  using EstablishBond = capabilities2_msgs::srv::EstablishBond;
  using UseCapability = capabilities2_msgs::srv::UseCapability;
  using FreeCapability = capabilities2_msgs::srv::FreeCapability;
  using ConnectCapability = capabilities2_msgs::srv::ConnectCapability;
  using TriggerCapability = capabilities2_msgs::srv::TriggerCapability;

  using CapabilityEventCode = capabilities2_msgs::msg::CapabilityEventCode;

  using GetInterfacesClient = rclcpp::Client<GetInterfaces>;
  using GetSemanticInterfacesClient = rclcpp::Client<GetSemanticInterfaces>;
  using GetProvidersClient = rclcpp::Client<GetProviders>;
  using GetRunnableSpecsClient = rclcpp::Client<GetRunnableSpecs>;
  using EstablishBondClient = rclcpp::Client<EstablishBond>;
  using UseCapabilityClient = rclcpp::Client<UseCapability>;
  using FreeCapabilityClient = rclcpp::Client<FreeCapability>;
  using ConnectCapabilityClient = rclcpp::Client<ConnectCapability>;
  using TriggerCapabilityClient = rclcpp::Client<TriggerCapability>;

  CapabilityClient() {

  };

  virtual ~CapabilityClient() = default;

  static capabilities2_msgs::msg::CapabilityParameter to_capability_parameter_msg(const fabric::Parameter& parameter)
  {
    capabilities2_msgs::msg::CapabilityParameter msg;
    msg.key = parameter.key;
    msg.value = parameter.value;
    msg.type = static_cast<int>(parameter.type);
    return msg;
  }

  static capabilities2_msgs::msg::Capability to_capability_msg(const fabric::EventParameters& parameters)
  {
    capabilities2_msgs::msg::Capability msg;
    for (const auto& option : parameters.options)
      msg.parameters.push_back(to_capability_parameter_msg(option));
    return msg;
  }

  /**
   * @brief Initialize the capability client with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    node_ = node;

    if (!node_->has_parameter("capability_client.service_wait_timeout_sec"))
      node_->declare_parameter<int>("capability_client.service_wait_timeout_sec", -1);
    service_wait_timeout_sec_ = node_->get_parameter("capability_client.service_wait_timeout_sec").as_int();

    node_->declare_parameter<std::string>("capability_client.services.get_interfaces", "/capabilities/get_interfaces");
    node_->declare_parameter<std::string>("capability_client.services.get_semantic_interfaces", "/capabilities/get_semantic_interfaces");
    node_->declare_parameter<std::string>("capability_client.services.get_providers", "/capabilities/get_providers");
    node_->declare_parameter<std::string>("capability_client.services.get_runnable_specs", "/capabilities/get_runnable_specs");
    node_->declare_parameter<std::string>("capability_client.services.establish_bond", "/capabilities/establish_bond");
    node_->declare_parameter<std::string>("capability_client.services.use_capability", "/capabilities/use_capability");
    node_->declare_parameter<std::string>("capability_client.services.free_capability", "/capabilities/free_capability");
    node_->declare_parameter<std::string>("capability_client.services.trigger_capability", "/capabilities/trigger_capability");
    node_->declare_parameter<std::string>("capability_client.services.connect_capability", "/capabilities/connect_capability");

    node_->get_parameter("capability_client.services.get_interfaces", get_interfaces_);
    node_->get_parameter("capability_client.services.get_semantic_interfaces", get_semantic_interfaces_);
    node_->get_parameter("capability_client.services.get_providers", get_providers_);
    node_->get_parameter("capability_client.services.get_runnable_specs", get_runnable_specs_);
    node_->get_parameter("capability_client.services.establish_bond", establish_bond_);
    node_->get_parameter("capability_client.services.use_capability", use_capability_);
    node_->get_parameter("capability_client.services.free_capability", free_capability_);
    node_->get_parameter("capability_client.services.trigger_capability", trigger_capability_);
    node_->get_parameter("capability_client.services.connect_capability", connect_capability_);

    get_interfaces_client_ = node_->create_client<GetInterfaces>(get_interfaces_);
    get_sem_interf_client_ = node_->create_client<GetSemanticInterfaces>(get_semantic_interfaces_);
    get_providers_client_ = node_->create_client<GetProviders>(get_providers_);
    get_runnable_specs_client_ = node_->create_client<GetRunnableSpecs>(get_runnable_specs_);
    establish_bond_client_ = node_->create_client<EstablishBond>(establish_bond_);
    use_capability_client_ = node_->create_client<UseCapability>(use_capability_);
    free_capability_client_ = node_->create_client<FreeCapability>(free_capability_);
    trig_capability_client_ = node_->create_client<TriggerCapability>(trigger_capability_);
    connect_capability_client_ = node_->create_client<ConnectCapability>(connect_capability_);

    RCLCPP_INFO(node_->get_logger(), "[Capability client] initialized.");
  }

  /**
   * @brief Wait until the capabilities2 server is ready to serve requests.
   *
   * Waits for all required services and then performs one probe request so
   * Fabric only proceeds once the server can answer discovery traffic.
   *
   * @param timeout_sec Optional override for the wait timeout. Negative values
   * use the configured parameter value.
   */
  void wait_until_ready(int timeout_sec = std::numeric_limits<int>::min())
  {
    const int effective_timeout_sec =
        timeout_sec == std::numeric_limits<int>::min() ? service_wait_timeout_sec_ : timeout_sec;

    wait_for_service(get_interfaces_client_, get_interfaces_, effective_timeout_sec);
    wait_for_service(get_sem_interf_client_, get_semantic_interfaces_, effective_timeout_sec);
    wait_for_service(get_providers_client_, get_providers_, effective_timeout_sec);
    wait_for_service(get_runnable_specs_client_, get_runnable_specs_, effective_timeout_sec);
    wait_for_service(establish_bond_client_, establish_bond_, effective_timeout_sec);
    wait_for_service(use_capability_client_, use_capability_, effective_timeout_sec);
    wait_for_service(free_capability_client_, free_capability_, effective_timeout_sec);
    wait_for_service(trig_capability_client_, trigger_capability_, effective_timeout_sec);
    wait_for_service(connect_capability_client_, connect_capability_, effective_timeout_sec);

    probe_server_readiness();
    RCLCPP_INFO(node_->get_logger(), "[Capability client] capabilities2 server is ready.");
  }

  void wait_till_ready(int timeout_sec = std::numeric_limits<int>::min())
  {
    wait_until_ready(timeout_sec);
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
    RCLCPP_INFO(node_->get_logger(), "[Capability client] requesting Interface information");

    auto request_interface = std::make_shared<GetInterfaces::Request>();

    bool completed = false;
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(mtx);

    // request data from the server
    auto result_interface_future = get_interfaces_client_->async_send_request(
        request_interface, [this, &completed, &cv, &capabilities](GetInterfacesClient::SharedFuture future) {
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

    RCLCPP_INFO(node_->get_logger(), "[Capability client] received %d interfaces from server\n", static_cast<int>(capabilities.size()));
  }

  /**
   * @brief Get the Semantic Interfaces from the capabilities2 server via related service client.
   * @throws `fabric::fabric_exception` if the service call fails
   *
   * @param interfaces std::vector of interfaces for which the semantic interfaces will be requested
   */
  void getSemanticInterfaces(std::vector<CapabilityInfo>& capabilities)
  {
    std::vector<CapabilityInfo> new_capabilities;

    for (auto& capability : capabilities)
    {
      RCLCPP_INFO(node_->get_logger(), "[Capability client] checking %s for semantic interfaces", capability.interface.c_str());

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
              RCLCPP_INFO(node_->get_logger(), "[Capability client]  Semantic interface: %s added\n", interface.c_str());
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
  void getProviders(std::vector<CapabilityInfo>& capabilities)
  {
    for (auto& capability : capabilities)
    {
      RCLCPP_INFO(node_->get_logger(), "[Capability client] requesting provider for %s", capability.interface.c_str());

      auto request_providers = std::make_shared<GetProviders::Request>();

      // request providers of the semantic interface
      request_providers->interface = capability.interface;
      request_providers->include_semantic = capability.is_semantic;

      bool completed = false;
      std::mutex mtx;
      std::condition_variable cv;
      std::unique_lock<std::mutex> lock(mtx);

      auto result_providers_future =
          get_providers_client_->async_send_request(request_providers, [this, &capability, &completed, &cv](GetProvidersClient::SharedFuture future) {
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

      RCLCPP_INFO(node_->get_logger(), "[Capability client] received for %s, default provider: %s, number of alternative providers: %d\n",
                  capability.interface.c_str(), capability.provider.c_str(), static_cast<int>(capability.alt_providers.size()));
    }
  }

  void getRunnableSpecs(std::vector<CapabilityInfo>& capabilities)
  {
    RCLCPP_INFO(node_->get_logger(), "[Capability client] requesting runnable specifications");

    auto request_specs = std::make_shared<GetRunnableSpecs::Request>();

    bool completed = false;
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(mtx);
    std::unordered_map<std::string, CapabilityProviderInfo> provider_metadata_by_name;

    auto result_specs_future = get_runnable_specs_client_->async_send_request(
        request_specs, [this, &provider_metadata_by_name, &completed, &cv](GetRunnableSpecsClient::SharedFuture future) {
          if (!future.valid())
          {
            throw fabric::fabric_exception("Failed to get runnable specifications from server");
          }

          auto response = future.get();
          if (response->index_of_specs.size() != response->runnable_specs.size())
          {
            throw fabric::fabric_exception("Runnable specification response has mismatched index and payload sizes");
          }

          for (const auto& runnable_spec : response->runnable_specs)
          {
            CapabilityProviderInfo provider_info = parse_provider_info(runnable_spec.spec);
            if (!provider_info.provider.empty())
            {
              provider_metadata_by_name[provider_info.provider] = provider_info;
            }
          }

          completed = true;
          cv.notify_all();
        });

    cv.wait(lock, [&completed]() { return completed; });

    for (auto& capability : capabilities)
    {
      capability.provider_details.clear();

      add_provider_details(capability, capability.provider, provider_metadata_by_name);
      for (const auto& provider_name : capability.alt_providers)
      {
        add_provider_details(capability, provider_name, provider_metadata_by_name);
      }
    }

    RCLCPP_INFO(node_->get_logger(), "[Capability client] attached runnable metadata to %d capabilities\n",
                static_cast<int>(capabilities.size()));
  }

  /**
   * @brief Request the bond from the capabilities2 server.
   * @throws `fabric::fabric_exception` if the service call fails
   *
   */
  std::string request_bond()
  {
    RCLCPP_INFO(node_->get_logger(), "[Capability client] requesting bond id");

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
          RCLCPP_INFO(node_->get_logger(), "[Capability client] received bond id : %s\n", bond_id.c_str());

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

      RCLCPP_INFO(node_->get_logger(), "[Capability client] starting capability %d : %s", index, connection.source.interface.c_str());

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

      RCLCPP_INFO(node_->get_logger(), "[Capability client] capability %s started successfully.\n", connection.source.interface.c_str());
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
              RCLCPP_INFO(node_->get_logger(), "[Capability client] capability %s freed successfully.\n", capability.c_str());
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
        RCLCPP_WARN(node_->get_logger(), "[Capability client] capability %s was not started as part of the plan, skipping free request.",
                    capability.c_str());
      }
    }

    // remove freed capabilities from started list
    for (const auto& capability : freed_capabilities)
      started_capabilities_.erase(std::remove(started_capabilities_.begin(), started_capabilities_.end(), capability), started_capabilities_.end());
  }

  void connect_capability(const std::string& ownership_id, const std::string& bond_id, uint8_t code, const fabric::node& source, const fabric::node& target)
  {
    auto request = std::make_shared<ConnectCapability::Request>();

    request->bond_id = bond_id;

    request->connection.type.code = code;
    request->connection.ownership_id = ownership_id;

    request->connection.source = to_capability_msg(source.parameters);
    request->connection.source.capability = source.interface;
    request->connection.source.provider = source.provider;
    request->connection.source.instance_id = std::to_string(source.instance_id);

    request->connection.target = to_capability_msg(target.parameters);
    request->connection.target.capability = target.interface;
    request->connection.target.provider = target.provider;
    request->connection.target.instance_id = std::to_string(target.instance_id);

    bool completed = false;
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(mtx);

    // send the request
    auto result_future =
        connect_capability_client_->async_send_request(request, [this, &completed, &cv](ConnectCapabilityClient::SharedFuture future) {
          if (!future.valid())
          {
            throw fabric::fabric_exception("Failed to configure capability connection event");
          }

          auto response = future.get();

          RCLCPP_INFO(node_->get_logger(), "[Capability client] capability connection event configured successfully.\n");
          completed = true;
          cv.notify_all();
        });

    // wait for the response
    cv.wait(lock, [&completed]() { return completed; });
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
      RCLCPP_INFO(node_->get_logger(), "[Capability client] configuring connection for %s/%d", connection.source.interface.c_str(),
                  connection.source.instance_id);

      if (connection.on_start.exists())
      {
        RCLCPP_INFO(node_->get_logger(), "[Capability client] connection to %s/%d on_start configuration requested",
                    connection.on_start.interface.c_str(), connection.on_start.instance_id);
        connect_capability(plan.plan_id, plan.bond_id, CapabilityEventCode::STARTED, connection.source, connection.on_start);
      }

      if (connection.on_stop.exists())
      {
        RCLCPP_INFO(node_->get_logger(), "[Capability client] connection to %s/%d on_stop configuration requested",
                    connection.on_stop.interface.c_str(), connection.on_stop.instance_id);
        connect_capability(plan.plan_id, plan.bond_id, CapabilityEventCode::STOPPED, connection.source, connection.on_stop);
      }

      if (connection.on_success.exists())
      {
        RCLCPP_INFO(node_->get_logger(), "[Capability client] connection to %s/%d on_success configuration requested",
                    connection.on_success.interface.c_str(), connection.on_success.instance_id);
        connect_capability(plan.plan_id, plan.bond_id, CapabilityEventCode::SUCCEEDED, connection.source, connection.on_success);
      }

      if (connection.on_failure.exists())
      {
        RCLCPP_INFO(node_->get_logger(), "[Capability client] connection to %s/%d on_failure configuration requested",
                    connection.on_failure.interface.c_str(), connection.on_failure.instance_id);
        connect_capability(plan.plan_id, plan.bond_id, CapabilityEventCode::FAILED, connection.source, connection.on_failure);
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
    request_trigger->capability.instance_id = std::to_string(plan.connections[0].source.instance_id);
    request_trigger->capability.capability = plan.connections[0].source.interface;
    request_trigger->capability.parameters = to_capability_msg(plan.connections[0].source.parameters).parameters;

    // send the request
    auto result_future =
        trig_capability_client_->async_send_request(request_trigger, [this, &completed, &cv](TriggerCapabilityClient::SharedFuture future) {
          if (!future.valid())
          {
            throw fabric::fabric_exception("Failed to trigger the first capability.");
          }

          auto response = future.get();
          completed = true;
          cv.notify_all();
          RCLCPP_INFO(node_->get_logger(), "[Capability client] first capability triggered successfully.");
        });

    // wait for the response
    cv.wait(lock, [&completed]() { return completed; });
  }

protected:
  static std::string yaml_string_or_empty(const YAML::Node& node, const char* key)
  {
    if (!node || !node[key])
    {
      return "";
    }
    return node[key].as<std::string>();
  }

  static bool yaml_bool_or_false(const YAML::Node& node, const char* key)
  {
    if (!node || !node[key])
    {
      return false;
    }
    return node[key].as<bool>();
  }

  static std::vector<std::string> yaml_string_list_or_empty(const YAML::Node& node, const char* key)
  {
    std::vector<std::string> values;
    if (!node || !node[key] || !node[key].IsSequence())
    {
      return values;
    }

    for (const auto& item : node[key])
    {
      values.push_back(item.as<std::string>());
    }

    return values;
  }

  static std::string yaml_serialized_value_or_empty(const YAML::Node& node, const char* key)
  {
    if (!node || !node[key])
    {
      return "";
    }

    std::string serialized = YAML::Dump(node[key]);
    if (!serialized.empty() && serialized.back() == '\n')
    {
      serialized.pop_back();
    }
    return serialized;
  }

  static CapabilityParameterInfo parse_parameter_info(const YAML::Node& node)
  {
    CapabilityParameterInfo parameter;
    parameter.name = yaml_string_or_empty(node, "name");
    parameter.type = yaml_string_or_empty(node, "type");
    parameter.description = yaml_string_or_empty(node, "description");
    parameter.semantic_key = yaml_string_or_empty(node, "semantic_key");
    parameter.required = yaml_bool_or_false(node, "required");
    parameter.satisfiable_from = yaml_string_list_or_empty(node, "satisfiable_from");
    parameter.fallback_parameter = yaml_string_or_empty(node, "fallback_parameter");
    parameter.aliases = yaml_string_list_or_empty(node, "aliases");
    parameter.default_value = yaml_serialized_value_or_empty(node, "default");
    parameter.has_default = !parameter.default_value.empty();
    return parameter;
  }

  static std::vector<CapabilityParameterInfo> parse_parameter_list(const YAML::Node& node)
  {
    std::vector<CapabilityParameterInfo> parameters;
    if (!node || !node.IsSequence())
    {
      return parameters;
    }

    for (const auto& item : node)
    {
      parameters.push_back(parse_parameter_info(item));
    }

    return parameters;
  }

  static CapabilityProviderInfo parse_provider_info(const std::string& yaml_text)
  {
    CapabilityProviderInfo provider_info;
    const YAML::Node root = YAML::Load(yaml_text);
    provider_info.provider = yaml_string_or_empty(root["provider"], "name");

    const YAML::Node definition = root["definition"];
    if (!definition || !definition.IsMap())
    {
      return provider_info;
    }

    provider_info.configuration_parameters = parse_parameter_list(definition["configuration_parameters"]);

    const YAML::Node runtime_parameters = definition["runtime_parameters"];
    provider_info.runtime_input_parameters = parse_parameter_list(runtime_parameters["input"]);
    provider_info.runtime_output_parameters = parse_parameter_list(runtime_parameters["output"]);
    return provider_info;
  }

  static void add_provider_details(
      CapabilityInfo& capability,
      const std::string& provider_name,
      const std::unordered_map<std::string, CapabilityProviderInfo>& provider_metadata_by_name)
  {
    if (provider_name.empty())
    {
      return;
    }

    auto it = provider_metadata_by_name.find(provider_name);
    if (it == provider_metadata_by_name.end())
    {
      return;
    }

    capability.provider_details.push_back(it->second);
  }

  /**
   * @brief Wait for a service to become available.
   */
  template <typename ClientT>
  void wait_for_service(const std::shared_ptr<ClientT>& client, const std::string& service_name, int timeout_sec)
  {
    using namespace std::chrono_literals;

    auto start = std::chrono::steady_clock::now();
    while (rclcpp::ok())
    {
      if (client->wait_for_service(1s))
      {
        RCLCPP_INFO(node_->get_logger(), "[Capability client] %s connected", service_name.c_str());
        return;
      }

      RCLCPP_INFO(node_->get_logger(), "[Capability client] %s is not available", service_name.c_str());

      if (timeout_sec >= 0)
      {
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start);
        if (elapsed.count() >= timeout_sec)
        {
          throw fabric::fabric_exception("Timed out waiting for service: " + service_name);
        }
      }
    }

    throw fabric::fabric_exception("ROS shutdown while waiting for service: " + service_name);
  }

  void probe_server_readiness()
  {
    RCLCPP_INFO(node_->get_logger(), "[Capability client] probing capabilities2 server readiness");

    auto request_specs = std::make_shared<GetRunnableSpecs::Request>();

    bool completed = false;
    std::mutex mtx;
    std::condition_variable cv;
    std::unique_lock<std::mutex> lock(mtx);

    auto result_specs_future = get_runnable_specs_client_->async_send_request(
        request_specs, [this, &completed, &cv](GetRunnableSpecsClient::SharedFuture future) {
          if (!future.valid())
          {
            throw fabric::fabric_exception("Capabilities2 readiness probe failed");
          }

          (void)future.get();
          completed = true;
          cv.notify_all();
        });

    cv.wait(lock, [&completed]() { return completed; });
  }

  /**
   * @brief pointer to the ROS2 node
   */
  rclcpp::Node::SharedPtr node_;

  int service_wait_timeout_sec_{ -1 };

  /**
   * @brief service names
   */
  std::string get_interfaces_;
  std::string get_semantic_interfaces_;
  std::string get_providers_;
  std::string get_runnable_specs_;
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

  /** Get runnable specs from capabilities server */
  GetRunnableSpecsClient::SharedPtr get_runnable_specs_client_;

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