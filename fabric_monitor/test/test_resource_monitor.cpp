#include <any>
#include <chrono>
#include <memory>
#include <limits>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <pluginlib/class_loader.hpp>
#include <fabric_monitor/battery_monitor.hpp>
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
    rclcpp::NodeOptions options;
    options.parameter_overrides(
        { rclcpp::Parameter("BatteryMonitor.enable", true), rclcpp::Parameter("BatteryMonitor.topic", "/battery_"
                                                                                                      "state") });
    node_ = std::make_shared<rclcpp::Node>("resource_monitor_test", options);
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
  auto battery_plugin = loader.createSharedInstance("fabric::BatteryMonitor");
  ASSERT_NE(battery_plugin, nullptr);
}

TEST_F(ResourceMonitorTest, DisabledBatteryMonitorDoesNotSubscribe)
{
  node_->declare_parameter<bool>("BatteryMonitor.enable", true);
  node_->set_parameter(rclcpp::Parameter("BatteryMonitor.enable", false));
  fabric::BatteryMonitor battery_monitor;
  battery_monitor.initialize(node_);
  battery_monitor.start();
  EXPECT_EQ(node_->count_subscribers("/battery_state"), 0u);
  const auto data = std::any_cast<fabric::monitor::BatteryData>(battery_monitor.getData());
  EXPECT_DOUBLE_EQ(data.percentage, -1.0);
  EXPECT_TRUE(data.warnings.empty());
  battery_monitor.stop();
}

TEST_F(ResourceMonitorTest, BatteryMonitorRetainsReadingsAndDrainsWarnings)
{
  fabric::BatteryMonitor battery_monitor;
  battery_monitor.initialize(node_);
  battery_monitor.start();
  auto publisher =
      node_->create_publisher<sensor_msgs::msg::BatteryState>("/battery_state", rclcpp::QoS(10).best_effort());
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (publisher->get_subscription_count() == 0 && std::chrono::steady_clock::now() < deadline)
  {
    executor_.spin_once(10ms);
  }
  ASSERT_GT(publisher->get_subscription_count(), 0u);
  for (const float percentage : { 0.8f, 0.4f, 0.2f, 0.1f, -1.0f, std::numeric_limits<float>::quiet_NaN(), 1.1f })
  {
    sensor_msgs::msg::BatteryState message;
    message.header.stamp.sec = 123;
    message.percentage = percentage;
    publisher->publish(message);
    const auto receive_deadline = std::chrono::steady_clock::now() + 200ms;
    while (std::chrono::steady_clock::now() < receive_deadline)
    {
      executor_.spin_once(10ms);
    }
    const auto data = std::any_cast<fabric::monitor::BatteryData>(battery_monitor.getData());
    EXPECT_DOUBLE_EQ(data.percentage, std::isfinite(percentage) && percentage >= 0.0f && percentage <= 1.0f ?
                                          static_cast<double>(percentage) :
                                          -1.0);
    if (percentage >= 0.0f && percentage < 0.4f)
    {
      ASSERT_EQ(data.warnings.size(), 1u);
      EXPECT_EQ(data.warnings.front().timestamp.sec, 123);
      EXPECT_EQ(data.warnings.front().status, percentage < 0.2f ? fabric::monitor::MonitoringStatus::FAULTY :
                                                                  fabric::monitor::MonitoringStatus::SUSPICIOUS);
    }
    else
    {
      EXPECT_TRUE(data.warnings.empty());
    }
    const auto retained = std::any_cast<fabric::monitor::BatteryData>(battery_monitor.getData());
    EXPECT_DOUBLE_EQ(retained.percentage, data.percentage);
    EXPECT_TRUE(retained.warnings.empty());
  }
  battery_monitor.stop();
  EXPECT_DOUBLE_EQ(std::any_cast<fabric::monitor::BatteryData>(battery_monitor.getData()).percentage, -1.0);
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