#pragma once

#include <fabric_base/utils/monitoring_structs.hpp>
#include <fabric_base/utils/plan_structs.hpp>
#include <fabric_base/utils/status_structs.hpp>

namespace fabric
{
using event = plan::event;
using node = plan::node;
using connection = plan::connection;
using PlanStatus = plan::Status;
using FabricMetaData = plan::MetaData;
using Plan = plan::Plan;

using Time = status::Time;
using Header = status::Header;
using BatteryRunInterval = status::BatteryRunInterval;
using BatteryUsage = status::BatteryUsage;
using RunEvidence = status::RunEvidence;
using ExecutionLifecycle = status::ExecutionLifecycle;
using FabricNodeLifecycle = status::FabricNodeLifecycle;
using LogRecord = status::LogRecord;
using CapabilityStatus = status::CapabilityStatus;
using SystemStatus = status::SystemStatus;
using MonitoringStatus = monitor::MonitoringStatus;
using ComponentStatus = monitor::ComponentStatus;
using LoggingData = monitor::LoggingData;
using DiagnosticData = monitor::DiagnosticData;

}  // namespace fabric