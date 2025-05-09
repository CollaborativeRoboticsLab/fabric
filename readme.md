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
    <Control name="sequential" name="contro_plan">
        <Control name="sequential" name="main_execution_plan">
            <Event interface="std_capabilities/CapabilityGetRunner" provider="std_capabilities/CapabilityGetRunner"/>
            <Event interface="std_capabilities/PromptCapabilityRunner" provider="std_capabilities/PromptCapabilityRunner" />
            <Control name="parallel" name="gather_occupancy_data">
                <Control name="sequential" name="navigate_or_retur">
                    <Event interface="std_capabilities/OccupancyGridRunner" provider="std_capabilities/OccupancyGridRunner"/>
                    <Event interface="std_capabilities/PromptOccupancyRunner" provider="std_capabilities/PromptOccupancyRunner" /> 
                <Control name="sequential">
                </Control>
                <Control name="sequential" name="gather_position_data">
                    <Event interface="std_capabilities/RobotPoseRunner" provider="std_capabilities/RobotPoseRunner" from="map" to="base_link"/>
                    <Event interface="std_capabilities/PromptPoseRunner" provider="std_capabilities/PromptPoseRunner" />
                </Control>
            </Control>
            <Event interface="std_capabilities/WaypointRunner" provider="std_capabilities/WaypointRunner" x="5.0" y="5.0" />
            <Control name="recovery" name="return_to_home_if_lost">
                <Event interface="std_capabilities/WaypointRunner" provider="std_capabilities/WaypointRunner" x="0.0" y="0.0" />
            </Control>
        </Control>
        <Event interface="std_capabilities/FabricCompletionRunner" provider="std_capabilities/FabricCompletionRunner"/>
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

1. [WaypointRunner Example 1](./fabric/docs/waypoint_runner_ex1.md)
Implements at the very basic fabric triggering that moves the robot from one point to another.

2. [WaypointRunner Example 2](./fabric/docs/waypoint_runner_ex2.md)
Implements navigating through 2 points using 'sequential' control functionality.


### Prompting

1. [PromptCapabilityRunner Example](./fabric/docs/prompt_capability_runner_ex1.md)
Implements requesting for robot's capabilities and prompting them to the LLM

2. [PromptOccupancyRunner Example](./fabric/docs/prompt_occupancy_runner_ex1.md)
Implements listening for robot's occupancy grid and prompting them to the LLM

2. [PromptPoseRunner Example](./fabric/docs/prompt_pose_runner_ex1.md)
Implements listening for robot's pose and prompting them to the LLM

2. [PromptPlanRunner Example](./fabric/docs/prompt_plan_runner_ex1.md)
Implements prompting the LLM for a plan for a new task and setting it to Fabric
