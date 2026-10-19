# Fabric Runtime Parameters

This document summarizes the main runtime parameters for the merged Fabric stack.

The active runtime is configured through `fabric_server/config/fabric.yaml`. Old Supervisor-only parameters such as dynamics and odometry monitor configuration are not part of the merged Fabric runtime anymore.

## Core Fabric Server Parameters

These parameters are read directly by `fabric_server`.

| Parameter | Type | Default | Description |
|---|---|---|---|
| `plan_file_path` | string | packaged default XML plan | Plan file queued at startup. |
| `telemetry.fabric_status_topic` | string | `"/fabric/status"` | Public execution-status topic published by Fabric. |
| `telemetry.monitor_poll_frequency_hz` | double | `2.0` | Poll frequency used by the monitor-ingestion thread. |
| `telemetry.retention_seconds` | int | `300` | How long generic telemetry records are retained for later capability correlation. |
| `telemetry.enable_battery_monitoring` | bool | `true` | Enable Fabric-owned battery monitoring. |
| `telemetry.battery_topic` | string | `"/battery_state"` | Battery topic used for capability battery intervals and low-battery telemetry. |

## Monitor Plugin Selection

The merged Fabric runtime uses pluginlib-loaded monitor plugins configured under:

| Parameter | Type | Description |
|---|---|---|
| `telemetry.monitor_plugins` | string array | Monitor plugin class names loaded at startup. |

The current default monitor set is:

- `fabric::ExecutionStatusMonitor`
- `fabric::CapabilityEventMonitor`
- `fabric::RosoutMonitor`
- `fabric::RosDiagnosticsMonitor`

The old Dynamics and Odom monitor paths were intentionally not carried into the merged Fabric runtime.

## Execution Status Monitor

Namespace: `ExecutionStatusMonitor`

| Parameter | Type | Default | Description |
|---|---|---|---|
| `topic` | string | `"/fabric/status"` | Fabric execution-status topic to subscribe to. |
| `frequency` | double | `1.0` | Poll/extraction frequency for returning buffered execution snapshots. |

## Capability Event Monitor

Namespace: `CapabilityEventMonitor`

| Parameter | Type | Default | Description |
|---|---|---|---|
| `topic` | string | `""` | Present for consistency with the generic monitor base; not used by the current implementation. |
| `frequency` | double | `1.0` | Polling frequency for capability snapshot requests. |
| `service_name` | string | `"/capabilities/get_event_snapshot"` | Snapshot service used to fetch capability events. |
| `service_wait_timeout_sec` | int | `-1` | Service wait timeout; negative means wait indefinitely. |
| `max_events` | int | `100` | Maximum number of events requested per snapshot call. |

## Rosout Monitor

Namespace: `RosoutMonitor`

| Parameter | Type | Default | Description |
|---|---|---|---|
| `topic` | string | `"/rosout"` | ROS 2 log topic to subscribe to. |
| `frequency` | double | `1.0` | Processing frequency for grouped rosout summaries. |

## Diagnostics Monitor

Namespace: `RosDiagnosticsMonitor`

| Parameter | Type | Default | Description |
|---|---|---|---|
| `topic` | string | `"/diagnostics"` | Diagnostics topic to subscribe to. |
| `frequency` | double | `1.0` | Processing frequency for grouped diagnostic summaries. |

## Shared Analysis Plugin Parameters

Namespace: `PromptToolsLlmAnalysis`

| Parameter | Type | Default | Description |
|---|---|---|---|
| `service_wait_timeout_sec` | int | `10` | Timeout when waiting for PromptTools services. |
| `request_timeout_sec` | int | `90` | Timeout for prompt, embedding, or tokenize requests. |
| `services.prompt` | string | `"/prompt/prompt"` | Prompt completion service. |
| `services.embedding` | string | `"/prompt/embedding"` | Embedding service. |
| `services.tokenize` | string | `"/prompt/tokenizer"` | Tokenization service. |

## Experience Runtime Parameters

The merged runtime also keeps Experience configuration under `experience.*`.

Common top-level switches include:

| Parameter | Type | Default | Description |
|---|---|---|---|
| `experience.enabled` | bool | `false` | Enable the Experience runtime inside Fabric. |
| `experience.rebuild` | bool | `false` | Rebuild the knowledge core from persisted data. |
| `experience.consume_feedback` | bool | `false` | Enable plan feedback ingestion. |
| `experience.user_feedback_root_dir` | string | `""` | Root directory for external user feedback artifacts. |
| `experience.dispatch_terminal_success_only` | bool | `false` | Restrict some feedback dispatch flows to terminal success outcomes. |

Additional planner, scorer, validation, and estimator settings are defined in `fabric.yaml` and are specific to the current Experience configuration.

## Retired Supervisor Parameters

These older configuration concepts were intentionally not carried forward into the active Fabric runtime:

- `use_dynamics_monitor`
- `use_odom_monitor`
- `DynamicsMonitor.*`
- `OdomMonitor.*`
- `use_capability_monitor`
- `update_frequency` on `SupervisorServer`

Capability monitoring, log monitoring, diagnostics monitoring, execution-status monitoring, and battery monitoring are now owned directly by Fabric.