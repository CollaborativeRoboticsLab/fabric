## PromptCapabilityRunner Example

### Dependencies

This example uses prompt tools stack. Follow instructions from [CollaborativeRoboticsLab/prompt_tools](https://github.com/CollaborativeRoboticsLab/prompt_tools) to setup Prompt tools stack.

### Plan selection

Uncomment the  line related to `prompt_1.xml` in the `config/fabric.yaml` file

### Build the package to apply changes

In the workspace root run,

```bash
colcon build
```

### Start the Prompt Tools stack with 

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