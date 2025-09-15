# XML Plan Parsing

The fabric package relies on XML-based plans to define workflows. These plans specify the sequence of capabilities to execute, along with the associated parameters. The XML format includes tags for capabilities as events, and control flows enabling complex workflows to be structured in a modular way.

Below is an example XML plan for configuring a set of capabilities:

```xml
<?xml version='1.0' encoding='UTF-8'?>
<Plan name='navigate_or_return_fabric'>
  <Control type='sequential' name='contro_plan'>
    <Control type='sequential' name='main_execution_plan'>
      <Runner interface='capabilities2_runner_capabilities/CapabilityGetRunner' provider='capabilities2_runner_capabilities/CapabilityGetRunner'/>
      <Runner interface='capabilities2_runner_prompt/PromptCapabilityRunner' provider='capabilities2_runner_prompt/PromptCapabilityRunner' />
      <Control type='parallel' name='gather_occupancy_data'>
        <Control type='sequential' name='navigate_or_retur'>
          <Runner interface='capabilities2_runner_nav2/OccupancyGridRunner' provider='capabilities2_runner_nav2/OccupancyGridRunner'/>
          <Runner interface='capabilities2_runner_prompt/PromptOccupancyRunner' provider='capabilities2_runner_prompt/PromptOccupancyRunner' /> 
        <Control type='sequential'>
        </Control>
        <Control type='sequential' name='gather_position_data'>
          <Runner interface='capabilities2_runner_nav2/RobotPoseRunner' provider='capabilities2_runner_nav2/RobotPoseRunner' from='map' to='base_link'/>
          <Runner interface='capabilities2_runner_prompt/PromptPoseRunner' provider='capabilities2_runner_prompt/PromptPoseRunner' />
        </Control>
      </Control>
      <Runner interface='capabilities2_runner_nav2/WaypointRunner' provider='capabilities2_runner_nav2/WaypointRunner' x='5.0' y='5.0' />
      <Control type='recovery' name='return_to_home_if_lost'>
          <Runner interface='capabilities2_runner_nav2/WaypointRunner' provider='capabilities2_runner_nav2/WaypointRunner' x='0.0' y='0.0' />
      </Control>
    </Control>
    <Runner interface='capabilities2_runner_fabric/FabricCompletionRunner' provider='capabilities2_runner_fabric/FabricCompletionRunner'/>
  </Control>
</Plan>
```