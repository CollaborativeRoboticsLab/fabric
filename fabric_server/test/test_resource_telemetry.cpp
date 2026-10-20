#include <gtest/gtest.h>

#include <fabric_server/utils/telemetry.hpp>

namespace
{
fabric::status::SystemStatus capabilityStatus()
{
  fabric::status::SystemStatus status;
  status.plan_id = "plan-1";
  fabric::status::CapabilityStatus capability;
  capability.capability_name = "demo/Launch";
  capability.instance_id = "instance-1";
  status.capabilities.push_back(capability);
  return status;
}
}  // namespace

TEST(ResourceTelemetry, DistinguishesMissingMeasurementsFromZeroUsage)
{
  fabric::server::telemetry::Aggregator aggregator;
  auto statuses = aggregator.ingestSystemStatuses({capabilityStatus()}, 0, false, -1.0);
  ASSERT_EQ(statuses.size(), 1u);
  EXPECT_FALSE(statuses.front().has_resource_usage);

  capabilities2_msgs::msg::LaunchResourceList snapshot;
  snapshot.header.stamp.sec = 12;
  aggregator.ingestResourceSnapshots({snapshot});
  statuses = aggregator.ingestSystemStatuses({capabilityStatus()}, 0, false, -1.0);
  ASSERT_EQ(statuses.size(), 1u);
  EXPECT_TRUE(statuses.front().has_resource_usage);
  EXPECT_EQ(statuses.front().resource_usage.header.stamp.sec, 12);
  EXPECT_EQ(statuses.front().resource_usage.active_launch_count, 0u);
  EXPECT_TRUE(statuses.front().resource_usage.resources.empty());
}

TEST(ResourceTelemetry, PreservesProxyWideMeasurementsWithoutInventingCapabilityAttribution)
{
  fabric::server::telemetry::Aggregator aggregator;
  capabilities2_msgs::msg::LaunchResourceList snapshot;
  snapshot.header.stamp.sec = 123;
  snapshot.header.stamp.nanosec = 456;
  snapshot.header.frame_id = "host";
  snapshot.active_launch_count = 2;
  snapshot.main_process_count = 2;
  snapshot.child_process_count = 1;
  snapshot.resource_count = 3;
  snapshot.total_cpu_usage_percent = 180.0f;
  snapshot.total_peak_sampled_cpu_usage_percent = 230.0f;
  snapshot.total_time_above_80pct_peak_sampled_cpu_sec = 8.0f;
  snapshot.total_memory_rss_bytes = 8192;
  snapshot.total_peak_memory_rss_bytes = 16384;
  snapshot.total_time_above_90pct_peak_rss_sec = 3.0f;
  snapshot.total_memory_vms_bytes = 32768;
  snapshot.total_peak_memory_vms_bytes = 65536;
  snapshot.total_time_above_90pct_peak_vms_sec = 4.0f;
  capabilities2_msgs::msg::LaunchResource resource;
  resource.key = "launch/process";
  resource.reference_name = "instance-1";
  resource.launch_id = "launch-1";
  resource.state = "running";
  resource.resolved_launch_file_path = "/tmp/demo.launch.py";
  resource.process_name = "worker";
  resource.pid = 42;
  resource.parent_pid = 41;
  resource.is_main_process = true;
  resource.launch_uptime_sec = 20.0f;
  resource.process_uptime_sec = 19.0f;
  resource.cpu_usage_percent = 120.0f;
  resource.peak_sampled_cpu_usage_percent = 160.0f;
  resource.time_above_80pct_peak_sampled_cpu_sec = 5.0f;
  resource.memory_rss_bytes = 4096;
  resource.peak_memory_rss_bytes = 8192;
  resource.time_above_90pct_peak_rss_sec = 2.0f;
  resource.memory_vms_bytes = 16384;
  resource.peak_memory_vms_bytes = 32768;
  resource.time_above_90pct_peak_vms_sec = 3.0f;
  snapshot.resources.push_back(resource);

  aggregator.ingestResourceSnapshots({snapshot});
  aggregator.ingestResourceSnapshots({});
  const auto statuses = aggregator.ingestSystemStatuses({capabilityStatus()}, 0, false, -1.0);
  ASSERT_EQ(statuses.size(), 1u);
  ASSERT_TRUE(statuses.front().has_resource_usage);
  const auto & usage = statuses.front().resource_usage;
  EXPECT_EQ(usage.header.stamp.sec, snapshot.header.stamp.sec);
  EXPECT_EQ(usage.header.stamp.nanosec, snapshot.header.stamp.nanosec);
  EXPECT_EQ(usage.header.frame_id, snapshot.header.frame_id);
  EXPECT_EQ(usage.active_launch_count, snapshot.active_launch_count);
  EXPECT_EQ(usage.main_process_count, snapshot.main_process_count);
  EXPECT_EQ(usage.child_process_count, snapshot.child_process_count);
  EXPECT_EQ(usage.resource_count, snapshot.resource_count);
  EXPECT_EQ(usage.total_cpu_usage_percent, snapshot.total_cpu_usage_percent);
  EXPECT_EQ(usage.total_peak_sampled_cpu_usage_percent,
    snapshot.total_peak_sampled_cpu_usage_percent);
  EXPECT_EQ(usage.total_time_above_80pct_peak_sampled_cpu_sec,
    snapshot.total_time_above_80pct_peak_sampled_cpu_sec);
  EXPECT_EQ(usage.total_memory_rss_bytes, snapshot.total_memory_rss_bytes);
  EXPECT_EQ(usage.total_peak_memory_rss_bytes, snapshot.total_peak_memory_rss_bytes);
  EXPECT_EQ(usage.total_time_above_90pct_peak_rss_sec,
    snapshot.total_time_above_90pct_peak_rss_sec);
  EXPECT_EQ(usage.total_memory_vms_bytes, snapshot.total_memory_vms_bytes);
  EXPECT_EQ(usage.total_peak_memory_vms_bytes, snapshot.total_peak_memory_vms_bytes);
  EXPECT_EQ(usage.total_time_above_90pct_peak_vms_sec,
    snapshot.total_time_above_90pct_peak_vms_sec);
  ASSERT_EQ(usage.resources.size(), 1u);
  const auto & process = usage.resources.front();
  EXPECT_EQ(process.key, resource.key);
  EXPECT_EQ(process.reference_name, resource.reference_name);
  EXPECT_EQ(process.launch_id, resource.launch_id);
  EXPECT_EQ(process.state, resource.state);
  EXPECT_EQ(process.resolved_launch_file_path, resource.resolved_launch_file_path);
  EXPECT_EQ(process.process_name, resource.process_name);
  EXPECT_EQ(process.pid, resource.pid);
  EXPECT_EQ(process.parent_pid, resource.parent_pid);
  EXPECT_EQ(process.is_main_process, resource.is_main_process);
  EXPECT_EQ(process.launch_uptime_sec, resource.launch_uptime_sec);
  EXPECT_EQ(process.process_uptime_sec, resource.process_uptime_sec);
  EXPECT_EQ(process.cpu_usage_percent, resource.cpu_usage_percent);
  EXPECT_EQ(process.peak_sampled_cpu_usage_percent, resource.peak_sampled_cpu_usage_percent);
  EXPECT_EQ(process.time_above_80pct_peak_sampled_cpu_sec,
    resource.time_above_80pct_peak_sampled_cpu_sec);
  EXPECT_EQ(process.memory_rss_bytes, resource.memory_rss_bytes);
  EXPECT_EQ(process.peak_memory_rss_bytes, resource.peak_memory_rss_bytes);
  EXPECT_EQ(process.time_above_90pct_peak_rss_sec, resource.time_above_90pct_peak_rss_sec);
  EXPECT_EQ(process.memory_vms_bytes, resource.memory_vms_bytes);
  EXPECT_EQ(process.peak_memory_vms_bytes, resource.peak_memory_vms_bytes);
  EXPECT_EQ(process.time_above_90pct_peak_vms_sec, resource.time_above_90pct_peak_vms_sec);
  EXPECT_EQ(statuses.front().capabilities.front().instance_id, "instance-1");
}

TEST(ResourceTelemetry, EmptyLaunchSnapshotClearsPreviousResources)
{
  fabric::server::telemetry::Aggregator aggregator;
  capabilities2_msgs::msg::LaunchResourceList active;
  active.active_launch_count = 1;
  active.total_memory_rss_bytes = 8192;
  active.resources.emplace_back();
  aggregator.ingestResourceSnapshots({active, capabilities2_msgs::msg::LaunchResourceList{}});
  const auto statuses = aggregator.ingestSystemStatuses({capabilityStatus()}, 0, false, -1.0);
  ASSERT_EQ(statuses.size(), 1u);
  EXPECT_TRUE(statuses.front().has_resource_usage);
  EXPECT_EQ(statuses.front().resource_usage.total_memory_rss_bytes, 0u);
  EXPECT_TRUE(statuses.front().resource_usage.resources.empty());
}
