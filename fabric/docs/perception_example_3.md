## Generative Example 1 - Single point navigation

This is presented as part of the tasks used for the IROS2026 paper. 

In this example, we define two points as point A and B as world information. The LLM would need to generate a plan or more that moves the robot To the point A and check for a person using vision or audio, then ask for the name. Then it should come back to the origin and repeat the name. If the person is not there, the robot is supposed to Go to point B instead.

### Dependencies

This example uses nav2 stack, perception stack, prompt tools.

Follow instructions from [CollaborativeRoboticsLab/turtlebot3-docker](https://github.com/CollaborativeRoboticsLab/turtlebot3-docker) or [CollaborativeRoboticsLab/turtlebot4-docker](https://github.com/CollaborativeRoboticsLab/turtlebot4-docker) to setup a Turtlebot based sim environment. This is compatible with any robot that uses Nav2 Stack.

### Plan selection

Uncomment the  line related to `perception_1.xml` in the `config/fabric.yaml` file

### Build the package to apply changes

In the workspace root run,

```bash
colcon build
```

### Start the turtlebot simulation
 
Start the nav2 stack along with the simulation robot or physical robot.

### Start the Prompt Tools stack

```bash
source install/setup.bash
ros2 launch prompt_bridge prompt_bridge.launch.py
```

### Start the Perception stack

```bash
source install/setup.bash
ros2 launch perception server.launch.py
```

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