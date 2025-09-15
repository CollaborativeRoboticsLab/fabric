## Generative Example 1 - Single point navigation

This is presented as part of the tasks used for the ICRA2026 paper. 

In this example, the robot is suppoed to go through (2.0, -0.5), (1.0, 3.0), (-3.0, -1.0), (0.0, -2.0) points. Out of these (1.0, 3.0) and (-3.0, -1.0) are unreachable. So instead of (1.0, 3.0) robot will move to (0.0, 2.0). Instead of (-3.0, -1.0) the robot will move to (-2.0, 0.0). The LLM would need to generate a plan that moves the robot through the points in order considering the unreachable point.

### Dependencies

This example uses nav2 stack for control and a simulated turtlebot3. Follow instructions from [Nav2 Dependency Installation](https://github.com/CollaborativeRoboticsLab/capabilities2/blob/develop/docs/nav2_setup.md) to setup nav stack.

### Plan selection

Uncomment the  line related to `generative_4.xml` in the `config/fabric.yaml` file

### Build the package to apply changes

In the workspace root run,

```bash
colcon build
```

### Start the turtlebot simulation

```bash
source install/setup.bash
export TURTLEBOT3_MODEL=waffle
ros2 launch nav_stack turtlebot3_world.launch.py
```

### Start the Navigation2 stack

```bash
source install/setup.bash
ros2 launch nav_stack system.launch.py
```

### Start the Prompt Tools stack

```bash
source install/setup.bash
ros2 launch prompt_bridge prompt_bridge.launch.py
```

### Start the Capabilities2 Server

```bash
source install/setup.bash
ros2 launch capabilities2_server server.launch.py
```

### Start the Fabric

```bash
source install/setup.bash
ros2 launch fabric fabric.launch.py
```

### Start the logging (Optional)

for on-device/terminal logging. Start this before Capabilities2 Server, Fabric

```bash
source install/setup.bash
ros2 launch event_logger listener.launch.py
```

or for visualization use [CollaborativeRoboticsLab/event_logger_ui](https://github.com/CollaborativeRoboticsLab/event_logger_ui)