## WaypointRunner Example 1 - Single Goal

In this example (0.5,2) point is reachable. And the robot moves to that point.

### Dependencies

This example uses nav2 stack. Follow instructions from [CollaborativeRoboticsLab/nav_stack](https://github.com/CollaborativeRoboticsLab/nav_stack.git) to setup a turtlebot3 based sim environment. This is compatible with any robot that uses Nav2 Stack

### Plan selection

Uncomment the  line related to `navigation_1.xml` in the `config/fabric.yaml` file

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

for on-device/terminal logging

```bash
source install/setup.bash
ros2 launch event_logger listener.launch.py
```

or for visualization use [CollaborativeRoboticsLab/event_logger_ui](https://github.com/CollaborativeRoboticsLab/event_logger_ui)