# Parameter Explanation

This document explains the main ROS parameters currently used by Experience,
with emphasis on the scorer and estimator settings in
`experience_server/config/experience_server.yaml`.

The parameters fall into four layers:

1. plugin selection,
2. scorer policy,
3. estimator behavior,
4. planner and interface wiring.

The practical rule is simple:

- estimator parameters affect how evidence becomes numbers,
- transform parameters affect how raw units become normalized penalties,
- feasibility parameters decide pass or fail,
- weights decide trade-offs among feasible plans.

## Top-Level Plugin Parameters

| Parameter | Description | Current Default / Notes |
| --- | --- | --- |
| `core_plugin` | Selects the knowledge core implementation. | `experience::GraphKnowledgeCore`. Change this only when swapping the core storage or retrieval backend. |
| `parsing_plugin` | Selects Fabric's parser, shared internally by planning, scoring, and dispatch. This parameter is not under `experience`. | `fabric::XMLParser`; no behavior-interface plugin is needed. |
| `planner_plugin` | Selects the planner plugin responsible for prompt generation and plan refinement. | `experience::PlanningPlugin` |
| `scorer_plugin` | Selects the candidate-plan scorer implementation. | `experience::RapfsScorerPlugin` |

## Planner Parameters

Planner-specific settings live under `PlanningPlugin`.

| Parameter | Description | Current Default / Notes |
| --- | --- | --- |
| `PlanningPlugin.model_family` | Model backend family used by PromptTools. | `openai` |
| `PlanningPlugin.model` | Concrete model name requested from PromptTools. | `gpt-5.1` |
| `PlanningPlugin.use_cache` | Whether prompt responses may be reused from cache. | `true` |
| `PlanningPlugin.use_chat_mode` | Whether PromptTools should use chat-style prompting. | `false` |
| `PlanningPlugin.max_capability_context` | Maximum number of ranked capabilities injected into planning prompts. | `8` |
| `PlanningPlugin.semantic_context_mode` | Controls whether planning uses detailed or community-heavy retrieval summaries. | `detailed` |
| `PlanningPlugin.log_semantic_context_comparison` | Logs prompt-size comparison between detailed and community context assembly. | `true` |

### Planner Refinement Policy

Refinement retry policy is enforced by the server, but the parameters now live under the active planner plugin namespace so they can travel with planner configuration.

| Parameter | Description | Current Default / Notes |
| --- | --- | --- |
| `PlanningPlugin.refinement.max_attempts` | Maximum number of validation-driven refinement retries after the initial planner pass. | `2` |
| `PlanningPlugin.refinement.retry_on_penalty_findings` | Whether penalty-level validation findings trigger another refinement attempt even when no hard reject occurred. | `true` |
| `PlanningPlugin.refinement.retry_on_warning_findings` | Whether warning-only validation findings trigger another refinement attempt. | `false` |

Current default:

```yaml
PlanningPlugin:
	refinement:
		max_attempts: 2
		retry_on_penalty_findings: true
		retry_on_warning_findings: false
```

This policy is evaluated after candidate parsing and validation. When all candidates are rejected, or when enabled severity thresholds are present, Experience can call the planner's refinement hook again using validator findings as critique.

## CoreGraphRag Parameter-Edge Settings

GraphRAG parameter-flow retrieval settings now live under `CoreGraphRag.edge.parameter`.

| Parameter | Description | Current Default / Notes |
| --- | --- | --- |
| `CoreGraphRag.edge.parameter.min_score` | Minimum structural score retained for weak parameter-flow retrieval. | `0.35` |
| `CoreGraphRag.edge.parameter.strong_score` | Threshold for `strong` edge classification when required-input coverage is complete. | `0.65` |
| `CoreGraphRag.edge.parameter.max_weak_dependencies` | Maximum number of weak parameter dependencies included in retrieved semantic context. | `2` |

These thresholds also drive persisted `edge_strength_class` labels:

- `strong` at or above `strong_score` with complete required-input coverage
- `medium` between `min_score` and `strong_score` with complete required-input coverage
- `weak` below `min_score`, or any edge with partial required-input coverage

## Estimator Plugin List

| Parameter | Description | Current Default / Notes |
| --- | --- | --- |
| `estimator_plugins` | Lists the feature estimators used to populate the RAPFS node vector `x_i = [p_i, t_i, e_i, f_i, r_i, u_i]`. | Removing an estimator usually increases missing evidence and therefore tends to raise uncertainty. |

Current default:

```yaml
estimator_plugins:
	- "experience::SuccessProbabilityPlugin"
	- "experience::RuntimePlugin"
	- "experience::EnergyPlugin"
	- "experience::FaultBurdenPlugin"
	- "experience::ResourcePlugin"
	- "experience::UncertaintyPlugin"
```

## RAPFS Scorer Parameters

All scorer parameters live under `RapfsScorerPlugin`.

### Version Labels

These do not change behavior by themselves. They label which policy generated a
decision and are persisted for explanation, calibration, and epoch analysis.

| Parameter | Description |
| --- | --- |
| `normalizer_version` | Label for the current transform set. |
| `estimator_version` | Label for the estimator semantics currently in use. |
| `weight_version` | Label for the current weight vector. |

Example:

```yaml
normalizer_version: "calibrated_v1"
weight_version: "baseline_v2"
estimator_version: "baseline_v2"
```

### Priors And Fallbacks

| Parameter | Description | Tuning Effect |
| --- | --- | --- |
| `default_success_probability` | Fallback success probability when no usable success evidence exists. | Higher values make unknown capabilities look safer. |
| `default_uncertainty` | Fallback uncertainty when no usable uncertainty evidence exists. | Higher values make missing data more conservative. |
| `unknown_unfamiliarity_prior` | Fallback unfamiliarity when semantic retrieval does not provide a reliable familiarity signal. | Higher values penalize semantically unfamiliar tasks more strongly. |

### Plan-Level Uncertainty Policy

| Parameter | Description | Allowed Values / Notes |
| --- | --- | --- |
| `plan_uncertainty_aggregation` | Controls how per-scenario uncertainty becomes one plan-level value. | `scenario_weighted_mean`: probability-weighted mean over reachable scenarios. `reachable_node_max`: worst-case maximum over reachable scenarios. `scenario_weighted_mean` is the current default because it preserves scenario likelihood. `reachable_node_max` is stricter and is useful when any high-risk branch should dominate the plan decision. |
| `uncertainty_weight_locked` | Policy flag that keeps `w_u` fixed. | Default: `true`. This should remain `true` unless a separate uncertainty calibration target exists. Task success alone is not a direct uncertainty label. |
| `uncertainty_weight_target` | Optional external target name used when experimenting with uncertainty-weight calibration. | Examples of legitimate targets include operator interventions or overrides. An empty value means there is no such target available. |

### Uncertainty Composition Weights

These determine how the node-level uncertainty term is composed.

| Parameter | Description |
| --- | --- |
| `uncertainty_statistical_weight` | Weight on uncertainty derived from statistical evidence. |
| `uncertainty_task_ambiguity_weight` | Weight on task-description ambiguity. |
| `uncertainty_unfamiliarity_weight` | Weight on semantic unfamiliarity. |
| `uncertainty_missing_evidence_weight` | Weight on explicitly missing evidence. |

Current default split:

```yaml
uncertainty_statistical_weight: 0.40
uncertainty_task_ambiguity_weight: 0.15
uncertainty_unfamiliarity_weight: 0.25
uncertainty_missing_evidence_weight: 0.20
```

The sum should remain close to `1.0` for interpretability.

### RAPFS Weights

These are the plan-ranking trade-off weights applied after feasibility checks.

The preferred configuration shape is now:

```yaml
RapfsScorerPlugin:
	estimator_names: ["success", "time", "energy", "fault", "resource", "uncertainty"]
	estimators:
		success:
			type: "experience::SuccessProbabilityPlugin"
			weight: 0.40
		time:
			type: "experience::RuntimePlugin"
			weight: 0.15
		energy:
			type: "experience::EnergyPlugin"
			weight: 0.10
		fault:
			type: "experience::FaultBurdenPlugin"
			weight: 0.10
		resource:
			type: "experience::ResourcePlugin"
			weight: 0.05
		uncertainty:
			type: "experience::UncertaintyPlugin"
			weight: 0.20
```

This shape lets one namespace describe both:

- which estimator plugin type should be loaded,
- which RAPFS weight should be applied to that feature.

| Weight Key | Description |
| --- | --- |
| `estimators.success.weight` | Reward for higher predicted completion probability. |
| `estimators.time.weight` | Penalty on normalized runtime. |
| `estimators.energy.weight` | Penalty on normalized energy use. |
| `estimators.fault.weight` | Penalty on predicted fault burden. |
| `estimators.resource.weight` | Penalty on predicted resource demand. |
| `estimators.uncertainty.weight` | Penalty on expected uncertainty. |

These weights should remain fixed within an evaluation epoch.

### Feasibility Thresholds

These are hard constraints, not preferences.

| Parameter | Description |
| --- | --- |
| `feasibility.min_success_probability` | Minimum acceptable success probability. |
| `feasibility.max_runtime_seconds` | Maximum acceptable runtime. |
| `feasibility.max_energy_delta_percentage` | Maximum acceptable energy change percentage. |
| `feasibility.max_fault_burden` | Maximum acceptable fault burden. |
| `feasibility.max_resource_demand` | Maximum acceptable resource demand. |
| `feasibility.max_uncertainty` | Maximum acceptable aggregate uncertainty. |

```yaml
feasibility:
	min_success_probability: 0.20
	max_runtime_seconds: 300.0
	max_energy_delta_percentage: 50.0
	max_fault_burden: 1.5
	max_resource_demand: 1.0
	max_uncertainty: 0.95
```

If any threshold fails, the candidate is rejected before final ranking.

### Piecewise-Linear Transforms

The transform groups convert raw units into normalized penalties in `[0, 1]`.

| Transform Group | Description |
| --- | --- |
| `runtime_seconds` | Converts raw runtime into a normalized runtime penalty. |
| `energy_delta_percentage` | Converts energy change into a normalized energy penalty. |
| `fault_burden` | Converts fault burden into a normalized penalty. |
| `resource_demand` | Converts resource demand into a normalized penalty. |
| `semantic_familiarity` | Converts familiarity into an unfamiliarity penalty used by the uncertainty model. |

Each uses:

```yaml
inputs: [...]
outputs: [...]
```

Interpretation:

- `inputs` are raw breakpoints,
- `outputs` are normalized penalty values,
- interpolation between points is linear.

For `semantic_familiarity`, the output is inverted relative to familiarity so
high familiarity becomes low unfamiliarity penalty.

## Estimator-Specific Parameters

### `SuccessProbabilityPlugin`

```yaml
alpha: 1.0
beta: 1.0
use_beta_smoothing: true
```

| Parameter | Description |
| --- | --- |
| `alpha` | Beta-prior pseudocount. |
| `beta` | Beta-prior pseudocount. |
| `use_beta_smoothing` | Whether sparse evidence uses posterior smoothing. |

### `UncertaintyPlugin`

```yaml
alpha: 1.0
beta: 1.0
credible_interval_z: 1.96
```

| Parameter | Description |
| --- | --- |
| `alpha` | Prior parameter. |
| `beta` | Prior parameter. |
| `credible_interval_z` | Width of the confidence or credible interval used to express statistical uncertainty. |

### `FaultBurdenPlugin`

```yaml
warning_weight: 1.0
error_weight: 3.0
```

| Parameter | Description |
| --- | --- |
| `warning_weight` | Weight applied to warning-level evidence when building fault burden. |
| `error_weight` | Weight applied to error-level evidence when building fault burden. |

Errors currently count more heavily than warnings when building fault burden.

## Planner Parameters

| Parameter | Description | Notes |
| --- | --- | --- |
| `PlanningPlugin.model_family` | Planner backend family. | |
| `PlanningPlugin.model` | Specific model identifier. | |
| `PlanningPlugin.use_cache` | Whether repeated prompt calls may reuse cached responses. | |
| `PlanningPlugin.use_chat_mode` | Planner prompting mode flag. | |
| `PlanningPlugin.max_capability_context` | Maximum number of capability-context items to inject into prompts. | |
| `PlanningPlugin.semantic_context_mode` | Selects how semantic retrieval is assembled into the prompt. | Known current mode: `detailed`. |
| `PlanningPlugin.log_semantic_context_comparison` | Whether detailed versus reduced retrieval prompt sizes are logged for analysis. | |

## Interface Parameters

| Parameter | Description |
| --- | --- |
| `CapabilitiesPlugin.services.get_runnable_specs` | Service name used to retrieve runnable capability specifications. |
| `FabricPlugin.services.parse_plan` | Service name used to validate or parse Fabric plans. |

## Practical Parameter Boundaries

Use these rules when changing parameters:

1. Change estimator priors when raw evidence interpretation is wrong.
2. Change transforms when normalized penalties are miscalibrated.
3. Change feasibility thresholds when mission limits are wrong.
4. Change RAPFS weights only between declared evaluation epochs.
5. Do not unlock `w_u` unless you also define and log a separate uncertainty target.
