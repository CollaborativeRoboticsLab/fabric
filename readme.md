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

## Composed launch

Composed launch is a more efficient approach to spin up a system under a single process. Positives are IPC via shared memory, providing lower latency and optimized resoirce usage. Draw back of this approach is that, it becomes harder for debugging and fault tolerence.

Use independent launch files with standard node based implementations for debugging. For deployment, utilize composed launch where ever possible.

We provide several composed launch files for launching the system.

### Non generative launch

This launch file starts Fabric Server and [Capabilities2 Server](https://github.com/CollaborativeRoboticsLab/capabilities2). Suitable for non-generative use with predefined plans.

```bash
source install/setup.bash
ros2 launch fabric_server nongenerative.launch.py
```

### Generative launch

This launch file starts Fabric Server, [Capabilities2 Server](https://github.com/CollaborativeRoboticsLab/capabilities2), and `prompt_bridge`. Suitable for generative use without perception.

```bash
export OPENAI_API_KEY=
source install/setup.bash
ros2 launch fabric_server generative.launch.py
```

### Generative launch with perception

This launch file starts Fabric Server, [Capabilities2 Server](https://github.com/CollaborativeRoboticsLab/capabilities2), `prompt_bridge`, and [Perception Server](https://github.com/CollaborativeRoboticsLab/perception). Suitable for generative use with perception.

```bash
export OPENAI_API_KEY=
source install/setup.bash
ros2 launch fabric_server generative_perception.launch.py
```