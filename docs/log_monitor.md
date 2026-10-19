# Rosout Monitor in Fabric

This document explains the Fabric-native rosout monitor.

`fabric::RosoutMonitor` is a `MonitorBase` plugin in the `fabric_monitor` package. It watches `/rosout`, groups recent logs by node, assigns a normalized health label, and optionally summarizes the logs through the shared `LlmAnalysisBase` interface.

## Data Source

The monitor subscribes to `rcl_interfaces/msg/Log` on `/rosout`.

Each message contributes:

- timestamp,
- log level,
- emitting node name,
- message text.

The plugin buffers logs by node name and processes them periodically.

## Severity Mapping

The rule-based mapping is:

- no `WARN`, `ERROR`, or `FATAL` messages: `NORMAL`,
- at least one `WARN` and no `ERROR` or `FATAL`: `SUSPICIOUS`,
- any `ERROR` or `FATAL`: `FAULTY`.

This produces a coarse status without involving the language model.

## Description Generation

Descriptions are produced in one of two ways:

- fallback summary when no shared analysis plugin is configured,
- LLM-backed summary when a `LlmAnalysisBase` implementation is injected at startup.

This keeps log collection decoupled from a specific prompt-service implementation.

## Output

`getData()` returns a `std::map<std::string, fabric::ComponentStatus>` keyed by node name.

The rosout monitor does not emit plan or capability timelines directly.

## How Fabric Uses It

`fabric_server` converts warning/error `ComponentStatus` entries into generic telemetry records. When capability snapshots arrive from the capability event monitor, Fabric correlates those records with active capability time windows and attaches them to:

- `CapabilityStatus.log_records`,
- `CapabilityStatus.related_nodes`.

The effect is capability-aware log evidence without coupling the rosout plugin itself to Fabric planning internals.