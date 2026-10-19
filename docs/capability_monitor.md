# Capability Event Monitor in Fabric

This document explains the Fabric-native capability monitor.

The current monitor is implemented as `fabric::CapabilityEventMonitor` in the `fabric_monitor` package. It is not the old Supervisor plugin that summarized capability health into `ComponentStatus` with an LLM. Instead, it reconstructs structured runtime evidence directly as Fabric-owned `SystemStatus` snapshots.

## Data Source

The monitor polls the `capabilities2` snapshot service:

- service: `/capabilities/get_event_snapshot`
- message family: `capabilities2_msgs`

Rather than subscribing to a live topic and emitting textual summaries, the monitor requests bounded event snapshots after the last seen sequence number and incrementally rebuilds capability state.

## Output Shape

The monitor returns `std::vector<fabric::status::SystemStatus>` from `getData()`.

Each returned `SystemStatus` is grouped by `plan_id` and contains one or more `CapabilityStatus` entries. Each capability entry carries:

- capability name,
- instance ID,
- lifecycle data,
- run evidence counters and latest terminal outcome.

This makes the capability monitor a source of structured execution evidence, not just a high-level health summary.

## Tracking Key

Capability state is keyed by:

$$
\text{plan\_id} / \text{instance\_id} / \text{capability\_name}
$$

This preserves the execution identity needed later for correlation, feedback persistence, and experience updates.

## Lifecycle Reconstruction

The monitor interprets `capabilities2` event codes to update lifecycle state.

Examples:

- `STARTED`: marks the capability active and records `last_start_time`.
- `STOPPED`: marks it inactive, records `last_stop_time`, and may infer a cancelled terminal outcome.
- `FAILED`, `SUCCEEDED`, `TERMINATED`: record explicit terminal outcomes.
- `DISCONNECTED`, `ERROR`, `UNDEFINED`: may infer terminal outcomes when explicit terminal messages are absent.
- `TRIGGERED`: records per-run trigger evidence.

At the end of each polling cycle, the monitor recomputes run-evidence counters such as:

- `runs_started`,
- `runs_completed`,
- `runs_succeeded`,
- `runs_failed`,
- `runs_aborted`,
- `runs_timed_out`,
- `last_outcome_time`,
- `last_outcome_label`.

## What the Monitor Does Not Do

The Fabric capability monitor deliberately does not:

- call an LLM,
- publish `ComponentStatus` summaries,
- attribute logs, diagnostics, or battery drops by itself,
- decide shutdown policy.

Those concerns live elsewhere:

- log and diagnostics monitoring stay in separate monitor plugins,
- battery and telemetry correlation happen in `fabric_server`,
- shutdown safety is computed from the enriched `SystemStatus` snapshots.

## Role in the Fabric Runtime

`CapabilityEventMonitor` is the authoritative capability-evidence source for the merged Fabric runtime.

Its output is later enriched by `fabric_server` with:

- execution metadata from `/fabric/status`,
- correlated rosout and diagnostics records,
- battery intervals.

This split keeps capability facts close to `capabilities2` while leaving cross-source joins in Fabric.