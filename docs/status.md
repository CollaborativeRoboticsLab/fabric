# Fabric Status System

## Overview
Fabric tracks plan lifecycle internally with `fabric::PlanStatus` and exposes it through `fabric_msgs/msg/FabricStatus`.

## Status Lifecycle
The current server code uses the following statuses during normal processing:

- **UNKNOWN**: Initial or indeterminate state.
- **QUEUED**: Plan is queued for processing.
- **PARSING**: Plan is being parsed.
- **PARSE_FAILED**: Parsing failed due to invalid format or errors.
- **VALIDATING**: Plan is being validated for compatibility.
- **VALIDATION_FAILED**: Validation failed (see `rejected_list` for reasons).
- **BONDING**: Establishing a bond with the capabilities server.
- **BOND_FAILED**: Failed to establish bond.
- **CAPABILITY_STARTING**: Capabilities are being started.
- **CAPABILITY_START_FAILED**: Failed to start capabilities.
- **CAPABILITY_CONNECTING**: Capabilities are being connected.
- **CAPABILITY_CONNECT_FAILED**: Failed to connect capabilities.
- **RUNNING**: Plan is actively running.
- **COMPLETED**: Plan execution completed successfully.
- **CANCELLED**: Plan execution was cancelled before completion.

When `/fabric/cancel_plan` targets the active plan, the server now marks it `CANCELLED`, stops the bond, wakes the processing thread, and frees allocated capabilities before moving on.

## Retrieving Plan Status

The status of any plan can be retrieved using the `/fabric/get_plan_status` ROS2 service.

Request fields:

- `header`
- `plan_id`

Response fields:

- `header`
- `status` as `fabric_msgs/msg/FabricStatus`
- `rejected_list`

### Example Service Call

```
ros2 service call /fabric/get_plan_status fabric_msgs/srv/GetPlanStatus "{plan_id: '<your_plan_id>'}"
```

### Example Response

```
status:
  code: 5  # VALIDATION_FAILED
rejected_list:
  - "Missing required capability: foo"
  - "Incompatible provider: bar"
```

In the current implementation, `rejected_list` is populated by parser syntax checks. Validation failures from `CompatibilityValidation` throw immediately and may leave `rejected_list` empty.

## Status Conversion

When responding to a status request, the server maps `PlanStatus` to `fabric_msgs/msg/FabricStatus` in `status_msg()`.

## Extensibility

New statuses can be added by extending `PlanStatus` and updating the status mapping in the server.