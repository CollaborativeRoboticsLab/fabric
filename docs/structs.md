# Fabric Structs and Their Usage

This document describes the core data structures used in Fabric, including both plan structs and runtime telemetry structs.

## Overview

The main structs are defined in:

- `fabric_base/utils/plan_structs.hpp`
- `fabric_base/utils/metadata_structs.hpp`
- `fabric_base/utils/status_structs.hpp`

Together they represent plans, capability metadata, and runtime evidence in the merged Fabric execution engine.

## Structs Summary

| Struct         | Purpose                                                      |
|---------------|--------------------------------------------------------------|
| `fabric::plan::node`        | Represents a single execution node (runner/capability)       |
| `fabric::plan::connection`  | Represents a directed connection between nodes               |
| `fabric::plan::Plan`        | Represents a parsed plan, including all connections          |
| `fabric::capability::Info`  | Describes a capability's interface, provider, and metadata |
| `fabric::plan::event`       | Enumerates supported event hooks (`ON_START`, `ON_SUCCESS`, `ON_FAILURE`, `ON_STOP`) |
| `fabric::status::SystemStatus` | Top-level runtime evidence snapshot grouped by plan ID |
| `fabric::status::CapabilityStatus` | Capability-centric lifecycle, outcome, battery, and correlation evidence |

---

## `fabric::plan::node`

Represents a single runner or capability in the plan.

| Field         | Type                    | Description                                 |
|---------------|-------------------------|---------------------------------------------|
| `interface`   | `std::string`           | Interface name of the runner/capability     |
| `provider`    | `std::string`           | Provider name of the runner/capability      |
| `parameters`  | `fabric::EventParameters` | Runner parameters converted from XML attributes |
| `instance_id` | `int`                   | Parser-assigned instance identifier         |

**Key Methods:**
- `exists() const`: Returns true when both `interface` and `provider` are set.

**Usage:**
- Used as the source and target in `fabric::plan::connection`.
- Populated by the parser when extracting runners from the XML plan.

---

## `fabric::plan::connection`

Represents a directed edge in the plan's execution graph, connecting nodes and specifying control flow.

| Field         | Type        | Description                                 |
|---------------|-------------|---------------------------------------------|
| `source`      | `fabric::plan::node`      | The source node (runner/capability)         |
| `on_start`    | `fabric::plan::node`      | Node to trigger on start event              |
| `on_stop`     | `fabric::plan::node`      | Node to trigger on stop event               |
| `on_success`  | `fabric::plan::node`      | Node to trigger on success event            |
| `on_failure`  | `fabric::plan::node`      | Node to trigger on failure event            |
| `description` | `std::string` | Human-readable description                 |

**Usage:**
- Populated by the parser when traversing the XML plan.
- Used by the server to determine execution flow and event handling.

---

## `fabric::plan::Plan`

Represents the entire parsed plan, including all connections and metadata.

| Field           | Type                          | Description                                 |
|-----------------|-------------------------------|---------------------------------------------|
| `plan_id`       | `std::string`                 | Generated identifier used for tracking      |
| `bond_id`       | `std::string`                 | Unique bond identifier for the plan         |
| `plan`          | `std::string`                 | Raw XML plan as a string                    |
| `metadata`      | `fabric::plan::MetaData`     | Optional metadata carried with the plan |
| `connections`   | `std::map<int, fabric::plan::connection>`   | All connections in the plan                 |
| `rejected_list` | `std::vector<std::string>`    | List of rejected/invalid elements           |
| `status`        | `fabric::plan::Status`        | Current lifecycle state for the plan        |

**Usage:**
- Created and populated by the parser plugin.
- Managed by the server (`plan_queue_`, `current_plan_`).
- Used to drive the execution and capability allocation.

---

## `fabric::capability::Info`

Describes a capability's interface, provider, and related metadata.

| Field           | Type                    | Description                                 |
|-----------------|-------------------------|---------------------------------------------|
| `interface`     | `std::string`           | Interface name of the capability            |
| `provider`      | `std::string`           | Provider name of the capability             |
| `is_semantic`   | `bool`                  | Whether the capability is semantic          |
| `alt_providers` | `std::vector<std::string>` | Alternative providers for the capability |

**Usage:**
- Populated by the capability client.
- Used by the server to allocate and manage capabilities for a plan.

---

## Example Usage in the System

- The **parser plugin** reads an XML plan and populates a `Plan` object, filling its `connections` map with `connection` structs, each referencing `node` structs for runners and control flow.

- The **server** manages a queue of `Plan` objects (`plan_queue_`), processes each plan, and uses the `connections` to trigger capabilities and manage execution events.

- **fabric::capability::Info** is used to track and allocate the required capabilities for each plan.

This struct-based design enables clear separation of concerns and extensibility for new plan types, events, capabilities, and telemetry sources.

---

## Runtime Telemetry Structs

Fabric also defines a runtime telemetry model in `fabric_base/utils/status_structs.hpp`.

These structs replaced the old Supervisor message boundary and are now the canonical internal evidence model used by monitor plugins, `fabric_server`, and the Experience layer.

### `fabric::status::SystemStatus`

Top-level runtime snapshot for one Fabric plan.

| Field | Type | Description |
|---|---|---|
| `header` | `fabric::status::Header` | Snapshot timestamp metadata. |
| `plan_id` | `std::string` | Plan identifier. |
| `planning_request_id` | `std::string` | Planning request identity propagated from selected-plan generation. |
| `candidate_plan_id` | `std::string` | Selected candidate plan identity. |
| `graph_hash` | `std::string` | Hash of the selected candidate graph. |
| `bond_id` | `std::string` | Capability bond identifier for the running plan. |
| `capability_plan_state` | `uint8_t` | Fabric plan lifecycle state encoded like `fabric_msgs/msg/FabricStatus`. |
| `capabilities` | `std::vector<CapabilityStatus>` | Per-capability execution evidence. |

### `fabric::status::CapabilityStatus`

Aggregated evidence for one capability execution track.

| Field | Type | Description |
|---|---|---|
| `capability_name` | `std::string` | Capability name. |
| `bond_id` | `std::string` | Optional bond ID associated with the capability run. |
| `instance_id` | `std::string` | Capability instance identifier used in the execution tracking key. |
| `has_lifecycle` | `bool` | True when lifecycle fields are populated. |
| `has_battery_usage` | `bool` | True when battery-usage fields are populated. |
| `has_run_evidence` | `bool` | True when run-evidence fields are populated. |
| `lifecycle` | `ExecutionLifecycle` | Last start/stop times plus active and transition flags. |
| `battery_usage` | `BatteryUsage` | Per-run and cumulative battery usage information. |
| `run_evidence` | `RunEvidence` | Inferred counters and latest terminal outcome. |
| `related_nodes` | `std::vector<FabricNodeLifecycle>` | Nodes or components observed while this capability was active. |
| `log_records` | `std::vector<LogRecord>` | Warning/error records correlated with the capability window. |

### `fabric::status::ExecutionLifecycle`

Tracks the capability lifecycle window:

- last start and stop timestamps,
- whether those timestamps are valid,
- whether the capability is currently active,
- whether start or stop transitions happened in the current processing cycle.

### `fabric::status::RunEvidence`

Stores inferred outcome counters such as:

- `runs_started`
- `runs_completed`
- `runs_succeeded`
- `runs_failed`
- `runs_aborted`
- `runs_timed_out`
- `last_outcome_time`
- `last_outcome_label`

### `fabric::status::BatteryUsage`

Stores battery-derived execution evidence:

- last observed start and stop percentages,
- last run delta,
- cumulative delta across completed runs,
- number of completed runs,
- a history of `BatteryRunInterval` entries.

### `fabric::status::FabricNodeLifecycle`

Represents when a node or component was first and last observed while a capability was active.

### `fabric::status::LogRecord`

Represents a warning or error correlated with a capability execution window.

| Field | Description |
|---|---|
| `capability_name` | Capability that was active when the record was correlated. |
| `timestamp` | Time of the source telemetry record. |
| `source` | Reporting node or component name. |
| `description` | Human-readable warning/error description. |
| `label` | Severity label, where Fabric currently uses `1` for warning and `2` for error. |

## Note on Missing Supervisor Message Docs

The old `supervisor/docs/message_information.md` is not copied as a standalone Fabric document because its conceptual content is now covered by the Fabric telemetry structs above. Fabric uses structs in `status_structs.hpp` rather than a Supervisor-owned internal message schema.
