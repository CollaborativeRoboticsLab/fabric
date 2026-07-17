# ROS2 Interface

| Node | Description |
| :--- | :--- |
| `Fabric` | Main server node. Loads the configured parser and validation plugins, queues plans, validates them against capabilities2, allocates/connects capabilities, and tracks execution status. |


| Service                   | Service Message           | Description |
| :---                      | :---                      | :---        |
| `/fabric/cancel_plan`     | `CancelFabricPlan.srv`    | Cancel the current plan running in the Fabric |
| `/fabric/set_completion`  | `CompleteFabric.srv`      | Mark the current plan complete. The request contains only `plan_id`; the response is empty. |
| `/fabric/set_plan`        | `SetFabricPlan.srv`       | Submit a plan as raw XML text. On success the response contains a generated `plan_id`; on rejection it returns an empty `plan_id` and an error string. |
| `/fabric/get_plan_status` | `GetPlanStatus.srv`       | Query the current queued or active status for a `plan_id`. Returns a `FabricStatus` code and, for parse failures, any rejected XML fragments collected during syntax validation. |
