## WaypointRunner Example 2 - Goal Sequence

In this example, (0.5,2) (1,2) (2,0.5) (-2,0) points are reachable. And the robot moves through those points.

### Dependencies

This example uses nav2 stack. Follow instructions from [CollaborativeRoboticsLab/turtlebot3-docker](https://github.com/CollaborativeRoboticsLab/turtlebot3-docker) or [CollaborativeRoboticsLab/turtlebot4-docker](https://github.com/CollaborativeRoboticsLab/turtlebot4-docker) to setup a Turtlebot based sim environment. This is compatible with any robot that uses Nav2 Stack.

### Plan selection

Uncomment the  line related to `navigation_2.xml` in the `config/fabric.yaml` file

### Build the package to apply changes

In the workspace root run,

```bash
colcon build
```

### Start the turtlebot simulation
 
Start the nav2 stack along with the simulation robot or physical robot.

### Start the Capabilities2 Server

```bash
source install/setup.bash
ros2 launch capabilities2_server capabilities2_server.launch.py
```

### Start the Fabric

```bash
source install/setup.bash
ros2 launch fabric fabric.launch.py
```