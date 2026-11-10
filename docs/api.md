# ROS2 Interface

| Node | Description |
| :--- | :--- |
| `Fabric` | Main server node. Loads the configured parser and validation plugins, queues plans, validates them against capabilities2, allocates/connects capabilities, and tracks execution status. |


| Service                   | Service Message           | Description |
| :---                      | :---                      | :---        |
| `/fabric/cancel_plan`     | `CancelFabricPlan.srv`    | Cancel the current plan running in the Fabric |
| `/fabric/set_completion`  | `CompleteFabric.srv`      | Mark the current plan complete. The request contains only `plan_id`; the response is empty. |
| `/fabric/plan/submit`     | `SubmitFabricPlan.srv`    | Submit a plan plus optional Fabric-owned metadata. On success the response contains a generated `plan_id`; on rejection it returns an empty `plan_id` and an error string. |
| `/fabric/get_plan_status` | `GetPlanStatus.srv`       | Query the current queued or active status for a `plan_id`. Returns a plan-keyed `FabricStatus` code and, for parse failures, any rejected XML fragments collected during syntax validation. |
| `/fabric/plan/get_snapshot` | `GetFabricPlanSnapshot.srv` | Read the latest cached snapshot of the current dequeued plan. The header stamp is the ROS time when that state was captured, not the response time; a zero stamp means no state has been captured yet (and ROS simulated time may also be zero). `has_plan` is false when none is available; `connections_complete` distinguishes successfully parsed connections from pre-parse or partial parse data. `instances` lists distinct capability endpoints by interface, provider, and instance ID. Connections describe the plan's intended event links, not confirmed runtime connections. |

Snapshot data is published at plan state changes, so an old timestamp alone does not mean a running plan is unhealthy. The service does not wait for the execution mutex, but executor scheduling can still delay a response. Parsing failures may contain partial connections; lifecycle failure codes do not include an error message. Completed, cancelled, and failed states may be cleared or replaced quickly and are not an execution history. A queued plan is not exposed as the current plan. The response does not include running capabilities or dependencies.

Capabilities2 keeps connection ownership by bond internally. Its event snapshot is bounded history, not a complete current connection inventory, so it cannot by itself confirm that the plan's connections remain active.
