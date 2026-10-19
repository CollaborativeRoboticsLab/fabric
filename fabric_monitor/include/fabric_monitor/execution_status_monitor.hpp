#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/monitor_base.hpp>
#include <fabric_base/utils/status_structs.hpp>
#include <fabric_msgs/msg/fabric_status.hpp>

namespace fabric
{

class ExecutionStatusMonitor : public MonitorBase
{
public:
  using SystemStatus = fabric::status::SystemStatus;

  ExecutionStatusMonitor() = default;
  ~ExecutionStatusMonitor() override = default;

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "ExecutionStatusMonitor");
  }

  void start() override
  {
    auto qos = rclcpp::QoS(rclcpp::KeepLast(10)).transient_local().reliable();
    status_subscriber_ = node_->create_subscription<fabric_msgs::msg::FabricStatus>(
        monitor_topic_, qos, std::bind(&ExecutionStatusMonitor::statusCallback, this, std::placeholders::_1));
    monitoring_active_ = true;
  }

  void stop() override
  {
    monitoring_active_ = false;
    status_subscriber_.reset();
  }

  std::any getData() override
  {
    std::lock_guard<std::mutex> lock(results_mutex_);
    std::vector<SystemStatus> snapshot;
    snapshot.reserve(pending_statuses_.size());
    for (auto& entry : pending_statuses_)
    {
      snapshot.push_back(std::move(entry.second));
    }
    pending_statuses_.clear();
    return snapshot;
  }

private:
  void statusCallback(const fabric_msgs::msg::FabricStatus::ConstSharedPtr& msg)
  {
    if (!monitoring_active_)
    {
      return;
    }

    SystemStatus status;
    const auto now = node_->now();
    status.header.stamp.sec = static_cast<int32_t>(now.seconds());
    status.header.stamp.nanosec = static_cast<uint32_t>(now.nanoseconds() % 1000000000ULL);
    status.plan_id = msg->plan_id;
    status.planning_request_id = msg->metadata.planning_request_id;
    status.candidate_plan_id = msg->metadata.candidate_plan_id;
    status.graph_hash = msg->metadata.graph_hash;
    status.capability_plan_state = msg->code;

    std::lock_guard<std::mutex> lock(results_mutex_);
    pending_statuses_[status.plan_id] = std::move(status);
  }

  std::mutex results_mutex_;
  std::map<std::string, SystemStatus> pending_statuses_;
  rclcpp::Subscription<fabric_msgs::msg::FabricStatus>::SharedPtr status_subscriber_;
};

}  // namespace fabric