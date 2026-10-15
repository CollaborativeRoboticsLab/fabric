# Experience Scorer RAPFS

This package contains the current RAPFS-based plan scorer plugin for Experience.

Its role is narrow by design:

- accept already-generated candidate plans,
- resolve each runner node through `CoreBase`,
- ask estimator plugins for node-level feature estimates,
- aggregate those estimates over the original Fabric XML control tree,
- apply feasibility thresholds,
- return the best feasible candidate.

The package does not own ROS action wiring, planner prompting, or telemetry ingestion. Those remain in other packages.

## Plugin Boundary

The exported plugin class is:

- `fabric::RapfsScorerPlugin`

It implements the shared scorer contract from:

- [experience_base](../experience_base)

The server loads it through pluginlib using the `ScorerBase` interface.

Primary integration points:

- [experience_server](../experience_server)
- [experience_base](../experience_base)
- [experience_estimator](../experience_estimator)
- [experience_core_grag](../experience_core_grag)

## What The Plugin Consumes

The scorer receives three main inputs:

1. a ROS node for parameters and logging,
2. a shared `CoreBase` instance for identity resolution and evidence lookup,
3. a shared list of `EstimateBase` plugins for node-level estimation.

This separation matters because the scorer should rank plans, not own graph storage internals.

## Scoring Flow

For each candidate plan:

1. candidate runner nodes are normalized and resolved to stable skill and context IDs,
2. the scorer asks `CoreBase` for the best available evidence for each node,
3. estimator plugins convert evidence into node-level outputs,
4. the scorer composes final uncertainty from four components,
5. the original Fabric XML tree is parsed into a scoring tree,
6. whole-plan expected success and cost summaries are aggregated,
7. feasibility thresholds are applied,
8. the final RAPFS score is computed.

The feature vector per node is:

$$
x_i = [p_i, t_i, e_i, f_i, r_i, u_i]
$$

The current final score follows the repository RAPFS baseline:

$$
\operatorname{RAPFS}(G_\pi)
= w_s S(G_\pi)
- w_t \hat{T}(G_\pi)
- w_e \hat{E}(G_\pi)
- w_f \hat{F}(G_\pi)
- w_r \hat{C}_r(G_\pi)
- w_u \hat{U}(G_\pi)
$$

Related docs:

- [Risk-Aware Plan Feasibility Score](../docs/risk_aware_plan_score.md)
- [Knowledge To RAPFS](../docs/knowledge_to_rapfs.md)
- [Plan Ranking And Selection](../docs/plan_ranking_selections.md)

## Supported Control Shapes

The scorer walks the original Fabric XML structure rather than flattening everything to a plain edge chain.

Current support includes:

- `sequential`
- `parallel_all`
- `parallel_any`
- `recovery`

This package uses scenario-based aggregation over the XML control structure to compute whole-plan metrics.

In practice that means:

- sequential chains accumulate along the reachable path,
- `parallel_all` uses branch makespan for time and sums concurrent cost burdens,
- `parallel_any` models first-success behavior,
- `recovery` attaches recovery-body scenarios to primary-failure outcomes.

## Uncertainty Composition

The plugin does not treat uncertainty as a single opaque estimator value. It composes plan-node uncertainty from four terms:

- statistical uncertainty,
- task ambiguity,
- unfamiliarity,
- missing evidence.

The current default statistical term comes from the uncertainty estimator plugin, while the other terms are scorer policy.

Related docs:

- [Risk-Aware Plan Feasibility Score](../docs/risk_aware_plan_score.md)
- [Data To Equation Bridge](../docs/data_to_equation_bridge.md)

## Configuration

The plugin reads scoring parameters from the `RapfsScorerPlugin.*` namespace on the server node.

Important groups:

- version labels: `normalizer_version`, `weight_version`, `estimator_version`
- default priors: `default_success_probability`, `default_uncertainty`
- uncertainty weights
- plan uncertainty aggregation: `plan_uncertainty_aggregation = scenario_weighted_mean | reachable_node_max`
- uncertainty-weight policy: `uncertainty_weight_locked`, optional `uncertainty_weight_target`
- feasibility thresholds
- piecewise-linear transforms for runtime, energy, fault, resource, and familiarity

Parameter docs:

- [Parameter Explanation](../docs/parameter_explanation.md)
- [Parameter Tunning](../docs/parameter_tunning.md)

The default plugin selection is configured in:

- [experience_server/config/experience_server.yaml](../experience_server/config/experience_server.yaml)

## Outputs

For each candidate plan the plugin populates plan-level score fields such as:

- terminal success estimate,
- expected runtime,
- expected energy delta,
- expected fault burden,
- expected resource demand,
- expected uncertainty,
- feasibility result,
- final RAPFS score.

By default, `expected_uncertainty` is now aggregated as a scenario-weighted mean over per-scenario reachable-node uncertainty, while `reachable_node_max` remains available as a stricter policy mode.

The uncertainty weight `w_u` is treated as a policy constant by default. If no separate uncertainty calibration target is configured, the scorer forces `uncertainty_weight_locked=true` and records that policy in serialized score components.

The selected-plan explanation path persists those results through the server-side attribution and explanation flow.

## Build And Integration

This package is a pluginlib-loaded shared library and is intended to be built with the Experience workspace.

Relevant packages:

- [experience_server](../experience_server)
- [experience_base](../experience_base)
- [experience_estimator](../experience_estimator)

Example focused build:

```bash
colcon --log-base /home/ubuntu/colcon_ws/log build \
	--base-paths /home/ubuntu/colcon_ws/src \
	--build-base /home/ubuntu/colcon_ws/build \
	--install-base /home/ubuntu/colcon_ws/install \
	--packages-select experience_scorer_rapfs experience_server \
	--event-handlers console_direct+
```

## Package Scope

This package should remain focused on scorer policy and graph-aware plan aggregation.

It should not absorb:

- telemetry ingestion,
- planner prompting,
- persistent storage implementation,
- Fabric submission wiring.

That separation keeps the scorer replaceable. If a future non-RAPFS scorer is added, it should be able to implement the same `ScorerBase` contract without changing server orchestration.

## Related Reading

Use these docs together:

1. [Implementation Pipeline](../docs/implementation.md)
2. [Knowledge To RAPFS](../docs/knowledge_to_rapfs.md)
3. [Risk-Aware Plan Feasibility Score](../docs/risk_aware_plan_score.md)
4. [Parameter Explanation](../docs/parameter_explanation.md)
5. [Parameter Tunning](../docs/parameter_tunning.md)
6. [Data To Knowledge](../docs/data_to_knowledge.md)
