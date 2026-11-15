# Capability Analytics in Fabric

This document explains how Fabric turns raw telemetry into capability-centric runtime evidence.

The current Fabric runtime no longer relies on `supervisor_msgs/SystemStatus`. Instead, Fabric builds `fabric::status::SystemStatus` snapshots directly from:

- capability events from `capabilities2`,
- Fabric execution state from `/fabric/status`,
- rosout summaries,
- diagnostic summaries,
- optional battery snapshots from `/battery_state`.

The goal is still capability-centric analytics, but the ownership boundary is now different: monitor plugins collect or normalize signals, and `fabric_server` performs the final join and persistence-oriented enrichment.

## Inputs

Fabric currently combines these telemetry sources:

- `CapabilityEventMonitor`: polls `/capabilities/get_event_snapshot` and reconstructs capability lifecycle and run evidence.
- `ExecutionStatusMonitor`: subscribes to `/fabric/status` and captures plan-level execution metadata such as `plan_id`, planning request ID, candidate plan ID, graph hash, and plan state.
- `RosoutMonitor`: summarizes `/rosout` messages into per-node `ComponentStatus` entries.
- `RosDiagnosticsMonitor`: summarizes `/diagnostics` messages into per-component `ComponentStatus` entries.
- Battery subscription in `fabric_server`: caches the latest `sensor_msgs/BatteryState` and attaches per-capability battery intervals during enrichment.

The execution and capability monitors produce Fabric-owned `SystemStatus` data directly. Log and diagnostics monitors produce `ComponentStatus` maps that are first converted into generic telemetry records and then correlated with active capability windows.

## Canonical Data Model

Fabric uses `fabric_base/include/fabric_base/utils/status_structs.hpp` as the canonical telemetry model.

At the top level, a `SystemStatus` contains:

- plan identity and execution metadata,
- zero or more `CapabilityStatus` entries.

Each `CapabilityStatus` can carry:

- lifecycle state,
- run evidence,
- battery usage,
- related nodes observed while the capability was active,
- correlated warning/error log records.

This replaces the old Supervisor message boundary with plain Fabric-owned structs.

## Join Strategy

Fabric joins telemetry in two stages.

### 1. Capability Reconstruction

`CapabilityEventMonitor` rebuilds per-capability lifecycle and run evidence from `capabilities2` event snapshots. It groups capability evidence by `plan_id` and uses the tracking key:

$$
\text{plan\_id} / \text{instance\_id} / \text{capability\_name}
$$

This preserves the current execution identity scheme while remaining independent of Supervisor.

### 2. Enrichment in `fabric_server`

When `fabric_server` ingests capability-bearing `SystemStatus` snapshots, it enriches each capability with:

- execution metadata merged from cached `/fabric/status` snapshots,
- correlated rosout and diagnostic evidence observed during the capability's active time window,
- battery intervals sampled when the capability starts and stops.

The helper logic for correlation, shutdown-safety checks, and battery interval accumulation lives in `fabric_server/include/fabric_server/utils/status.hpp`.

## Correlating Logs and Diagnostics

Rosout and diagnostics plugins do not emit capability timelines directly. They emit normalized `ComponentStatus` values, which Fabric converts into `TelemetryRecord` entries with:

- timestamp,
- source name,
- textual description,
- warning or error label.

For each capability, Fabric keeps only the telemetry records whose timestamps fall within that capability's active lifecycle window. Those records are then used to populate:

- `CapabilityStatus.log_records`,
- `CapabilityStatus.related_nodes`.

If two capabilities overlap in time, the same record may legitimately be associated with both. Fabric keeps all plausible associations and leaves stricter attribution to downstream analysis.

## Battery Usage

Battery monitoring is now Fabric-owned.

When enabled, `fabric_server` subscribes to `/battery_state` and caches the latest percentage. During capability enrichment:

- if a capability started in the current cycle, Fabric records the battery percentage as `last_start_percentage`,
- if a capability stopped in the current cycle, Fabric records the battery percentage as `last_stop_percentage`,
- it then appends a `BatteryRunInterval` and updates cumulative drop statistics.

Because multiple capabilities can overlap, Fabric intentionally does not try to attribute a shared battery drop to only one capability. It preserves intervals per capability and leaves finer attribution to downstream analysis.

## Shutdown Safety

Fabric also computes a simple shutdown-safety summary from capability evidence.

The current rules classify the system as:

- `SAFE_TO_KILL` when all tracked capabilities are inactive and have terminal evidence,
- `DO_NOT_KILL` when any capability is still active or incomplete,
- `UNKNOWN` when no publishable capability evidence has been observed.

These checks are implemented on Fabric-owned structs in `status.hpp` and are no longer tied to Supervisor messages.

## What Was Not Carried Forward

The old Supervisor stack also documented dynamics and odometry monitoring as inputs to capability analytics. Those paths were intentionally not carried into the merged Fabric runtime.

That means the active Fabric capability analytics path is now centered on:

- capability events,
- execution status,
- logs,
- diagnostics,
- battery telemetry.

This matches the current system goal: planning support and execution evidence, not low-level failure prediction and recovery.