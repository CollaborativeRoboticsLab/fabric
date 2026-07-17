# XML Plan

Fabric relies on XML plans to define workflows. A plan is submitted either from a file or through the `/fabric/set_plan` service as raw XML text.

The current XML parser accepts only these element types:

- `<Plan>` as the document root.
- `<Control>` with a required `type` attribute.
- `<Runner>` with required `interface` and `provider` attributes.

Supported control types are:

- `sequential`
- `parallel_any`
- `parallel_all`
- `recovery`

Any additional attributes on a `<Runner>` are forwarded into the runner's `EventParameters` and are auto-typed as `bool`, `int`, `double`, or `string`.

Below is a valid example plan shape:

```xml
<?xml version='1.0' encoding='UTF-8'?>
<Plan name='example_plan'>
  <Control type='sequential' name='main_execution'>
    <Runner interface='capabilities2_runner_nav2/WaypointRunner' provider='capabilities2_runner_nav2/WaypointRunner' x='5.0' y='5.0' />

    <Control type='parallel_any' name='gather_context'>
      <Runner interface='capabilities2_runner_nav2/RobotPoseRunner' provider='capabilities2_runner_nav2/RobotPoseRunner' from='map' to='base_link' />
      <Runner interface='capabilities2_runner_nav2/OccupancyGridRunner' provider='capabilities2_runner_nav2/OccupancyGridRunner' />
    </Control>

    <Control type='recovery' name='return_home_on_failure'>
      <Runner interface='capabilities2_runner_nav2/WaypointRunner' provider='capabilities2_runner_nav2/WaypointRunner' x='0.0' y='0.0' />
      </Control>
  </Control>
</Plan>
```

Notes:

- You do not need to include a completion runner manually. The parser appends `fabric_capabilities/FabricCompletionRunner` automatically and injects the generated `plan_id`.
- A plan submitted through `/fabric/set_plan` only gets a lightweight XML compatibility check up front. Full syntax validation happens during parsing.