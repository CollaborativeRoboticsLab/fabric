#pragma once

#include <any>
#include <cmath>
#include <mutex>
#include <string>

#include <fabric_base/monitor_base.hpp>
#include <fabric_base/utils/monitoring_structs.hpp>
#include <sensor_msgs/msg/battery_state.hpp>

namespace fabric
{

class BatteryMonitor : public MonitorBase
{
public:
  using BatteryState = sensor_msgs::msg::BatteryState;

  ~BatteryMonitor() override
  {
    stop();
  }

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "BatteryMonitor");
    if (!node_->has_parameter("BatteryMonitor.enable"))
    {
      node_->declare_parameter<bool>("BatteryMonitor.enable", true);
    }
    enabled_ = node_->get_parameter("BatteryMonitor.enable").as_bool();
    if (!node_->has_parameter("BatteryMonitor.topic"))
    {
      node_->declare_parameter<std::string>("BatteryMonitor.topic", "/battery_state");
    }
    if (monitor_topic_.empty())
    {
      monitor_topic_ = node_->get_parameter("BatteryMonitor.topic").as_string();
    }
  }

  void start() override
  {
    if (!enabled_ || monitoring_active_.exchange(true))
    {
      return;
    }
    battery_subscriber_ = node_->create_subscription<BatteryState>(
        monitor_topic_, rclcpp::QoS(rclcpp::KeepLast(10)).best_effort(),
        [this](BatteryState::ConstSharedPtr msg) {
          std::lock_guard<std::mutex> lock(results_mutex_);
          if (!monitoring_active_)
          {
            return;
          }
          const double percentage = msg->percentage;
          results_.percentage = std::isfinite(percentage) && percentage >= 0.0 && percentage <= 1.0 ?
              percentage : -1.0;
          if (results_.percentage < 0.0 || percentage >= 0.4)
          {
            return;
          }
          monitor::ComponentStatus warning;
          warning.timestamp = { msg->header.stamp.sec, msg->header.stamp.nanosec };
          warning.status = percentage < 0.2 ? monitor::MonitoringStatus::FAULTY :
              monitor::MonitoringStatus::SUSPICIOUS;
          warning.description = percentage < 0.2 ? "Battery critically low (" : "Battery low (";
          warning.description += std::to_string(percentage * 100.0) + "%).";
          results_.warnings.push_back(std::move(warning));
        });
  }

  void stop() override
  {
    monitoring_active_ = false;
    battery_subscriber_.reset();
    std::lock_guard<std::mutex> lock(results_mutex_);
    results_ = monitor::BatteryData{};
  }

  std::any getData() override
  {
    std::lock_guard<std::mutex> lock(results_mutex_);
    monitor::BatteryData data;
    data.percentage = results_.percentage;
    data.warnings.swap(results_.warnings);
    return data;
  }

private:
  std::mutex results_mutex_;
  bool enabled_{ true };
  monitor::BatteryData results_;
  rclcpp::Subscription<BatteryState>::SharedPtr battery_subscriber_;
};

}  // namespace fabric