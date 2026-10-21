#include <any>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <pluginlib/class_loader.hpp>
#include <fabric_monitor/capability_event_monitor.hpp>
#include <fabric_monitor/resource_monitor.hpp>

using namespace std::chrono_literals;

class ResourceMonitorTest : public ::testing::Test
{
protected:
  static void SetUpTestSuite()
  {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite()
  {
    rclcpp::shutdown();
  }

  void SetUp() override
  {
    node_ = std::make_shared<rclcpp::Node>("resource_monitor_test");
    monitor_.initialize(node_);
    publisher_ =
        node_->create_publisher<fabric::ResourceMonitor::ResourceSnapshot>("/capabilities2_launch_proxy/resources", 10);
    executor_.add_node(node_);
    monitor_.start();
  }

  void TearDown() override
  {
    monitor_.stop();
    executor_.remove_node(node_);
    publisher_.reset();
    node_.reset();
  }

  std::vector<fabric::ResourceMonitor::ResourceSnapshot> takeSnapshots()
  {
    return std::any_cast<std::vector<fabric::ResourceMonitor::ResourceSnapshot>>(monitor_.getData());
  }

  void publishAndSpin(const fabric::ResourceMonitor::ResourceSnapshot& snapshot)
  {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (publisher_->get_subscription_count() == 0 && std::chrono::steady_clock::now() < deadline)
    {
      executor_.spin_some();
      std::this_thread::yield();
    }
    ASSERT_GT(publisher_->get_subscription_count(), 0u);
    publisher_->publish(snapshot);
    const auto receive_deadline = std::chrono::steady_clock::now() + 1s;
    while (std::chrono::steady_clock::now() < receive_deadline)
    {
      executor_.spin_once(10ms);
    }
  }

  rclcpp::Node::SharedPtr node_;
  fabric::ResourceMonitor monitor_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  rclcpp::Publisher<fabric::ResourceMonitor::ResourceSnapshot>::SharedPtr publisher_;
};

TEST_F(ResourceMonitorTest, PreservesMeasurementsAndDrainsSnapshots)
{
  EXPECT_TRUE(takeSnapshots().empty());
  fabric::ResourceMonitor::ResourceSnapshot snapshot;
  snapshot.header.stamp.sec = 123;
  snapshot.active_launch_count = 1;
  snapshot.main_process_count = 1;
  snapshot.child_process_count = 1;
  snapshot.resource_count = 2;
  snapshot.total_cpu_usage_percent = 140.0f;
  snapshot.total_memory_rss_bytes = 8192;
  capabilities2_msgs::msg::LaunchResource resource;
  resource.key = "launch/process";
  resource.reference_name = "capability-instance";
  resource.launch_id = "launch";
  resource.state = "running";
  resource.pid = 42;
  resource.parent_pid = 41;
  resource.is_main_process = true;
  resource.cpu_usage_percent = 120.0f;
  resource.peak_memory_rss_bytes = 16384;
  resource.time_above_90pct_peak_rss_sec = 2.5f;
  snapshot.resources.push_back(resource);
  resource.is_main_process = false;
  resource.pid = 43;
  resource.parent_pid = 42;
  snapshot.resources.push_back(resource);

  publishAndSpin(snapshot);
  auto snapshots = takeSnapshots();
  ASSERT_EQ(snapshots.size(), 1u);
  EXPECT_EQ(snapshots.front(), snapshot);
  EXPECT_TRUE(takeSnapshots().empty());
}

TEST_F(ResourceMonitorTest, KeepsLatestSnapshotAndReportsEmptyLaunchSet)
{
  fabric::ResourceMonitor::ResourceSnapshot snapshot;
  snapshot.active_launch_count = 1;
  publishAndSpin(snapshot);
  snapshot.active_launch_count = 0;
  publishAndSpin(snapshot);
  auto snapshots = takeSnapshots();
  ASSERT_EQ(snapshots.size(), 1u);
  EXPECT_EQ(snapshots.front().active_launch_count, 0u);
  monitor_.stop();
  EXPECT_TRUE(takeSnapshots().empty());
}

TEST_F(ResourceMonitorTest, PluginCanBeLoaded)
{
  pluginlib::ClassLoader<fabric::MonitorBase> loader("fabric_base", "fabric::MonitorBase");
  auto plugin = loader.createSharedInstance("fabric::ResourceMonitor");
  ASSERT_NE(plugin, nullptr);
}

TEST_F(ResourceMonitorTest, EventMonitorStopsWithPendingSnapshot)
{
  using Snapshot = fabric::CapabilityEventMonitor::GetEventSnapshot;
  bool request_received = false;
  auto service = node_->create_service<Snapshot>(
    "/capabilities/get_event_snapshot",
    [&request_received](std::shared_ptr<rmw_request_id_t>, std::shared_ptr<Snapshot::Request>) {
      request_received = true;
    });
  fabric::CapabilityEventMonitor event_monitor;
  event_monitor.initialize(node_);
  event_monitor.start();

  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!request_received && std::chrono::steady_clock::now() < deadline)
  {
    executor_.spin_once(10ms);
  }

  const auto stop_started = std::chrono::steady_clock::now();
  event_monitor.stop();
  EXPECT_TRUE(request_received);
  EXPECT_LT(std::chrono::steady_clock::now() - stop_started, 1500ms);
  EXPECT_TRUE(rclcpp::ok());
}