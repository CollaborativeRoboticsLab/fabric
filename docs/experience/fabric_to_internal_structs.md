# Fabric To Internal Structs

This document records the translation boundary between Fabric-owned ROS data and
Experience-owned internal planning data.

## Principle

Inside Experience, names should reflect internal semantics rather than the
external system that supplied them.

- Fabric plan becomes plan.
- Fabric runner capability/interface becomes skill.
- Fabric provider becomes implementation.

Fabric also owns the plan submission boundary. The external submit request now
goes through `/fabric/plan/submit`, which accepts raw plan XML plus optional
Fabric-owned metadata. Experience no longer exposes a separate execution
registration service.

Fabric-specific names should remain in the adapter package
[experience_interface_fabric](../experience_interface_fabric) and in Fabric ROS
messages. Experience planner, core, and scoring code should consume the
internal structs defined in [metadata.hpp](../experience_base/include/experience_base/utils/metadata.hpp).

## Current Mapping

| Fabric ROS field | Experience internal field |
|---|---|
| `FabricNode.interface` | `SkillInfo.skill_name` |
| `FabricNode.provider` | `SkillInfo.implementation_name` |
| `FabricNode.instance_id` | `SkillInfo.instance_id` |
| `FabricConnection.source` | `BehaviourPlanConnection.source` |
| `FabricConnection.on_success` | `BehaviourPlanConnection.on_success` |
| `FabricConnection.on_failure` | `BehaviourPlanConnection.on_failure` |
| parsed Fabric XML | `BehaviourPlanDefinition` |
| `FabricMetaData.planning_request_id` | `SystemStatus.planning_request_id` |
| `FabricMetaData.candidate_plan_id` | `SystemStatus.candidate_plan_id` |
| `FabricMetaData.graph_hash` | `SystemStatus.graph_hash` |

## Boundary Ownership

- Fabric owns XML parsing semantics and the `/fabric/plan/parse` service.
- Fabric owns submission semantics and the `/fabric/plan/submit` service.
- `experience_interface_fabric` translates Fabric parse results into internal
	plan structs.
- Fabric runtime telemetry carries Fabric-owned plan metadata forward in `SystemStatus`.
- Experience owns plan evaluation, attribution, fallback, and plan feedback.
