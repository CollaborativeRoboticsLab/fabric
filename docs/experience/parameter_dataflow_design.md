# Parameter Dataflow Design

This document defines the Experience-side interpretation of provider parameter metadata and inferred parameter-mediated relationships.

Fabric execution-time validation is documented separately in [fabric/parameter_validation.md](https://github.com/CollaborativeRoboticsLab/fabric/blob/experience-integration/docs/parameter_validation.md).

The goal is to support two related behaviors:

1. allow a single logical parameter to be satisfied by multiple sources without forcing artificial renaming, and
2. expose parameter-mediated relationships between capabilities so retrieval and validation can reason about data flow even when dependencies were not declared explicitly.

## 1. Canonical Parameter Identity

Each parameter may expose multiple identifiers, but the canonical matching order is:

1. `semantic_key`
2. `name`
3. `aliases`

Interpretation rules:

- `semantic_key` is the stable cross-capability identity.
- `name` is the local runner binding name.
- `aliases` support migration and looser matching, but should not override a stable `semantic_key`.

## 2. Fallback Satisfaction Semantics

Parameter role is defined by the section where it appears:

- `configuration_parameters`: local configuration only
- `runtime_parameters.input`: consumed during execution
- `runtime_parameters.output`: produced during execution

When a runtime input can also be satisfied locally, the declared source list controls what is allowed.

Supported source labels:

- `upstream`: a previous capability produces a matching runtime output
- `configuration`: a local configuration parameter satisfies the runtime input
- `external`: the plan or environment injects a value directly for this runner
- `default`: the runtime input or its fallback configuration parameter defines a default value

Satisfaction precedence is:

1. upstream runtime output
2. explicit plan-provided value
3. configuration fallback parameter
4. declared default value

Rules:

- configuration parameters do not create inter-capability edges on their own
- runtime output to runtime input is the only relation that creates a dataflow edge
- a runtime input may share the same `name` as a configuration parameter if `satisfiable_from` and `fallback_parameter` make the fallback explicit
- if `required` is `false`, an unsatisfied runtime input is not a validation failure
- if `required` is `true`, at least one allowed source must satisfy it

## 3. Experience Validation Rules

Experience validation should reason over parsed candidate plans, the retrieved capability set, and the inferred parameter-flow graph, not just direct neighbors in one concrete Fabric plan.

For each candidate capability chain or retrieved subgraph:

1. build output-to-input parameter edges using canonical identity and type compatibility
2. annotate each input with its satisfaction mode: upstream, configuration, external, default, or missing
3. compute coverage per capability as matched required runtime inputs divided by total required runtime inputs
4. classify the capability chain:
   - `valid`: all required runtime inputs are satisfied
   - `weak`: some inputs are satisfied but coverage is partial or depends on lower-confidence alias matches
   - `invalid`: one or more blocking required inputs are unsatisfied

Experience should use this classification in retrieval, scoring, and replanning:

- `valid` chains may be passed forward normally
- `weak` chains should remain retrievable but incur a ranking penalty and produce explanation warnings
- `invalid` chains should be rejected for execution planning and trigger replanning or capability-set expansion

Current implementation boundary:

- server-side validation runs after candidate parsing and before RAPFS scoring
- `experience::CompatibilityValidation` rejects malformed or unknown candidate references
- `experience::DataflowValidation` rejects unsatisfied required runtime inputs and emits warning or penalty findings for weak but satisfiable dataflow
- when every candidate is rejected, Experience can invoke planner refinement again with validator findings as critique

This is the correct layer to diagnose the “3 parameters, only 1 match” case. Fabric should only block when a chosen concrete plan is unsatisfied; Experience should judge whether a capability chain is semantically weak before it becomes the chosen plan.

## 4. GraphRAG Edge Scoring

Parameter-mediated edges are soft semantic edges, not declared hard dependencies.

Create an inferred edge only from:

- capability A runtime output
- capability B runtime input

Store the edge with the matched parameter identity and a score based on four core factors:

$$
S_{param} = w_k K + w_t T + w_c C + w_r R
$$

Where:

- $K$: canonical-key match score
- $T$: type compatibility score
- $C$: coverage contribution for the receiving capability
- $R$: role-direction score, where output-to-input is valid and all other directions score zero

Recommended interpretation:

- exact `semantic_key` match: highest `K`
- exact `name` match without `semantic_key`: medium `K`
- alias-only match: lower `K`
- exact type match: `T = 1`
- unspecified type on one side: discounted but allowed
- incompatible types: `T = 0`
- coverage contribution: matched required inputs divided by total required runtime inputs for the receiver

The edge record should preserve both the raw factors and the aggregate score so retrieval and explanation can distinguish “barely connected” from “strongly connected.”

Current implementation also persists a discrete `edge_strength_class` with the following threshold policy:

- `strong`: `structural_score >= strong_score` and required-input coverage is complete
- `medium`: `min_score <= structural_score < strong_score` and required-input coverage is complete
- `weak`: below `min_score`, or any edge with partial required-input coverage

The current defaults are loaded from `CoreGraphRag.edge.parameter.{min_score,strong_score,max_weak_dependencies}`.

Use this score in retrieval as follows:

- strong score: retrieve both capabilities together confidently
- medium score: retrieve together with lower rank
- weak score: keep as a soft candidate only if other semantic evidence supports the relation
- zero score: do not infer a parameter-flow relation

## 5. Code Surface Map

### Capabilities2

- `capabilities2_server/models/resource.hpp`: parameter metadata schema
- `capabilities2_server/models/defineable_base.hpp`: carries config/runtime parameter lists
- `docs/providers.md`: provider-spec contract and examples

### Experience

- `experience_base/utils/metadata.hpp`: parameter metadata used by retrieval and scoring
- `experience_interface_capabilities/runnable_spec_parser.hpp`: parses runnable specs into Experience metadata
- `experience_core_grag/core_graphrag.hpp`: parameter-mediated edge construction, scoring, weak/medium/strong classification, and retrieval filtering
- `experience_validator/*`: candidate compatibility and dataflow validators
- `experience_server/server.hpp`: validation orchestration, selected-plan persistence, and validation-driven planner refinement retry
- retrieval and explanation surfaces: active consumers of parameter-edge strength and validation outcomes

## 6. Implementation Boundary

Implemented now:

- provider-schema fields for parameter-role semantics
- Experience-side parameter metadata parsing
- Experience-side parameter-flow graph construction
- GraphRAG edge scoring and retrieval integration
- persisted `edge_strength_class` for parameter-flow edges
- candidate compatibility and dataflow validation plugins
- validation-driven planner refinement retry on rejected candidates