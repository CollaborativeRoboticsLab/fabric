#include <memory>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>
#include <tinyxml2.h>



#include <fabric/xml_parser.hpp>






  /**
   * @brief Verify the plan before continuing the execution using xml parsing and collected interface, semantic interface
   * and provider information
   *
   */
  void verify_and_continue()
  {
    event_->info("Verifying the plan");

    bool verification_success = true;


    // extract the components within the 'plan' tags
    bool extraction_success = false;
    tinyxml2::XMLElement* plan = xml_parser_->extract_plan(document, extraction_success);

    if (!extraction_success)
    {
      result_msg->success = false;
      result_msg->message = "Execution plan is not compatible. Please recheck and update";
      event_->error(result_msg->message);
      goal_handle_->abort(result_msg);
      verification_success = false;
    }

    event_->info("Plan extraction complete");

    // verify whether the plan is valid by checking the tags
    std::string error_message;

    if (!xml_parser_->check_tags(plan, interface_list, providers_list, rejected_list, error_message))
    {
      result_msg->success = false;
      result_msg->message = "Plan verification failed: " + error_message;
      event_->error(result_msg->message);
      goal_handle_->abort(result_msg);
      verification_success = false;
    }

    event_->info("Checking tags successful");

    // verify the plan
    if (!verification_success)
    {
      event_->info("Plan verification failed");

      if (rejected_list.size() > 0)
      {
        // TODO: improve with error codes
        auto result = std::make_shared<Plan::Result>();
        result->success = false;
        result->message = "Plan verification failed. There are mismatched events";

        for (const auto& rejected_element : rejected_list)
        {
          result->failed_elements.push_back(rejected_element);
        }

        goal_handle_->abort(result);
        event_->info(result->message);
      }
      else
      {
        // TODO: improve with error codes
        result_msg->success = false;
        result_msg->message = "Plan verification failed. Server Execution Cancelled.";
        event_->error(result_msg->message);
        goal_handle_->abort(result_msg);
        return;
      }

      event_->error("Server Execution Cancelled");
    }

    event_->info("Plan verification successful. Proceeding with connections extraction");

    // Extract the connections from the plan
    xml_parser_->extract_connections(plan, connection_map);

    event_->info("Connection extraction successful");

    // estasblish the bond with the server
    request_bond();
  }

  /**
   * @brief Request the bond from the capabilities2 server
   *
   */
  void request_bond()
  {
    event_->info("Requesting bond id");

    // create bond establishing server request
    auto request_bond = std::make_shared<EstablishBond::Request>();

    // send the request
    auto result_future = establish_bond_client_->async_send_request(request_bond, [this](EstablishBondClient::SharedFuture future) {
      if (!future.valid())
      {
        result_msg->success = false;
        result_msg->message = "Failed to retrieve the bond id. Server execution cancelled";
        event_->error(result_msg->message);
        goal_handle_->abort(result_msg);
        return;
      }

      auto response = future.get();
      bond_id_ = response->bond_id;
      event_->info("Received the bond id : " + bond_id_);

      establish_bond();
    });
  }

  /**
   * @brief establish the bond with capabilities2 server
   *
   */
  void establish_bond()
  {
    bond_client_cache_[bond_id_] = std::make_unique<BondClient>(shared_from_this(), event_, bond_id_);
    bond_client_cache_[bond_id_]->start();

    event_->info("Bond sucessfully established with bond id : " + bond_id_);

    if (bond_client_cache_.size() > 1)
    {
      for (auto& [old_bond_id, bond_client] : bond_client_cache_)
      {
        if (old_bond_id != bond_id_)
        {
          bond_client->stop();
          event_->info("Stopping and removing old bond with id : " + old_bond_id);
        }
      }
    }

    expected_capabilities_ = connection_map.size();

    event_->info("Requsting start of " + std::to_string(expected_capabilities_) + " capabilities");

    use_capability(connection_map);
  }

  /**
   * @brief Request use of capability from capabilities2 server
   *
   * @param capabilities capability list to be started
   * @param provider provider of the capability
   */
  void use_capability(std::map<int, capabilities2::node_t>& capabilities)
  {
    std::string capability = capabilities[completed_capabilities_].source.runner;
    std::string provider = capabilities[completed_capabilities_].source.provider;

    auto request_use = std::make_shared<UseCapability::Request>();
    request_use->capability = capability;
    request_use->preferred_provider = provider;
    request_use->bond_id = bond_id_;

    event_->info("Starting capability of Runner " + std::to_string(completed_capabilities_) + " : " +
                 capabilities[completed_capabilities_].source.runner);

    // send the request
    auto result_future =
        use_capability_client_->async_send_request(request_use, [this, capability, provider](UseCapabilityClient::SharedFuture future) {
          if (!future.valid())
          {
            result_msg->success = false;
            result_msg->message = "Failed to Use capability " + capability + " from " + provider + ". Server Execution Cancelled";
            event_->error(result_msg->message);
            goal_handle_->abort(result_msg);

            // release all capabilities that were used since not all started successfully
            free_capability_all(connection_map);

            for (auto& [bond_id, bond_client] : bond_client_cache_)
            {
              bond_client->stop();
            }
            return;
          }

          completed_capabilities_++;

          auto response = future.get();

          event_->info(std::to_string(completed_capabilities_) + "/" + std::to_string(expected_capabilities_) + " : start succeessful");

          // Check if all expected calls are completed before calling verify_plan
          if (completed_capabilities_ == expected_capabilities_)
          {
            event_->info("All requested capabilities have been started. Configuring the capabilities with events");

            expected_configurations_ = connection_map.size();

            event_->info("Requsting capability configuration for " + std::to_string(expected_configurations_) + " capabilities");

            configure_capabilities(connection_map);
          }
          else
          {
            use_capability(connection_map);
          }
        });
  }

  /**
   * @brief Free all started capabilities in the capabilities map
   *
   * @param capabilities map of capabilities to be freed
   */
  void free_capability_all(std::map<int, capabilities2::node_t>& capabilities)
  {
    std::string capability = capabilities[freed_capabilities_].source.runner;

    auto request_free = std::make_shared<FreeCapability::Request>();
    request_free->capability = capability;
    request_free->bond_id = bond_id_;

    // send the request
    auto result_future = free_capability_client_->async_send_request(request_free, [this, capability](FreeCapabilityClient::SharedFuture future) {
      if (!future.valid())
      {
        result_msg->success = false;
        result_msg->message = "Failed to free capability " + capability;
        event_->error(result_msg->message);
        goal_handle_->abort(result_msg);
        return;
      }

      auto response = future.get();
      event_->info("Successfully freed capability " + capability);

      freed_capabilities_++;

      // Check if all expected calls are completed before calling verify_plan
      if (freed_capabilities_ == completed_capabilities_)
      {
        event_->info("All started capabilities have been freed.");
      }
      else
      {
        free_capability_all(connection_map);
      }
    });
  }

  /**
   * @brief Request use of capability from capabilities2 server
   */
  void configure_capabilities(std::map<int, capabilities2::node_t>& capabilities)
  {
    auto request_configure = std::make_shared<ConfigureCapability::Request>();

    event_->info("Configuring capability of Runner " + std::to_string(completed_configurations_) + " named " +
                 capabilities[completed_configurations_].source.runner);

    if (xml_parser_->convert_to_string(capabilities[completed_configurations_].source.parameters, request_configure->source.parameters))
    {
      request_configure->source.capability = capabilities[completed_configurations_].source.runner;
      request_configure->source.provider = capabilities[completed_configurations_].source.provider;
    }
    else
    {
      request_configure->source.capability = "";
      request_configure->source.provider = "";
    }

    if (xml_parser_->convert_to_string(capabilities[completed_configurations_].target_on_start.parameters,
                                      request_configure->target_on_start.parameters))
    {
      request_configure->target_on_start.capability = capabilities[completed_configurations_].target_on_start.runner;
      request_configure->target_on_start.provider = capabilities[completed_configurations_].target_on_start.provider;
    }
    else
    {
      request_configure->target_on_start.capability = "";
      request_configure->target_on_start.provider = "";
    }

    if (xml_parser_->convert_to_string(capabilities[completed_configurations_].target_on_stop.parameters,
                                      request_configure->target_on_stop.parameters))
    {
      request_configure->target_on_stop.capability = capabilities[completed_configurations_].target_on_stop.runner;
      request_configure->target_on_stop.provider = capabilities[completed_configurations_].target_on_stop.provider;
    }
    else
    {
      request_configure->target_on_stop.capability = "";
      request_configure->target_on_stop.provider = "";
    }

    if (xml_parser_->convert_to_string(capabilities[completed_configurations_].target_on_success.parameters,
                                      request_configure->target_on_success.parameters))
    {
      request_configure->target_on_success.capability = capabilities[completed_configurations_].target_on_success.runner;
      request_configure->target_on_success.provider = capabilities[completed_configurations_].target_on_success.provider;
    }
    else
    {
      request_configure->target_on_success.capability = "";
      request_configure->target_on_success.provider = "";
    }

    if (xml_parser_->convert_to_string(capabilities[completed_configurations_].target_on_failure.parameters,
                                      request_configure->target_on_failure.parameters))
    {
      request_configure->target_on_failure.capability = capabilities[completed_configurations_].target_on_failure.runner;
      request_configure->target_on_failure.provider = capabilities[completed_configurations_].target_on_failure.provider;
    }
    else
    {
      request_configure->target_on_failure.capability = "";
      request_configure->target_on_failure.provider = "";
    }

    request_configure->connection_description = capabilities[completed_configurations_].connection_description;
    request_configure->trigger_id = capabilities[completed_configurations_].trigger_id;

    std::string source_capability = capabilities[completed_configurations_].source.runner;

    // send the request
    auto result_future =
        conf_capability_client_->async_send_request(request_configure, [this, source_capability](ConfigureCapabilityClient::SharedFuture future) {
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

  /**
   * @brief Trigger the first node
   */
  void trigger_first_node()
  {
    auto request_trigger = std::make_shared<TriggerCapability::Request>();

    std::string parameter_string;
    xml_parser_->convert_to_string(connection_map[0].source.parameters, parameter_string);
    request_trigger->capability = connection_map[0].source.runner;
    request_trigger->parameters = parameter_string;

    // send the request
    auto result_future = trig_capability_client_->async_send_request(request_trigger, [this](TriggerCapabilityClient::SharedFuture future) {
      if (!future.valid())
      {
        result_msg->success = false;
        result_msg->message = "Failed to trigger capability " + connection_map[0].source.runner;
        event_->error(result_msg->message);
        goal_handle_->abort(result_msg);
        return;
      }

      auto response = future.get();
      event_->info("Successfully triggered capability " + connection_map[0].source.runner);

      result_msg->success = true;
      result_msg->message = "Successfully started fabric execution with " + std::to_string(expected_capabilities_) +
                            " capabilities and " + std::to_string(expected_configurations_) + " configurations";
      event_->info(result_msg->message);
      goal_handle_->succeed(result_msg);
    });
  }

private:
  /** File Path link */
  std::string plan_file_path;

  /** Modified plan with closing capabilities */
  std::string modified_plan;

  /** flag to select loading from file or accepting via action server */
  bool read_file;

  int expected_interfaces_;
  int completed_interfaces_;

  int expected_providers_;
  int completed_providers_;

  int expected_capabilities_;
  int completed_capabilities_;
  int freed_capabilities_;

  int expected_configurations_;
  int completed_configurations_;


  /** XML Document */
  tinyxml2::XMLDocument document;

  /** vector of connections */
  std::map<int, capabilities2::node_t> connection_map;

  /** Interface List */
  std::vector<bool> is_semantic_list;






 


  /** Event client for publishing events */
  std::shared_ptr<EventClient> event_;

  /** XMLParser engine */
  std::shared_ptr<XMLParser> xml_parser_;

  /** capabilities2 server and fabric synchronization tools */
  // std::mutex mutex_;
  // std::condition_variable cv_;
  // bool fabric_completed_;
  // std::unique_lock<std::mutex> lock_;
};