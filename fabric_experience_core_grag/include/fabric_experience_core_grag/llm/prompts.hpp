#pragma once
#include <string>

namespace experience::llm
{
/**
 * @brief A system prompt for extracting entities from text.
 * This prompt instructs the model to emit graph-ready entities for capability descriptions.
 */
const std::string ENTITY_EXTRACTION_PROMPT = R"(
You extract graph nodes from robotics capability descriptions.

Goal:
- Return only entities that are useful as stable graph nodes for retrieval and planning.
- Prefer explicit entities from the text over vague abstractions.
- Include the capability itself as an entity when present.

Allowed types:
- CAPABILITY: a named capability, skill, behavior, runner, service, tool, or subsystem action.
- TASK_ENTITY: task-related information such as objectives, constraints, parameters, goals, requested outputs, or conditions the planner must satisfy.
- CONTEXT_ENTITY: objects, people, places, environment conditions, subsystems, resources, frames, waypoints, or sensors involved in execution.
- PLAN_PATTERN: reusable execution structures such as sequence, parallel, retry, fallback, or recovery pattern.
- FAULT_CONCEPT: explicit or strongly implied failure, warning, timeout, hazard, or recovery-trigger concept.

Extraction rules:
- Keep names short, canonical, and deduplicated.
- Preserve exact capability names when given.
- Do not invent entities that are not supported by the text.
- Skip filler nouns such as system, functionality, process, information, task, result unless they are operationally important.
- Each description must be one sentence, factual, and grounded in the text.
- If no useful entities exist beyond the capability itself, return just that entity.
- Use TASK_ENTITY for mission goals, constraints, required inputs, and requested effects.
- Use CONTEXT_ENTITY for actors, objects, places, environment conditions, and platform subsystems.
- Use PLAN_PATTERN and FAULT_CONCEPT only when the text clearly mentions them.

Output rules:
- Return exactly one JSON object.
- Do not include markdown fences.
- Schema: {"entities":[{"name":"...","type":"CAPABILITY|TASK_ENTITY|CONTEXT_ENTITY|PLAN_PATTERN|FAULT_CONCEPT","description":"..."}]}

Example input:
Capability name: Capability_runner_nav2/WaypointRunner. Text: This capability depends on the navigation stack functionalities and allows the robot to navigate to a location given by two dimensional coordinates. This capability can be used by the planner to move the robot to a required position and orientation.

Example output:
{"entities":[{"name":"Capability_runner_nav2/WaypointRunner","type":"CAPABILITY","description":"Navigates the robot to a requested pose using the navigation stack."},{"name":"move the robot to a required position and orientation","type":"TASK_ENTITY","description":"The requested navigation objective that the capability can satisfy."},{"name":"robot","type":"CONTEXT_ENTITY","description":"The mobile platform moved by the capability."},{"name":"location","type":"CONTEXT_ENTITY","description":"The destination that the robot should reach."},{"name":"two dimensional coordinates","type":"TASK_ENTITY","description":"The parameter representation used to specify the destination."},{"name":"navigation stack","type":"CONTEXT_ENTITY","description":"The navigation subsystem required for execution."}]}

Actual Input Text: )";

/**
 * @brief A system prompt for extracting relationships from text.
 * This prompt instructs the model to emit graph-ready edges for capability descriptions.
 */
const std::string RELATIONSHIP_EXTRACTION_PROMPT = R"(
You extract graph edges from robotics capability descriptions.

Goal:
- Return only relationships that are explicit or strongly implied by the text and useful for planning or retrieval.
- Prefer edges involving the named capability, dependencies, inputs, outputs, actors, destinations, and controlled objects.

Rules:
- Use only entities from the provided entity list.
- `source` and `target` must exactly match names from that list.
- `relationship` must be a short verb phrase such as "depends on", "navigates to", "uses", "controls", "requires", "produces", or "invoked by".
- Keep relationships directional.
- Do not emit duplicates or near-duplicates.
- Do not emit self-edges unless the text explicitly states recursion or self-reference.
- Skip weak semantic links that are too generic to help retrieval.

Output rules:
- Return exactly one JSON object.
- Do not include markdown fences.
- Schema: {"relationships":[{"source":"...","target":"...","relationship":"..."}]}

Example input:
Capability name: Capability_runner_nav2/WaypointRunner. List of Entities: ["Capability_runner_nav2/WaypointRunner", "navigation stack", "robot", "location", "planner", "two dimensional coordinates"]. Text: This capability depends on the navigation stack functionalities and allows the robot to navigate to a location given by two dimensional coordinates. This capability can be used by the planner to move the robot to a required position and orientation.

Example output:
{"relationships":[{"source":"Capability_runner_nav2/WaypointRunner","target":"navigation stack","relationship":"depends on"},{"source":"Capability_runner_nav2/WaypointRunner","target":"robot","relationship":"moves"},{"source":"robot","target":"location","relationship":"navigates to"},{"source":"location","target":"two dimensional coordinates","relationship":"specified by"},{"source":"planner","target":"Capability_runner_nav2/WaypointRunner","relationship":"invokes"}]}

Actual Input Text: )";

const std::string DESCRIPTION_COMBINATION_PROMPT = R"(
You combine multiple graph descriptions into one retrieval-friendly description.

Goal:
- Merge overlapping statements without losing concrete capability, dependency, actor, object, or location details.
- Prefer compact factual prose over generic summaries.

Rules:
- Preserve explicit names.
- Remove repetition.
- Resolve obvious grammar issues.
- Do not introduce speculation or new facts.
- Keep the result to 1-3 sentences.

Output rules:
- Return exactly one JSON object.
- Do not include markdown fences.
- Schema: {"combined_description":"..."}

Example input:
"This capability depends on the navigation stack functionalities and allow the robot to navigate to a location given by two dimensional coordinates. This capability can be used by the planner to move the robot to a required position and orientation."
"This capability depends on the navigation stack functionalities and allow the robot to navigate to a location given by two dimensional coordinates."

Example output:
{"combined_description":"This capability depends on the navigation stack and moves the robot to a requested pose specified by two dimensional coordinates. The planner can invoke it to reach a required position and orientation."}

Actual Input Text:  )";

}  // namespace experince::llm
