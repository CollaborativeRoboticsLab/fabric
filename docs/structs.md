# Fabric Structs and Their Usage

This document describes the core data structures (structs) used in Fabric, their fields, and how they are used throughout the system.

## Overview

The main structs are defined in `fabric_base/structs.hpp` and are central to representing plans, nodes, connections, and capabilities in the Fabric execution engine.

## Structs Summary

| Struct         | Purpose                                                      |
|---------------|--------------------------------------------------------------|
| `node`        | Represents a single execution node (runner/capability)       |
| `connection`  | Represents a directed connection between nodes               |
| `Plan`        | Represents a parsed plan, including all connections          |
| `CapabilityInfo` | Describes a capability's interface, provider, and metadata |

---

## `node`

Represents a single runner or capability in the plan.

| Field         | Type                    | Description                                 |
|---------------|-------------------------|---------------------------------------------|
| `interface`   | `std::string`           | Interface name of the runner/capability     |
| `provider`    | `std::string`           | Provider name of the runner/capability      |
| `parameters`  | `tinyxml2::XMLElement*` | XML parameters for the runner/capability    |

**Key Methods:**
- `exists() const`: Returns true if the node is valid (all fields set).
- `parameter_to_string() const`: Serializes the parameters to a string.

**Usage:**
- Used as the source and target in `connection`.
- Populated by the parser when extracting runners from the XML plan.

---

## `connection`

Represents a directed edge in the plan's execution graph, connecting nodes and specifying control flow.

| Field         | Type        | Description                                 |
|---------------|-------------|---------------------------------------------|
| `source`      | `node`      | The source node (runner/capability)         |
| `on_start`    | `node`      | Node to trigger on start event              |
| `on_stop`     | `node`      | Node to trigger on stop event               |
| `on_success`  | `node`      | Node to trigger on success event            |
| `on_failure`  | `node`      | Node to trigger on failure event            |
| `description` | `std::string` | Human-readable description                 |
| `trigger_id`  | `int`       | Unique ID for event triggering             |

**Usage:**
- Populated by the parser when traversing the XML plan.
- Used by the server to determine execution flow and event handling.

---

## `Plan`

Represents the entire parsed plan, including all connections and metadata.

| Field           | Type                          | Description                                 |
|-----------------|-------------------------------|---------------------------------------------|
| `bond_id`       | `std::string`                 | Unique bond identifier for the plan         |
| `plan`          | `std::string`                 | Raw XML plan as a string                    |
| `connections`   | `std::map<int, connection>`   | All connections in the plan                 |
| `rejected_list` | `std::vector<std::string>`    | List of rejected/invalid elements           |

**Usage:**
- Created and populated by the parser plugin.
- Managed by the server (`plan_queue_`, `current_plan_`).
- Used to drive the execution and capability allocation.

---

## `CapabilityInfo`

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

- **CapabilityInfo** is used to track and allocate the required capabilities for each plan.

This struct-based design enables clear separation of concerns and extensibility for new plan types, events, and capabilities.
