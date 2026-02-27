# Examples and Testing

## Navigation

| &nbsp; Example &nbsp; | Description |
| ---     | ---         |
| [Example 1](./../fabric/docs/nav2_example1.md) | Implements the basic fabric triggering that moves the robot from one point to another. |
| [Example 2](./../fabric/docs/nav2_example2.md) | Implements navigating through 5 points using 'sequential' control functionality. |
| [Example 3](./../fabric/docs/nav2_example3.md) | Implements navigating through 5 points inluding 1 inaccessible point (1 recovery point) using `sequential` and `recovery` control functionality. |
| [Example 4](./../fabric/docs/nav2_example4.md) | Implements navigating through 5 points inluding 4 inaccessible point (4 recovery point) using `sequential` and `recovery` control functionality. |

## Prompting

| &nbsp; Example &nbsp; | Description |
| ---     | ---         |
| [Example 1](./../fabric/docs/prompt_example1.md) | Implements requesting for robot's capabilities and prompting them to the LLM |
| [Example 2](./../fabric/docs/prompt_example2.md) | Implements listening for robot's pose and prompting them to the LLM 
| [Example 3](./../fabric/docs/prompt_example3.md) | Implements prompting the LLM for a plan for a new task and setting it to Fabric |

## Generative Behaviour (ICRA2026 & IROS2026)

| &nbsp; Example &nbsp; | Description |
| ---     | ---         |
| [Example 1](./../fabric/docs/generative_example1.md) | Implements moving the robot to a given coordinate |
| [Example 2](./../fabric/docs/generative_example2.md) | Implements moving the robot through a sequence of given coordinates, which are all reachable |
| [Example 3](./../fabric/docs/generative_example3.md) | Implements moving the robot through a sequence of given coordinates, where one is unreachable with one recovery action |
| [Example 4](./../fabric/docs/generative_example4.md) | Implements moving the robot through a sequence of given coordinates, where two are unreachable with two recoveries |
| [Example 5](./../fabric/docs/generative_example5.md) | Implements moving the robot through a sequence of given coordinates, where two are unreachable, but all contains recovery actions. |

## Prompting

| &nbsp; Example &nbsp; | Description |
| ---     | ---         |
| [Example 1](./../fabric/docs/perception_example_1.md) | Implements moving the robot to a position and checking the environment via vision and explaining what is there |
| [Example 2](./../fabric/docs/perception_example_2.md) | Implements moving the robot to a position and checking if there is a person and, if there is asking for the name and speaking it after returning. | 
| [Example 3](./../fabric/docs/perception_example_3.md) | Implements moving the robot to a position and checking if there is a person and, if there is asking for the name and speaking it after returning. If there is no person, it will move to a different position. | 