# Resource Monitor in Fabric

`fabric::ResourceMonitor` is a data-only `MonitorBase` plugin in `fabric_monitor`.
It collects the resource measurements already published by the capabilities2
launch proxy; it does not inspect processes itself or depend on Supervisor.

## Data Source

The default topic is `/capabilities2_launch_proxy/resources`, carrying
`capabilities2_msgs/msg/LaunchResourceList`. Set `ResourceMonitor.topic` if the
proxy has a different node name or namespace. The subscription uses reliable,
volatile QoS with a depth of 10, matching the proxy publisher.

Measurements include CPU percentage, RSS/VMS memory in bytes, sampled peaks,
time near those peaks, launch/process uptime, process and parent PIDs, and launch
identity. CPU percentages are preserved without clamping to 100 percent because
processes can use multiple cores. Aggregate totals retain the proxy's semantics;
the monitor does not recompute or sum them.

## Output And Ingestion

`getData()` returns `std::vector<capabilities2_msgs::msg::LaunchResourceList>`.
Only the latest unread snapshot is retained, so the buffer is bounded. Reading
drains it; an empty vector means no new measurement. A received snapshot with
zero active launches is valid data and clears earlier process measurements.
Stopping the plugin releases the subscription and clears unread measurements.

The plugin is enabled in `fabric_server/config/fabric.yaml`. Fabric's telemetry
aggregator converts its output to the ROS-independent `fabric::status::ResourceUsage`
and attaches it to capability-bearing `SystemStatus` records. The
`has_resource_usage` flag distinguishes unavailable telemetry from measured zero
usage. The source timestamp is preserved: these are last-observed measurements,
not a freshness guarantee. There is no historical sample store in this monitor.
The server's `telemetry.monitor_poll_frequency_hz` controls collection from plugins;
the proxy controls the sampling/publication rate.

## Remaining Validation

These snapshots describe the entire launch proxy, not an individual Fabric plan.
Launch/reference identifiers and process relationships are retained for subsequent
correlation. Shared processes must not be charged independently to multiple
capabilities. Per-capability distributions, terminal resource persistence, resource
scoring, and peak-resource feasibility remain separate work. Resource scoring is
not enabled by adding this monitor.

Focused tests cover ROS subscription delivery, all wire fields, drain/latest-only
behavior, plugin loading, native telemetry conversion, and empty launch snapshots.
A live launch-proxy smoke test and capability attribution validation remain needed.