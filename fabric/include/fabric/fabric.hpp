#include <memory>
#include <thread>
#include <string>
#include <vector>
#include <algorithm>
#include <tinyxml2.h>

{



  

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