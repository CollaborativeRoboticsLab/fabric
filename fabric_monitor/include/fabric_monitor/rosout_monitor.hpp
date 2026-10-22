#pragma once

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>

#include <rcl_interfaces/msg/log.hpp>
#include <rclcpp/rclcpp.hpp>

#include <fabric_base/monitor_base.hpp>
#include <fabric_base/utils/structs.hpp>

namespace fabric
{

class RosoutMonitor : public MonitorBase
{
public:
  RosoutMonitor() = default;
  ~RosoutMonitor() override = default;

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "RosoutMonitor");
    node_->declare_parameter(plugin_name_ + ".model_family", "openai");
    node_->declare_parameter(plugin_name_ + ".model", "gpt-5.1");
    node_->get_parameter(plugin_name_ + ".model_family", model_family_);
    node_->get_parameter(plugin_name_ + ".model", model_);
  }

  void start() override
  {
    auto qos = rclcpp::QoS(rclcpp::KeepLast(100)).best_effort();
    rosout_subscriber_ = node_->create_subscription<rcl_interfaces::msg::Log>(
      monitor_topic_, qos,
      std::bind(&RosoutMonitor::rosoutCallback, this, std::placeholders::_1));

    monitoring_active_ = true;
    monitor_thread_ = std::thread(&RosoutMonitor::monitoring_function, this);
  }

  void stop() override
  {
    monitoring_active_ = false;
    buffer_cv_.notify_all();

    if (monitor_thread_.joinable())
    {
      monitor_thread_.join();
    }
  }

  std::any getData() override
  {
    std::lock_guard<std::mutex> results_lock(results_mutex_);
    auto snapshot = results_;
    results_.clear();
    return snapshot;
  }

protected:
  void monitoring_function() override
  {
    while (monitoring_active_)
    {
      std::unique_lock<std::mutex> lock(buffer_mutex_);
      buffer_cv_.wait(lock, [this]() { return !pending_logs_.empty() || !monitoring_active_; });

      if (!monitoring_active_)
      {
        break;
      }

      processing_logs_ = pending_logs_;
      pending_logs_.clear();
      lock.unlock();

      for (const auto& entry : processing_logs_)
      {
        const auto& node_name = entry.first;
        const auto& log_levels = entry.second;

        ComponentStatus component_status;
        component_status.timestamp = log_levels.timestamp;
        if (log_levels.error.empty() && log_levels.fatal.empty() && log_levels.warn.empty())
        {
          component_status.status = MonitoringStatus::NORMAL;
        }
        else if (!log_levels.error.empty() || !log_levels.fatal.empty())
        {
          component_status.status = MonitoringStatus::FAULTY;
        }
        else
        {
          component_status.status = MonitoringStatus::SUSPICIOUS;
        }

        component_status.description = summarizeLogs(node_name, log_levels, component_status.status);

        std::lock_guard<std::mutex> results_lock(results_mutex_);
        results_[node_name] = component_status;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1000.0 / monitor_frequency_)));
    }
  }

private:
  std::string summarizeLogs(
    const std::string& node_name,
    const LoggingData& log_levels,
    MonitoringStatus status) const
  {
    if (!analysis_plugin_)
    {
      return fallbackSummary(node_name, log_levels, status);
    }

    std::string user_prompt = "Node: " + node_name + "\nLogs:\n";
    appendLines("DEBUG", log_levels.debug, user_prompt);
    appendLines("INFO", log_levels.info, user_prompt);
    appendLines("WARN", log_levels.warn, user_prompt);
    appendLines("ERROR", log_levels.error, user_prompt);
    appendLines("FATAL", log_levels.fatal, user_prompt);

    auto response = analysis_plugin_->prompt(LlmPromptRequest{
      "Summarize the operational condition of this ROS 2 node from the provided rosout messages in one or two sentences.",
      user_prompt,
      "",
      model_family_,
      model_,
      false,
      false,
      false,
      false,
    });

    if (!response.success || response.response.empty())
    {
      return fallbackSummary(node_name, log_levels, status);
    }

    return response.response;
  }

  static void appendLines(const std::string& label, const std::vector<std::string>& messages, std::string& out)
  {
    for (const auto& message : messages)
    {
      out += label + ": " + message + "\n";
    }
  }

  static std::string fallbackSummary(const std::string& node_name, const LoggingData& log_levels, MonitoringStatus status)
  {
    const size_t warning_count = log_levels.warn.size();
    const size_t error_count = log_levels.error.size() + log_levels.fatal.size();
    const char* status_label = status == MonitoringStatus::FAULTY ? "faulty" :
      (status == MonitoringStatus::SUSPICIOUS ? "showing warnings" : "nominal");
    return node_name + " is " + status_label + "; warnings=" + std::to_string(warning_count) +
      ", errors=" + std::to_string(error_count) + ".";
  }

  void rosoutCallback(const rcl_interfaces::msg::Log::ConstSharedPtr& msg)
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);
    auto& node_logs = pending_logs_[msg->name];
    node_logs.timestamp.sec = msg->stamp.sec;
    node_logs.timestamp.nanosec = msg->stamp.nanosec;

    switch (msg->level)
    {
      case rcl_interfaces::msg::Log::DEBUG:
        node_logs.debug.push_back(msg->msg);
        break;
      case rcl_interfaces::msg::Log::INFO:
        node_logs.info.push_back(msg->msg);
        break;
      case rcl_interfaces::msg::Log::WARN:
        node_logs.warn.push_back(msg->msg);
        break;
      case rcl_interfaces::msg::Log::ERROR:
        node_logs.error.push_back(msg->msg);
        break;
      case rcl_interfaces::msg::Log::FATAL:
        node_logs.fatal.push_back(msg->msg);
        break;
      default:
        break;
    }

    buffer_cv_.notify_all();
  }

  std::string model_family_;
  std::string model_;
  std::map<std::string, LoggingData> pending_logs_;
  std::map<std::string, LoggingData> processing_logs_;
  std::map<std::string, ComponentStatus> results_;
  std::mutex results_mutex_;
  rclcpp::Subscription<rcl_interfaces::msg::Log>::SharedPtr rosout_subscriber_;
};

}  // namespace fabric