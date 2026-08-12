# Generation Plugins in Fabric

> Note: In the earlier version of Fabric (up to v0.2.0), generative plans were handled by collaboration between [fabric_capabilities](https://github.com/CollaborativeRoboticsLab/fabric_capabilities) and [prompt_capabilities](https://github.com/CollaborativeRoboticsLab/prompt_capabilities) at the capability level. Since we are introducing [experience](https://github.com/CollaborativeRoboticsLab/experience) stack based plan generation in addition to existing prompt_tools based plan generation, the generation pipeline was integrated into the fabric server in v0.3.0 onwards with two approaches selectable via plugins.

## Motivation and Design

Generation plugins convert a task description into a `fabric::Plan`. The Fabric server loads one generation plugin at startup through the `generation_plugin` parameter and exposes plan generation through the `/fabric/plan/generate` ROS2 action.

This keeps task-to-plan generation separate from Fabric execution:

- **Runtime selection:** Different generation backends can be selected through configuration without changing the server.

- **Backend isolation:** Prompt-based generation, experience-based generation, or future generators can keep their own service/action clients and backend-specific logic.

- **Shared server behavior:** The server handles the public action API, optional queuing, parser compatibility checks, and action feedback/result messages.

## GenerationBase Interface

All generation plugins inherit from `fabric::GenerationBase` and implement:

- `initialize(const rclcpp::Node::SharedPtr&)`: Prepares the plugin with the ROS2 node context and initializes any backend clients.

- `generate(const std::string& task, const std::string& uuid, bool flush)`: Generates and returns a `fabric::Plan` from a task description.

The returned `fabric::Plan` should contain the generated XML plan text in `plan.plan`. The Fabric server assigns the `plan_id` and handles queuing.

## Server Action Flow

The Fabric server creates a `fabric_msgs/action/GeneratePlan` action server at:

```text
/fabric/plan/generate
```

The action goal contains:

| Field | Purpose |
|-------|---------|
| `task` | Natural-language task description to generate a plan for |
| `auto_queue` | If true, the generated plan is checked by the parser and added to `plan_queue_` |
| `uuid` | Prompt/session identifier forwarded to the generation plugin |
| `flush` | Cache-control flag forwarded to the generation plugin |

The server behavior is:

1. Reject empty task goals.
2. Publish `GENERATION_REQUESTED` feedback.
3. Call `generation_plugin_->generate(goal->task, goal->uuid, goal->flush)`.
4. Assign a new `plan_id` and mark the generated plan as `QUEUED`.
5. If `auto_queue` is true, check parser compatibility and push the plan into `plan_queue_`.
6. Publish `GENERATION_COMPLETE` feedback and return the XML plan plus `plan_id` in the action result.

## Configuration

The active generator is selected with the `generation_plugin` parameter in `fabric_server/config/fabric.yaml`:

```yaml
generation_plugin: fabric::PromptToolsGenerator
# generation_plugin: fabric::ExperienceGenerator
```

The class name must match the pluginlib class exported by the generator package's `plugins.xml`.

## PromptToolsGenerator

`fabric::PromptToolsGenerator` is exported by `fabric_generator_prompttools`.

It generates a plan by combining the requested task with runnable capability descriptions and sending that prompt to the prompt service.

### Dependencies and Runtime Services

The plugin waits for these services during initialization:

| Service | Type | Purpose |
|---------|------|---------|
| `prompt/prompt` | `prompt_msgs/srv/Prompt` | Sends the constructed prompt to the configured model backend |
| `capabilities2/get_runnable_specs` | `capabilities2_msgs/srv/GetRunnableSpecs` | Retrieves capability descriptions used as prompt context |

### Generation Behavior

1. Request runnable specs from `capabilities2/get_runnable_specs`.
2. Build a prompt containing capability names, descriptions, and the user task.
3. Send the prompt to `prompt/prompt` with `uuid`, cache settings, and model options.
4. Return a `fabric::Plan` whose `plan.plan` is the prompt response text.

The current implementation sets `model_family` to `openai`, uses cache by default, forwards `flush`, and requests a non-streaming model response.

## ExperienceGenerator

`fabric::ExperienceGenerator` is exported by `fabric_generator_experience`.

It delegates plan generation to the Experience subsystem through a ROS2 action client.

### Dependencies and Runtime Action

The plugin waits for this action server during initialization:

| Action | Type | Purpose |
|--------|------|---------|
| `experience/plan/generate` | `experience_msgs/action/GeneratePlan` | Generates a plan using the Experience stack |

### Generation Behavior

1. Send the Fabric task as `task_description` to the Experience action server.
2. Log feedback status updates from the Experience action.
3. Wait for the action result.
4. Return a `fabric::Plan` whose `plan.plan` is the Experience result plan.

The current Experience action interface does not consume the Fabric `uuid` or `flush` inputs, so this plugin ignores those arguments.

## Adding a New Generation Plugin

To add another generator:

1. Create a package that depends on `fabric_base`, `pluginlib`, `rclcpp`, and any backend-specific interfaces.
2. Implement a class inheriting from `fabric::GenerationBase`.
3. Implement `initialize(...)` and `generate(task, uuid, flush)`.
4. Export the class with `PLUGINLIB_EXPORT_CLASS(MyGenerator, fabric::GenerationBase)`.
5. Add a `plugins.xml` entry with `base_class_type="fabric::GenerationBase"`.
6. Install and export the plugin description with `pluginlib_export_plugin_description_file(fabric_base plugins.xml)`.
7. Set `generation_plugin` in `fabric.yaml` to the exported class name.

## Notes

- Generation plugins are responsible only for creating plan text. Parsing, compatibility checks, execution, and capability wiring remain owned by the Fabric server and parser/validation plugins.

- `auto_queue` is handled by the server, not by generator plugins.

- Generator output should be XML compatible with the active parser plugin.

- `fabric_base` does not depend on `capabilities2_events`; generation plugins only need capability-specific dependencies when they call capability services directly.
