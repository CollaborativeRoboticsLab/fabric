# Fabric

Fabric is a ROS2 package that provides a system to coordinate and manage various capabilities as defined by the [Capabilities2 framework](https://github.com/CollaborativeRoboticsLab/capabilities2). This package extends the functionality of the Capabilities2 package to implement a planning framework based on capabilities. It is designed to parse an execution plan given via an XML file and then to identify connections between various capabilities in the system.

Currently the system support 3 types of Control fuctions `sequential`, `parallel` and `recovery`, and a multitude of Event functions.

## Starting the Fabric

Start the capabilities2 server first. Then run the following on a new terminal

```bash
source install/setup.bash
ros2 launch fabric fabric.launch.py
```

## Features

- Dynamic Capability Loading: Interacts with and manages capabilities defined by the capabilities2 framework.
- Flexible Workflow Execution: Parses XML-based plans and identifies event-driven callbacks for success, failure, or in-progress states.


## Launching fabric

`fabric/plans` folder includes sample XML plans that can be used to test the system. New plans can be added to the same folder or a different location. 

Then modify the `fabric/config/fabric.yaml` file to change the active execution plan.
A number of plans are availabe with the package and included in the `fabric.yaml` file that has been commented out. Uncomment them to use. Make sure to leave only one line uncommented.

```yaml
/**:
  ros__parameters:
    plan_file_path: "install/fabric/share/fabric/plans/default.xml"
    
```
Finally start the capabilities2 server. Run the following on a new terminal

```bash
source install/setup.bash
ros2 launch fabric fabric.launch.py
```


## XML Plan Parsing

The fabric package relies on XML-based plans to define workflows. These plans specify the sequence of capabilities to execute, along with the associated parameters. The XML format includes tags for capabilities as events, and control flows enabling complex workflows to be structured in a modular way.

Below is an example XML plan for configuring a set of capabilities:

```xml
<?xml version="1.0" encoding="UTF-8"?>
<Plan name="navigate_or_return_fabric">
    <Control type="sequential" name="contro_plan">
        <Control type="sequential" name="main_execution_plan">
            <Runner interface="std_capabilities/CapabilityGetRunner" provider="std_capabilities/CapabilityGetRunner"/>
            <Runner interface="std_capabilities/PromptCapabilityRunner" provider="std_capabilities/PromptCapabilityRunner" />
            <Control type="parallel" name="gather_occupancy_data">
                <Control type="sequential" name="navigate_or_retur">
                    <Runner interface="std_capabilities/OccupancyGridRunner" provider="std_capabilities/OccupancyGridRunner"/>
                    <Runner interface="std_capabilities/PromptOccupancyRunner" provider="std_capabilities/PromptOccupancyRunner" /> 
                <Control type="sequential">
                </Control>
                <Control type="sequential" name="gather_position_data">
                    <Runner interface="std_capabilities/RobotPoseRunner" provider="std_capabilities/RobotPoseRunner" from="map" to="base_link"/>
                    <Runner interface="std_capabilities/PromptPoseRunner" provider="std_capabilities/PromptPoseRunner" />
                </Control>
            </Control>
            <Runner interface="std_capabilities/WaypointRunner" provider="std_capabilities/WaypointRunner" x="5.0" y="5.0" />
            <Control type="recovery" name="return_to_home_if_lost">
                <Runner interface="std_capabilities/WaypointRunner" provider="std_capabilities/WaypointRunner" x="0.0" y="0.0" />
            </Control>
        </Control>
        <Runner interface="std_capabilities/FabricCompletionRunner" provider="std_capabilities/FabricCompletionRunner"/>
    </Control>
</Plan>
```

## API

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

## Samples and Testing

### Navigation

| &nbsp; Example &nbsp; | Description |
| ---     | ---         |
| [Example 1](./fabric/docs/nav2_example1.md) | Implements the basic fabric triggering that moves the robot from one point to another. |
| [Example 2](./fabric/docs/nav2_example2.md) | Implements navigating through 5 points using 'sequential' control functionality. |
| [Example 3](./fabric/docs/nav2_example3.md) | Implements navigating through 5 points inluding 1 inaccessible point (1 recovery point) using `sequential` and `recovery` control functionality. |
| [Example 4](./fabric/docs/nav2_example4.md) | Implements navigating through 5 points inluding 4 inaccessible point (4 recovery point) using `sequential` and `recovery` control functionality. |

### Prompting

| &nbsp; Example &nbsp; | Description |
| ---     | ---         |
| [Example 1](./fabric/docs/prompt_example1.md) | Implements requesting for robot's capabilities and prompting them to the LLM |
| [Example 2](./fabric/docs/prompt_example2.md) | Implements listening for robot's occupancy grid and prompting them to the LLM |
| [Example 3](./fabric/docs/prompt_example3.md) | Implements listening for robot's pose and prompting them to the LLM 
| [Example 4](./fabric/docs/prompt_example4.md) | Implements prompting the LLM for a plan for a new task and setting it to Fabric |

### Navigation

| &nbsp; Example &nbsp; | Description |
| ---     | ---         |
| [Example 1](./fabric/docs/generative_example1.md) | Implements the execution plan generation to acheive one point to another. |
| [Example 2](./fabric/docs/generative_example2.md) | Implements the execution plan generation to acheive waypoint navigation. |
| [Example 3](./fabric/docs/generative_example3.md) | Implements the execution plan generation to acheive waypoint navigation with  one unreachable point having recovery point. |
| [Example 4](./fabric/docs/generative_example4.md) | Implements the execution plan generation to acheive waypoint navigation with two unreachable points having recovery points. |
| [Example 5](./fabric/docs/generative_example5.md) | Implements the execution plan generation to acheive waypoint navigation with two unreachable points having two recovery points. Two reachable points also have two recovery points. |
