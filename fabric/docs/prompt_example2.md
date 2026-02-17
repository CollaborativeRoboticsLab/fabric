## PromptPoseRunner Example

### Dependencies

This example uses prompt tools stack, nav2 stack and turtlebot. Follow instructions from [CollaborativeRoboticsLab/turtlebot3-docker](https://github.com/CollaborativeRoboticsLab/turtlebot3-docker) or [CollaborativeRoboticsLab/turtlebot4-docker](https://github.com/CollaborativeRoboticsLab/turtlebot4-docker) to setup a Turtlebot based sim environment and  [CollaborativeRoboticsLab/prompt_tools](https://github.com/CollaborativeRoboticsLab/prompt_tools) to setup Prompt tools stack.

### Plan selection

Uncomment the  line related to `prompt_2.xml` in the `config/fabric.yaml` file

### Build the package to apply changes

In the workspace root run,

```bash
colcon build
```

### Start the turtlebot simulation
 
Start the nav2 stack along with the simulation robot or physical robot.

### Start the Prompt Tools stack

```bash
export OPENAI_API_KEY=
source install/setup.bash
ros2 launch prompt_bridge prompt_bridge.launch.py
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