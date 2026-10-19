# Knowledge Store To RAPFS

RAPFS ranks candidate plans. The knowledge store supplies the numbers RAPFS
needs for each capability in each plan.

For every capability instance $i$, the scorer needs:

$$
x_i=[p_i,t_i,e_i,f_i,\\mathbf{r}_i,u_i]
$$

where $p_i$ is success probability, $t_i$ is time, $e_i$ is energy, $f_i$ is
fault burden, $\\mathbf{r}_i$ is resource demand, and $u_i$ is uncertainty.

## Read Flow

For each planning request:

1. The planner proposes candidate Fabric plan graphs.
2. Experience gathers capability metadata, fallback evidence, and task context.
3. PromptTools produces a draft Fabric XML plan from that context.
4. Experience parses the draft into a graph, identifies issues, and prompts a refined Fabric XML plan.
5. The final parsed graph identifies which capability and context IDs each plan node refers to.
6. The statistical graph fetches matching empirical evidence, using exact context, broader context class, capability aggregate, and then the default prior in the default scoring path.
7. Optional proxy fallbacks such as semantic-neighbor aggregates and winning-plan priors remain available, but they are no longer part of the default candidate scoring path.
8. Estimators convert evidence into $p_i,t_i,e_i,f_i,\mathbf{r}_i,u_i$.
9. Fabric can submit the selected plan together with `planning_request_id`, `candidate_plan_id`, and `graph_hash` as `FabricMetaData`.
10. Fabric propagates those metadata fields in `SystemStatus`, letting Experience attach terminal outcomes to the selected candidate plan without a separate registration step.
11. Experience persists a selected-node explanation record containing the chosen evidence key, estimator window, fallback level, explanation text, estimator outputs, and a fixed RAPFS-aligned feature vector for each selected node.
12. RAPFS aggregates those node estimates over the original Fabric XML control tree, applies hard feasibility thresholds, and ranks the surviving plans.

## What Each Store Contributes

| Store | Contribution to RAPFS |
|---|---|
| Semantic graph | task relevance, identity resolution, related entities, plan patterns, unfamiliarity |
| Vector index | text similarity for descriptions, logs, tasks, and prior contexts |
| Statistical graph | success, time, energy, faults, resource evidence, statistical uncertainty |

Semantic similarity is not success probability. It helps choose candidates and
estimate unfamiliarity, but reliability comes from runtime evidence.

Winning-plan priors are also not direct reliability measurements. They are a
read-side aggregate over previously selected candidate plans and their eventual
outcomes, used only when capability-local evidence is still absent.

## Feature Estimates

- $p_i$ comes from successful runs divided by completed runs.
- $t_i$ comes from valid start/stop durations.
- $e_i$ comes from battery deltas.
- $f_i$ comes from warning/error rates.
- $\\mathbf{r}_i$ should come from resource telemetry, but that telemetry is
	not available yet.
- $u_i$ currently blends estimator-side statistical uncertainty with a transformed semantic-familiarity signal and missing-evidence defaults; the full multi-component uncertainty model remains an open calibration task.

Failure risk and uncertainty are different. A capability can be known to fail
often, which lowers $p_i$, without being uncertain. A capability can also have
little data, which raises $u_i$.

## Plan Aggregation

RAPFS must respect the Fabric graph shape:

- a sequential chain needs every required step to succeed,
- `parallel_all` needs all required branches,
- `parallel_any` can succeed if one branch succeeds,
- recovery plans can still succeed after a primary failure if recovery works.

That means multiplying every capability probability together is only valid for
a simple critical chain. Parallel and recovery plans need graph inference or
simulation.

The current implementation walks the original Fabric XML tree directly:

- `sequential` multiplies success along the chain, sums runtime, energy, and fault burden, and takes the maximum resource and uncertainty seen along the reachable path.
- `parallel_all` assumes every branch must succeed, multiplies branch success, uses the branch makespan for runtime, sums energy and fault burden across branches, and takes the maximum concurrent resource/uncertainty summary.
- `parallel_any` uses an independence baseline $1-\prod_j(1-p_j)$ for success, approximates runtime from success-weighted winning branches, and weights energy and fault burden by each branch's probability of being the first success.
- `recovery` treats the recovery body as executing with trigger probability $1-p_{\mathrm{primary}}$ and adds its expected costs only in those scenarios.

These are baseline approximations that make branching plans rankable today. Exact inference or Monte Carlo evaluation is still the intended next step.

Costs must also follow the graph shape. Time is wall-clock makespan, so
parallel work overlaps instead of being summed. Energy and faults are summed
only for the steps that actually execute in a scenario. Peak resources are
checked component by component at the busiest moment, not by adding every node
in the graph.

Before ranking, the current scorer rejects plans that fall below the configured
minimum success threshold or exceed configured runtime, energy, fault,
resource, or uncertainty budgets. Those thresholds, together with the
piecewise-linear cost transforms, are versioned through the scoring config and
recorded with the selected plan explanation.

## Explanation Record

Every score should leave a record showing what evidence was used: the plan ID,
graph hash, capability/context IDs, fallback level for each estimate, node
features, plan-level score components, feasibility result, active estimator,
normalizer and weight versions, plus missing-evidence and unfamiliarity flags.
That record is what makes later debugging and calibration possible.

The current implementation persists both per-node and per-edge candidate attribution, plus a dedicated `plan_node_score_explanation` artifact for selected nodes. That artifact stores requested and selected capability/context identity, selected key, estimator window, fallback level, explanation text, estimator outputs, and a fixed RAPFS-aligned feature vector. The selected plan explanation can be retrieved through the `experience/plan/score_explanation` service.

## Current Status

The current data can support success, time, energy, and fault estimates. It
cannot yet support quantitative resource demand. The scorer already has a
resource term and a resource feasibility threshold, but until telemetry exists
those values mostly remain estimator defaults and missing resource data is still
represented through higher uncertainty.
