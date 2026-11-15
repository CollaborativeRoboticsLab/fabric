#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/utils/exception.hpp>

namespace fabric
{

struct LlmPromptRequest
{
  std::string system_prompt;
  std::string user_prompt;
  std::string request_id;
  std::string model_family;
  std::string model;
  bool use_cache{ false };
  bool flush_cache{ false };
  bool use_chat_mode{ false };
  bool stream{ false };
};

struct LlmPromptResult
{
  bool success{ false };
  bool buffered{ false };
  std::string response;
  std::string error_message;
};

struct LlmEmbeddingRequest
{
  std::string text;
  std::string model_family;
  std::string model;
  int embedding_dim{ 0 };
};

struct LlmEmbeddingResult
{
  bool success{ false };
  std::vector<float> embedding;
  std::string error_message;
};

struct LlmTokenizationRequest
{
  enum class Mode
  {
    ENCODE,
    DECODE,
  };

  Mode mode{ Mode::ENCODE };
  std::string text;
  std::vector<int32_t> tokens;
  std::string model_family;
  std::string model;
};

struct LlmTokenizationResult
{
  bool success{ false };
  std::string text;
  std::vector<int32_t> tokens;
  std::string error_message;
};

class LlmAnalysisBase
{
public:
  virtual ~LlmAnalysisBase() = default;

  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "LlmAnalysisBasePlugin");
  }

  virtual LlmPromptResult prompt(const LlmPromptRequest& request) = 0;

  virtual LlmEmbeddingResult embed(const LlmEmbeddingRequest& request) = 0;

  virtual LlmTokenizationResult tokenize(const LlmTokenizationRequest& request) = 0;

  virtual void shutdown() {}

protected:
  void initialize_base(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name)
  {
    node_ = node;
    plugin_name_ = plugin_name;
  }

  rclcpp::Node::SharedPtr node_;
  std::string plugin_name_;
};

}  // namespace fabric