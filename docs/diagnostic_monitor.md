# Diagnostics Monitor in Fabric

This document explains the Fabric-native diagnostics monitor.

`fabric::RosDiagnosticsMonitor` is a `MonitorBase` plugin in the `fabric_monitor` package. It observes `/diagnostics`, converts diagnostic levels into Fabric's normalized monitoring statuses, and optionally uses the shared `LlmAnalysisBase` interface to summarize recent diagnostics.

## Data Source

The monitor subscribes to `diagnostic_msgs/msg/DiagnosticArray` on `/diagnostics`.

Each diagnostic entry contains at least:

- component name,
- diagnostic level (`OK`, `WARN`, `ERROR`, `STALE`),
- message text.

The plugin buffers diagnostics by component name and periodically converts them into Fabric `ComponentStatus` records.

## Severity Mapping

The mapping is intentionally simple:

- no `WARN`, `ERROR`, or `STALE` entries: `NORMAL`,
- any `WARN` without `ERROR` or `STALE`: `SUSPICIOUS`,
- any `ERROR` or `STALE`: `FAULTY`.

This is the same normalized status scale used by Fabric's other generic telemetry monitors.

## Description Generation

The monitor can generate descriptions in two modes:

- fallback summary: a deterministic text summary based on buffered diagnostics,
- analysis-backed summary: an optional `LlmAnalysisBase` implementation summarizes recent messages.

That means diagnostics monitoring remains useful even when PromptTools or another analysis backend is unavailable.

## Output

`getData()` returns a `std::map<std::string, fabric::ComponentStatus>` keyed by component name.

The diagnostics monitor does not directly know about plans or capabilities. It only reports component-centric evidence.

## How Fabric Uses It

`fabric_server` ingests these `ComponentStatus` entries as generic telemetry records. Later, when a capability-bearing `SystemStatus` arrives, Fabric correlates those records against the capability's lifecycle window and attaches them as:

- `log_records`,
- `related_nodes`.

So the diagnostics monitor stays reusable and collection-focused, while plan-aware correlation remains in `fabric_server`.