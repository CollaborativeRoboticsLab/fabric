#pragma once

#include <any>
#include <mutex>
#include <string>
#include <vector>

#include <capabilities2_msgs/msg/launch_resource_list.hpp>
#include <fabric_base/monitor_base.hpp>

namespace fabric
{

class ResourceMonitor : public MonitorBase
{
public:
  using ResourceSnapshot = capabilities2_msgs::msg::LaunchResourceList;

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "ResourceMonitor");
    if (monitor_topic_.empty())
    {
      monitor_topic_ = "/capabilities2_launch_proxy/resources";
    }
  }

  void start() override
  {
    monitoring_active_ = true;
    resource_subscriber_ = node_->create_subscription<ResourceSnapshot>(
        monitor_topic_, rclcpp::QoS(10), [this](ResourceSnapshot::ConstSharedPtr msg) {
          std::lock_guard<std::mutex> lock(results_mutex_);
          if (monitoring_active_)
          {
            pending_snapshots_ = { *msg };
          }
        });
  }

  void stop() override
  {
    monitoring_active_ = false;
    resource_subscriber_.reset();
    std::lock_guard<std::mutex> lock(results_mutex_);
    pending_snapshots_.clear();
  }

  std::any getData() override
  {
    std::lock_guard<std::mutex> lock(results_mutex_);
    std::vector<ResourceSnapshot> snapshots;
    snapshots.swap(pending_snapshots_);
    return snapshots;
  }

private:
  std::mutex results_mutex_;
  std::vector<ResourceSnapshot> pending_snapshots_;
  rclcpp::Subscription<ResourceSnapshot>::SharedPtr resource_subscriber_;
};

}  // namespace fabric