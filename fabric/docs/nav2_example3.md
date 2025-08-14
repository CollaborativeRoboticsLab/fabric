## WaypointRunner Example 1 - Single Goal

### Dependencies

This example uses nav2 stack and turtlebot3. Follow instructions from [Nav2 Dependency Installation](https://github.com/CollaborativeRoboticsLab/capabilities2/blob/develop/docs/nav2_setup.md) to setup nav stack.

In this example, (0,2) (1,2) (-2,0) are reachable, (3,0.5) (2,-3) points are not reachable.Because of this, the robot moves to the (0,0.5) as a recovery action. (1,2) (-2,0) points also have recovery actions linked, but they are not triggered as the point is accessible.

### Plan selection

Uncomment the  line related to `navigation_3.xml` in the `config/fabric.yaml` file

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

### Start the Capabilities2 Fabric

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