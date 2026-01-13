# Fabric Status System

## Overview
The Fabric system uses a robust status tracking mechanism to represent the lifecycle and health of each plan being processed. Statuses are tracked internally using the `PlanStatus` enum and are exposed externally via the `fabric_msgs/msg/FabricStatus` message type.

## Status Lifecycle
Each plan submitted to the Fabric server transitions through a well-defined set of statuses, including:

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
- **CANCELLED**: Plan was cancelled by the user or system.

## Retrieving Plan Status

The status of any plan can be retrieved using the `/fabric/get_plan_status` ROS2 service. This service returns:

- The current status code (as `fabric_msgs/msg/FabricStatus`)
- The plan ID
- A list of rejected items (if validation or parsing failed)

### Example Service Call

```
ros2 service call /fabric/get_plan_status fabric_msgs/srv/GetPlanStatus "{plan_id: '<your_plan_id>'}"
```

### Example Response

```
status:
  code: 5  # VALIDATION_FAILED
plan_id: "123e4567-e89b-12d3-a456-426614174000"
rejected_list:
  - "Missing required capability: foo"
  - "Incompatible provider: bar"
```

## Status Conversion

Internally, the Fabric server uses the `PlanStatus` enum (see `structs.hpp`) to track status. When responding to a status request, the server converts this enum to the appropriate `FabricStatus` message code in the callback, ensuring that only the server node depends on the ROS message type.

## Extensibility

The status system is designed to be type-safe, extensible, and easy to integrate with client applications. New statuses can be added to the `PlanStatus` enum and mapped in the server callback as needed.