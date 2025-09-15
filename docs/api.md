# API

| Node      |  Description |
| :---      | :---            | 
| `Fabric`  | Implements the XML parsing and connection extraction as well as communicating with `capabilities_server` to configure capability events |
| `Client`  | Reads an exection plan from a path or a ROS message and sends it to the `fabric` node. Provides additional services that expose the Fabric to outside |

| Action    | Action Message | Description |
| :---      | :---            | :---        |
| `/fabric` | `Plan.action` | Receive and XML plan via the message for execution |

| Service                   | Service Message           | Description |
| :---                      | :---                      | :---        |
| `/fabric/get_status`      | `GetFabricStatus.srv`     | Retrieve the status of the fabric |
| `/fabric/cancel_plan`     | `CancelFabricPlan.srv`    | Cancel the current plan running in the Fabric |
| `/fabric/set_completion`  | `CompleteFabric.srv`      | Update the status of the fabric as completed (used by capabilitie) |
| `/fabric/set_plan`        | `SetFabricPlan`           | Add a new fabric plan to the queue |
