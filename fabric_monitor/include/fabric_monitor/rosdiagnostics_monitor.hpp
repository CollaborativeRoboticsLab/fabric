#pragma once

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>

#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <diagnostic_msgs/msg/diagnostic_status.hpp>
#include <rclcpp/rclcpp.hpp>

#include <fabric_base/monitor_base.hpp>
#include <fabric_base/utils/structs.hpp>

namespace fabric
{

class RosDiagnosticsMonitor : public MonitorBase
{
public:
  RosDiagnosticsMonitor() = default;
  ~RosDiagnosticsMonitor() override = default;

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "RosDiagnosticsMonitor");
    node_->declare_parameter(plugin_name_ + ".model_family", "openai");
    node_->declare_parameter(plugin_name_ + ".model", "gpt-5.1");
    node_->get_parameter(plugin_name_ + ".model_family", model_family_);
    node_->get_parameter(plugin_name_ + ".model", model_);
  }

  void start() override
  {
    auto qos = rclcpp::QoS(rclcpp::KeepLast(100)).best_effort();
    diagnostics_subscriber_ = node_->create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
      monitor_topic_, qos,
      std::bind(&RosDiagnosticsMonitor::diagnosticsCallback, this, std::placeholders::_1));

    monitoring_active_ = true;
    monitor_thread_ = std::thread(&RosDiagnosticsMonitor::monitoring_function, this);
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
      buffer_cv_.wait(lock, [this]() { return !pending_diagnostics_.empty() || !monitoring_active_; });

      if (!monitoring_active_)
      {
        break;
      }

      processing_diagnostics_ = pending_diagnostics_;
      pending_diagnostics_.clear();
      lock.unlock();

      for (const auto& entry : processing_diagnostics_)
      {
        const auto& component_name = entry.first;
        const auto& diagnostics = entry.second;

        ComponentStatus component_status;
        component_status.timestamp = diagnostics.timestamp;
        if (diagnostics.error.empty() && diagnostics.stale.empty() && diagnostics.warn.empty())
        {
          component_status.status = MonitoringStatus::NORMAL;
        }
        else if (!diagnostics.error.empty() || !diagnostics.stale.empty())
        {
          component_status.status = MonitoringStatus::FAULTY;
        }
        else
        {
          component_status.status = MonitoringStatus::SUSPICIOUS;
        }

        component_status.description = summarizeDiagnostics(component_name, diagnostics, component_status.status);

        std::lock_guard<std::mutex> results_lock(results_mutex_);
        results_[component_name] = component_status;
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(1000.0 / monitor_frequency_)));
    }
  }

private:
  std::string summarizeDiagnostics(
    const std::string& component_name,
    const DiagnosticData& diagnostics,
    MonitoringStatus status) const
  {
    if (!analysis_plugin_)
    {
      return fallbackSummary(component_name, diagnostics, status);
    }

    std::string user_prompt = "Component: " + component_name + "\nDiagnostics:\n";
    appendLines("OK", diagnostics.ok, user_prompt);
    appendLines("WARN", diagnostics.warn, user_prompt);
    appendLines("ERROR", diagnostics.error, user_prompt);
    appendLines("STALE", diagnostics.stale, user_prompt);

    auto response = analysis_plugin_->prompt(LlmPromptRequest{
      "Summarize the operational condition of this ROS 2 component from the provided diagnostics in one or two sentences.",
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
      return fallbackSummary(component_name, diagnostics, status);
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

  static std::string fallbackSummary(const std::string& component_name, const DiagnosticData& diagnostics, MonitoringStatus status)
  {
    const size_t warning_count = diagnostics.warn.size();
    const size_t error_count = diagnostics.error.size() + diagnostics.stale.size();
    const char* status_label = status == MonitoringStatus::FAULTY ? "faulty" :
      (status == MonitoringStatus::SUSPICIOUS ? "showing warnings" : "nominal");
    return component_name + " is " + status_label + "; warnings=" + std::to_string(warning_count) +
      ", errors=" + std::to_string(error_count) + ".";
  }

  void diagnosticsCallback(const diagnostic_msgs::msg::DiagnosticArray::ConstSharedPtr& msg)
  {
    std::lock_guard<std::mutex> lock(buffer_mutex_);

    for (const auto& status : msg->status)
    {
      auto& component_diagnostics = pending_diagnostics_[status.name];
      component_diagnostics.timestamp.sec = static_cast<int32_t>(msg->header.stamp.sec);
      component_diagnostics.timestamp.nanosec = msg->header.stamp.nanosec;

      switch (status.level)
      {
        case diagnostic_msgs::msg::DiagnosticStatus::OK:
          component_diagnostics.ok.push_back(status.message);
          break;
        case diagnostic_msgs::msg::DiagnosticStatus::WARN:
          component_diagnostics.warn.push_back(status.message);
          break;
        case diagnostic_msgs::msg::DiagnosticStatus::ERROR:
          component_diagnostics.error.push_back(status.message);
          break;
        case diagnostic_msgs::msg::DiagnosticStatus::STALE:
          component_diagnostics.stale.push_back(status.message);
          break;
        default:
          break;
      }
    }

    buffer_cv_.notify_all();
  }

  std::string model_family_;
  std::string model_;
  std::map<std::string, DiagnosticData> pending_diagnostics_;
  std::map<std::string, DiagnosticData> processing_diagnostics_;
  std::map<std::string, ComponentStatus> results_;
  std::mutex results_mutex_;
  rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr diagnostics_subscriber_;
};

}  // namespace fabric