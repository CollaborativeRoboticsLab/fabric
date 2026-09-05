# Fabric

Fabric is a ROS2 package that provides a system to coordinate and manage various capabilities as defined by the [Capabilities2 framework](https://github.com/CollaborativeRoboticsLab/capabilities2). This package extends the functionality of the Capabilities2 package to implement a Finite State Machine based on capabilities. It is designed to parse an execution plan given via an XML file and  identify connections between various capabilities in the system which would be relayed back to Capabilities2 framework to execute.

Currently the system supports 4 control functions:
- `sequential`: triggers child runners in sequence.
- `parallel_all`: waits for all child branches to report success before proceeding.
- `parallel_any`: proceeds when any child branch reports success.
- `recovery`: defines a recovery branch that can be triggered on failure.

## Features

- Implements a Partially Incomplete Finite State Machine based on the XML execution plan
- Validates the XML plan for compatibility with robot.
- Parses XML-based plans and identifies connections between capabilities.
- Informs capabilities2 framework regarding the capability connections and orchestrate a FSM.

## Read more about,

- [XML Plan strucutre](./docs/xml_plan.md)
- [Parser Plugins](./docs/parser_plugins.md)
- [Validation Plugins](./docs/validation_plugins.md)
- [Capability Client](./docs/capability_client.md)
- [Bond Client](./docs/bond_client.md)
- [Internal Data Structures](./docs/structs.md)
- [ROS2 Interface](./docs/api.md)
- [Fabric Status system](./docs/status.md)

## Setting the Fabric

Clone the repo into a workspace

```bash
mkdir -p workspace/src
cd workspace/src
```

```bash
git clone https://github.com/CollaborativeRoboticsLab/fabric.git
```

```bash
cd ..
rosdep install --from-paths src --ignore-src -r -y
```

Setup the [capabilities2](https://github.com/CollaborativeRoboticsLab/capabilities2) framework as instructed.


## Launching fabric

The [fabric_server/plans/default.xml](./fabric_server/plans/default.xml) file is the default example plan. New plans can be added there or stored anywhere else on disk.

The [fabric_server/config/fabric.yaml](./fabric_server/config/fabric.yaml) file configures the parser plugin, validation plugin, and capability client settings. The plan path itself is provided by the launch file.

By default, `fabric.launch.py` starts Fabric together with `capabilities2_server` and `prompt_bridge`. One optional launch flag and one opt-out flag extend that composition:

- `start_experience_stack:=true` includes `experience_server.launch.py`, which in turn starts the Experience and Supervisor stacks.
- `start_prompt_tools:=false` disables `prompt_bridge` when prompt-based plan generation is not needed.

Launch with the packaged default plan:

```bash
source install/setup.bash
ros2 launch fabric_server fabric.launch.py
```

Launch with a different packaged plan by filename:

```bash
source install/setup.bash
ros2 launch fabric_server fabric.launch.py filename:=default.xml
```

Launch with an explicit plan path:

```bash
source install/setup.bash
ros2 launch fabric_server fabric.launch.py plan_file_path:=/absolute/path/to/plan.xml
```

Launch with the Experience stack composed into the same process graph:

```bash
source install/setup.bash
ros2 launch fabric_server fabric.launch.py start_experience_stack:=true
```

Launch with the default prompt-based generation support enabled:

```bash
export OPENAI_API_KEY=<your_openai_api_key>
source install/setup.bash
ros2 launch fabric_server fabric.launch.py
```

Disable prompt tools explicitly when you only want direct plan execution:

```bash
source install/setup.bash
ros2 launch fabric_server fabric.launch.py start_prompt_tools:=false
```

You can combine Experience with the default prompt-tools startup when Fabric should own the full planning stack:

```bash
export OPENAI_API_KEY=<your_openai_api_key>
source install/setup.bash
ros2 launch fabric_server fabric.launch.py start_experience_stack:=true
```