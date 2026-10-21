#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <fabric_base/validation_base.hpp>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <prompt_msgs/msg/model_option.hpp>
#include <prompt_msgs/srv/prompt.hpp>
#include <sstream>
#include <string>
#include <vector>

namespace fabric
{

using ValidationBase = ::fabric::ValidationBase;
using PlanningRequest = ::fabric::planning::Request;
using BehaviourInfo = ::fabric::capability::Descriptor;
using CandidatePlan = ::fabric::planning::CandidatePlan;

class SemanticValidation : public ValidationBase
{
public:
  using Prompt = prompt_msgs::srv::Prompt;

  struct SemanticValidationDecision
  {
    bool aligned = true;
    double confidence = 0.0;
    std::string reason;
    std::vector<std::string> unsupported_steps;
  };

  void validate(fabric::plan::Plan& plan, const std::vector<fabric::capability::Descriptor>& capabilities) override
  {
    (void)plan;
    (void)capabilities;
  }

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "SemanticValidation");

    node_->declare_parameter<std::string>(plugin_name_ + ".prompt_service", "prompt/prompt");
    node_->declare_parameter<std::string>(plugin_name_ + ".model_family", "openai");
    node_->declare_parameter<std::string>(plugin_name_ + ".model", "gpt-5.1");
    node_->declare_parameter<bool>(plugin_name_ + ".use_cache", true);
    node_->declare_parameter<bool>(plugin_name_ + ".flush_cache", false);
    node_->declare_parameter<bool>(plugin_name_ + ".use_chat_mode", false);
    node_->declare_parameter<double>(plugin_name_ + ".request_timeout_sec", 5.0);
    node_->declare_parameter<double>(plugin_name_ + ".warning_confidence_threshold", 0.60);
    node_->declare_parameter<double>(plugin_name_ + ".low_confidence_penalty", 0.10);
    node_->declare_parameter<bool>(plugin_name_ + ".warn_when_unavailable", true);

    prompt_service_ = node_->get_parameter(plugin_name_ + ".prompt_service").as_string();
    model_family_ = node_->get_parameter(plugin_name_ + ".model_family").as_string();
    model_ = node_->get_parameter(plugin_name_ + ".model").as_string();
    use_cache_ = node_->get_parameter(plugin_name_ + ".use_cache").as_bool();
    flush_cache_ = node_->get_parameter(plugin_name_ + ".flush_cache").as_bool();
    use_chat_mode_ = node_->get_parameter(plugin_name_ + ".use_chat_mode").as_bool();
    request_timeout_sec_ = node_->get_parameter(plugin_name_ + ".request_timeout_sec").as_double();
    warning_confidence_threshold_ = node_->get_parameter(plugin_name_ + ".warning_confidence_threshold").as_double();
    low_confidence_penalty_ = node_->get_parameter(plugin_name_ + ".low_confidence_penalty").as_double();
    warn_when_unavailable_ = node_->get_parameter(plugin_name_ + ".warn_when_unavailable").as_bool();

    prompt_client_ = node_->create_client<Prompt>(prompt_service_);
  }

  void validate(const PlanningRequest& request, const std::vector<BehaviourInfo>& capabilities,
                CandidatePlan& candidate) const override
  {
    if (candidate.validation.rejected || candidate.nodes.empty())
    {
      return;
    }

    const auto decision = requestSemanticDecision(request, capabilities, candidate);
    if (!decision.has_value())
    {
      if (warn_when_unavailable_)
      {
        addWarning(candidate, "semantic validation unavailable; continuing with deterministic validation only");
      }
      return;
    }

    if (!decision->aligned)
    {
      std::ostringstream message;
      message << "semantic validation rejected candidate";
      if (!decision->reason.empty())
      {
        message << ": " << decision->reason;
      }
      if (!decision->unsupported_steps.empty())
      {
        message << " unsupported_steps=";
        for (size_t index = 0; index < decision->unsupported_steps.size(); ++index)
        {
          if (index > 0)
          {
            message << ',';
          }
          message << decision->unsupported_steps[index];
        }
      }
      addReject(candidate, message.str());
      return;
    }

    if (decision->confidence < warning_confidence_threshold_)
    {
      std::ostringstream message;
      message << "semantic validation confidence is low";
      if (!decision->reason.empty())
      {
        message << ": " << decision->reason;
      }
      addWarning(candidate, message.str());
      addPenalty(candidate, low_confidence_penalty_, message.str());
    }
  }

protected:
  virtual std::optional<SemanticValidationDecision>
  requestSemanticDecision(const PlanningRequest& request, const std::vector<BehaviourInfo>& capabilities,
                          const CandidatePlan& candidate) const
  {
    if (!prompt_client_ || !prompt_client_->wait_for_service(std::chrono::milliseconds(100)))
    {
      return std::nullopt;
    }

    auto prompt_request = std::make_shared<Prompt::Request>();
    prompt_request->uuid = request.planning_request_id.empty() ? request.uuid : request.planning_request_id;
    prompt_request->prompt.prompt = buildSemanticPrompt(request, capabilities, candidate);
    prompt_request->prompt.model_family = model_family_;
    prompt_request->prompt.use_cache = use_cache_;
    prompt_request->prompt.flush_cache = flush_cache_;
    prompt_request->prompt.use_chat_mode = use_chat_mode_;

    prompt_msgs::msg::ModelOption model_option;
    model_option.key = "model";
    model_option.value = model_;
    model_option.type = prompt_msgs::msg::ModelOption::STRING_TYPE;
    prompt_request->prompt.options.push_back(model_option);

    std::mutex mutex;
    std::condition_variable cv;
    bool completed = false;
    Prompt::Response::SharedPtr response;
    std::unique_lock<std::mutex> lock(mutex);

    prompt_client_->async_send_request(prompt_request,
                                       [&response, &completed, &cv](rclcpp::Client<Prompt>::SharedFuture future) {
                                         response = future.get();
                                         completed = true;
                                         cv.notify_all();
                                       });

    if (!cv.wait_for(lock, std::chrono::duration<double>(request_timeout_sec_), [&completed]() { return completed; }))
    {
      return std::nullopt;
    }

    if (!response || !response->response.success)
    {
      return std::nullopt;
    }

    return parseSemanticDecision(response->response.response);
  }

  std::string buildSemanticPrompt(const PlanningRequest& request, const std::vector<BehaviourInfo>& capabilities,
                                  const CandidatePlan& candidate) const
  {
    std::ostringstream prompt;
    prompt << "You are validating whether the execution order of robotics capabilities can achieve the intended task. "
           << "Use the task and the ordered capability descriptions only. "
           << "Each plan step contains the interface description and provider description for the executed capability. "
           << "If a step lists implicit_prerequisites, treat them as backend-managed dependencies that are "
              "auto-started by capabilities2. "
           << "Do not require those dependencies to appear as separate explicit plan steps unless the task is "
              "specifically about managing that dependency lifecycle. "
           << "Respect the listed order because it is the execution order of the plan. "
           << "Return JSON only with keys aligned (bool), confidence (0..1), reason (string), unsupported_steps (array "
              "of step ids).\n\n";
    prompt << "Task: " << request.task_description << "\n\n";
    prompt << "Ordered capability descriptions:\n";
    for (const auto& node : candidate.nodes)
    {
      const BehaviourInfo* capability = findCapability(capabilities, node.capability_name, node.provider_name);
      prompt << "- step_id: " << node.candidate_node_id << "\n";
      prompt << "  interface: " << node.capability_name << "\n";
      prompt << "  provider: " << node.provider_name << "\n";
      prompt << "  skill_id: " << node.capability_id << "\n";
      if (capability == nullptr)
      {
        prompt << "  interface_description: metadata unavailable\n";
        prompt << "  provider_description: metadata unavailable\n";
        continue;
      }

      prompt << "  interface_description: " << capability->interface_description << "\n";
      prompt << "  provider_description: " << capability->provider_description << "\n";
      const auto implicit_prerequisites = implicitPrerequisites(*capability);
      if (!implicit_prerequisites.empty())
      {
        prompt << "  implicit_prerequisites:";
        for (const auto& dependency : implicit_prerequisites)
          prompt << " [" << dependency << "]";
        prompt << "\n";
      }
    }
    return prompt.str();
  }

  static std::vector<std::string> implicitPrerequisites(const BehaviourInfo& capability)
  {
    std::vector<std::string> dependencies;
    std::unordered_set<std::string> seen;
    for (const auto& relation : capability.relations)
    {
      if (relation.predicate != "depends_on" || relation.object.empty())
        continue;
      if (seen.insert(relation.object).second)
        dependencies.push_back(relation.object);
    }
    return dependencies;
  }

  std::optional<SemanticValidationDecision> parseSemanticDecision(const std::string& raw_response) const
  {
    try
    {
      std::string cleaned = raw_response;
      const size_t fenced_start = cleaned.find("```");
      if (fenced_start != std::string::npos)
      {
        const size_t first_newline = cleaned.find('\n', fenced_start);
        const size_t fenced_end = cleaned.rfind("```");
        if (first_newline != std::string::npos && fenced_end != std::string::npos && fenced_end > first_newline)
        {
          cleaned = cleaned.substr(first_newline + 1, fenced_end - first_newline - 1);
        }
      }

      const auto parsed = nlohmann::json::parse(cleaned);
      if (!parsed.contains("aligned") || !parsed["aligned"].is_boolean())
      {
        return std::nullopt;
      }

      SemanticValidationDecision decision;
      decision.aligned = parsed["aligned"].get<bool>();
      if (parsed.contains("confidence") && parsed["confidence"].is_number())
      {
        decision.confidence = std::clamp(parsed["confidence"].get<double>(), 0.0, 1.0);
      }
      if (parsed.contains("reason") && parsed["reason"].is_string())
      {
        decision.reason = parsed["reason"].get<std::string>();
      }
      if (parsed.contains("unsupported_steps") && parsed["unsupported_steps"].is_array())
      {
        for (const auto& item : parsed["unsupported_steps"])
        {
          if (item.is_string())
          {
            decision.unsupported_steps.push_back(item.get<std::string>());
          }
        }
      }
      return decision;
    }
    catch (const std::exception&)
    {
      return std::nullopt;
    }
  }

  static const BehaviourInfo* findCapability(const std::vector<BehaviourInfo>& capabilities,
                                             const std::string& interface_name, const std::string& provider_name)
  {
    for (const auto& capability : capabilities)
    {
      if (capability.interface_name == interface_name && capability.provider_name == provider_name)
      {
        return &capability;
      }
    }
    return nullptr;
  }

  rclcpp::Client<Prompt>::SharedPtr prompt_client_;
  std::string prompt_service_ = "prompt/prompt";
  std::string model_family_ = "openai";
  std::string model_ = "gpt-5.1";
  bool use_cache_ = true;
  bool flush_cache_ = false;
  bool use_chat_mode_ = false;
  bool warn_when_unavailable_ = true;
  double request_timeout_sec_ = 5.0;
  double warning_confidence_threshold_ = 0.60;
  double low_confidence_penalty_ = 0.10;
};

}  // namespace fabric