# Fabric

Fabric is a ROS2 package that provides a system to coordinate and manage various capabilities as defined by the [Capabilities2 framework](https://github.com/CollaborativeRoboticsLab/capabilities2). This package extends the functionality of the Capabilities2 package to implement a Finite State Machine based on capabilities. It is designed to parse an execution plan given via an XML file and  identify connections between various capabilities in the system which would be relayed back to Capabilities2 framework to execute.

Currently the system support 4 types of Control fuctions 
- `sequential` : provides sequential triggering of capabilities. Exits if any capabilities fail
- `parallel-all` : provides parallel triggering of capabilities and waits until at least one completes to proceed
- `parallel-any` : provides parallel triggering of capabilities and waits until all completes to proceed
- `recovery` : provides sequential triggering of recovery capabilities if the predecessor outside the recovery block fails. Exits if any recovery capabilities succeed

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
- [Examples and Testing](./docs/examples.md)

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

`fabric/plans` folder includes sample XML plans that can be used to test the system. New plans can be added to the same folder or a different location. 

Then modify the `fabric/config/fabric.yaml` file to change the active execution plan. A number of plans are availabe with the package and included in the `fabric.yaml` file that has been commented out. Uncomment them to use. Make sure to leave only one line uncommented.

```yaml
/**:
  ros__parameters:
    plan_file_path: "install/fabric/share/fabric/plans/default.xml"
    
```

Finally start the fabric server. Run the following on a new terminal

```bash
source install/setup.bash
ros2 launch fabric fabric.launch.py
```

## Composed launch

Composed launch is a more efficient approach to spin up a system under a single process. Positives are IPC via shared memory, providing lower latency and optimized resoirce usage. Draw back of this approach is that, it becomes harder for debugging and fault tolerence.

Use independent launch files with standard node based implementations for debugging. For deployment, utilize composed launch where ever possible.

We provide several composed launch files for launching the system.

### Non generative launch

This launch file starts Fabric Server and [Capabilities2 Server](https://github.com/CollaborativeRoboticsLab/capabilities2). Suitable for non-generative use with predefined plans.

```bash
source install/setup.bash
ros2 launch fabric nongenerative.launch.py
```

### Generative launch

This launch file starts Fabric Server, [Capabilities2 Server](https://github.com/CollaborativeRoboticsLab/capabilities2) and [Prompt Tools](https://github.com/CollaborativeRoboticsLab/prompt_tools). Suitable for generative use without perception.

```bash
source install/setup.bash
ros2 launch fabric generative.launch.py
```

### Generative launch with perception

This launch file starts Fabric Server, [Capabilities2 Server](https://github.com/CollaborativeRoboticsLab/capabilities2), [Prompt Tools](https://github.com/CollaborativeRoboticsLab/prompt_tools), and [Perception Server](https://github.com/CollaborativeRoboticsLab/perception) . Suitable for generative use with perception.

```bash
source install/setup.bash
ros2 launch fabric generative_perception.launch.py
```