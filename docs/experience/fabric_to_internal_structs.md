# Fabric To Internal Structs

This document records how Fabric's integrated runtime obtains native planning
data without the former Experience service adapters.

## Principle

Planning, score-only evaluation, and dispatch use the configured Fabric parser
directly. There is no internal call to `/fabric/plan/parse` and no
`BehaviourInterfaceBase` adapter. The public parse and submit services remain
available to external clients.

`ParserBase::parsePlanDefinition()` returns native `fabric::capability::PlanDefinition`
data, including complete runner parameters and event connections. It reports
parser exceptions through `success` and `error`. Shared parser entry points
serialize access to the parser's mutable document state.

## Current Mapping

| Source | Fabric internal field |
|---|---|
| Runner interface | `plan::node.interface` |
| Runner provider | `plan::node.provider` |
| Runner instance | `plan::node.instance_id` |
| Runner parameters | `plan::node.parameters` |
| Parsed event connection | `plan::connection` |
| Parsed Fabric XML | `capability::PlanDefinition` |
| capabilities2 runnable specification | `capability::Descriptor` |
| `FabricMetaData.planning_request_id` | `SystemStatus.planning_request_id` |
| `FabricMetaData.candidate_plan_id` | `SystemStatus.candidate_plan_id` |
| `FabricMetaData.graph_hash` | `SystemStatus.graph_hash` |

## Boundary Ownership

- Fabric owns XML parsing semantics and the `/fabric/plan/parse` service.
- Fabric owns submission semantics and the `/fabric/plan/submit` service.
- The active parser provides native plan definitions directly to the planner.
- `CapabilityClient::getRunnableSpecs()` converts capabilities2 runnable YAML into
	native descriptors, retaining provider metadata, parameters, relations, and commands.
- Capability discovery and core initialization run in the readiness worker while
	the ROS executor handles service responses. A fresh core does not require a
	metadata plugin or a pre-existing database.
- Fabric runtime telemetry carries Fabric-owned plan metadata forward in `SystemStatus`.
- Fabric's integrated Experience plugins own evaluation, attribution, fallback,
  and plan feedback.
