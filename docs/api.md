# ROS2 Interface

| Node      |  Description |
| :---      | :---            | 
| `Fabric`  | Implements the XML parsing and connection extraction as well as communicating with `capabilities_server` to configure capability events |


| Service                   | Service Message           | Description |
| :---                      | :---                      | :---        |
| `/fabric/cancel_plan`     | `CancelFabricPlan.srv`    | Cancel the current plan running in the Fabric |
| `/fabric/set_completion`  | `CompleteFabric.srv`      | Update the status of the fabric as completed (used by capabilitie) |
| `/fabric/set_plan`        | `SetFabricPlan`           | Add a new fabric plan to the queue |
