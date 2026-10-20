#include <pluginlib/class_list_macros.hpp>

#include <fabric_monitor/capability_event_monitor.hpp>
#include <fabric_monitor/execution_status_monitor.hpp>
#include <fabric_monitor/resource_monitor.hpp>
#include <fabric_monitor/rosdiagnostics_monitor.hpp>
#include <fabric_monitor/rosout_monitor.hpp>

PLUGINLIB_EXPORT_CLASS(fabric::ExecutionStatusMonitor, fabric::MonitorBase)
PLUGINLIB_EXPORT_CLASS(fabric::CapabilityEventMonitor, fabric::MonitorBase)
PLUGINLIB_EXPORT_CLASS(fabric::ResourceMonitor, fabric::MonitorBase)
PLUGINLIB_EXPORT_CLASS(fabric::RosoutMonitor, fabric::MonitorBase)
PLUGINLIB_EXPORT_CLASS(fabric::RosDiagnosticsMonitor, fabric::MonitorBase)