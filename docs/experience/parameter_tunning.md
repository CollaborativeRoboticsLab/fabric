# Parameter Tuning Guide

This document describes how to tune Experience parameters without mixing up
three different problems:

1. bad estimates,
2. bad normalization,
3. bad preferences.

The ordering matters.

If the system is wrong because its runtime estimate is biased, changing RAPFS
weights is the wrong fix. If the system is wrong because battery cost is
over-penalized after normalization, changing estimator priors is the wrong fix.

Tune from lower-level semantics to higher-level preferences.

## Recommended Tuning Order

### Stage 1: Verify Raw Evidence

Before tuning any parameter, check that the stored evidence itself is sensible.

Inspect:

- capability runtime samples,
- energy deltas,
- warning and error counts,
- selected-plan explanations,
- plan feedback rows,
- scoring epoch summaries.

If these inputs are wrong, parameter tuning will only hide the problem.

### Stage 2: Tune Estimator Priors

Tune estimator priors when the system behaves badly under sparse data.

Parameters involved:

- `SuccessProbabilityPlugin.alpha`
- `SuccessProbabilityPlugin.beta`
- `SuccessProbabilityPlugin.use_beta_smoothing`
- `UncertaintyPlugin.alpha`
- `UncertaintyPlugin.beta`
- `UncertaintyPlugin.credible_interval_z`
- `FaultBurdenPlugin.warning_weight`
- `FaultBurdenPlugin.error_weight`

Typical adjustments:

- increase `alpha` and `beta` when early success estimates fluctuate too much,
- increase `credible_interval_z` when uncertainty intervals are too optimistic,
- increase `error_weight` when plans with recoverable warnings should still rank above plans with repeated hard faults.

### Stage 3: Tune Uncertainty Composition

Tune scorer-side uncertainty composition when the system is handling ambiguity,
unfamiliarity, or missing evidence poorly.

Parameters involved:

- `RapfsScorerPlugin.default_uncertainty`
- `RapfsScorerPlugin.uncertainty_statistical_weight`
- `RapfsScorerPlugin.uncertainty_task_ambiguity_weight`
- `RapfsScorerPlugin.uncertainty_unfamiliarity_weight`
- `RapfsScorerPlugin.uncertainty_missing_evidence_weight`
- `RapfsScorerPlugin.unknown_unfamiliarity_prior`
- `RapfsScorerPlugin.plan_uncertainty_aggregation`

Typical adjustments:

- raise `uncertainty_missing_evidence_weight` if plans with absent telemetry are still selected too aggressively,
- raise `unknown_unfamiliarity_prior` if unseen tasks are being treated as too safe,
- switch `plan_uncertainty_aggregation` to `reachable_node_max` if any rare but high-risk branch should dominate the plan-level uncertainty.

### Stage 4: Tune Transforms

Tune transforms when the raw estimated values are reasonable but the normalized
penalties are not.

Example symptom:

- runtime estimates are accurate, but 40 seconds is being penalized too weakly,
- energy estimates are accurate, but small battery deltas are over-penalized,
- semantic familiarity is present, but unfamiliarity still dominates the score.

Parameters involved:

- `RapfsScorerPlugin.transforms.runtime_seconds.*`
- `RapfsScorerPlugin.transforms.energy_delta_percentage.*`
- `RapfsScorerPlugin.transforms.fault_burden.*`
- `RapfsScorerPlugin.transforms.resource_demand.*`
- `RapfsScorerPlugin.transforms.semantic_familiarity.*`

Practical rule:

- move transform breakpoints when the 0-to-1 mapping is wrong,
- do not move RAPFS weights to compensate for a broken transform.

### Stage 5: Tune Feasibility Thresholds

Tune hard constraints only after estimates and transforms are stable.

Parameters involved:

- `RapfsScorerPlugin.feasibility.min_success_probability`
- `RapfsScorerPlugin.feasibility.max_runtime_seconds`
- `RapfsScorerPlugin.feasibility.max_energy_delta_percentage`
- `RapfsScorerPlugin.feasibility.max_fault_burden`
- `RapfsScorerPlugin.feasibility.max_resource_demand`
- `RapfsScorerPlugin.feasibility.max_uncertainty`

Use this when the robot should simply refuse certain plans regardless of their
relative ranking.

### Stage 6: Tune RAPFS Weights

Tune weights only after the lower layers are credible.

Parameters involved:

- `RapfsScorerPlugin.estimators.success.weight`
- `RapfsScorerPlugin.estimators.time.weight`
- `RapfsScorerPlugin.estimators.energy.weight`
- `RapfsScorerPlugin.estimators.fault.weight`
- `RapfsScorerPlugin.estimators.resource.weight`
- `RapfsScorerPlugin.estimators.uncertainty.weight`

Important rules:

1. Freeze weights within an epoch.
2. Change one weight set at a time.
3. Record a new `weight_version` whenever weights move.
4. Compare epochs against the same evaluation task distribution when possible.

## How To Tune Specific Behaviors

### The scorer is overconfident on sparse evidence

Try, in order:

1. increase success-prior smoothing,
2. increase `credible_interval_z`,
3. increase `default_uncertainty`,
4. increase `uncertainty_missing_evidence_weight`.

### The scorer rejects too many unfamiliar tasks

Try, in order:

1. lower `unknown_unfamiliarity_prior`,
2. soften the `semantic_familiarity` transform,
3. lower `estimators.uncertainty.weight` only if the previous two changes still leave a correct uncertainty signal but too strong a preference penalty.

### The scorer prefers fast but brittle plans

Try, in order:

1. check whether success estimates are underestimating failure,
2. check whether fault burden transform is too flat,
3. increase `estimators.success.weight` or `estimators.fault.weight`,
4. only then consider reducing `estimators.time.weight`.

### The scorer treats uncertainty too weakly

Try, in order:

1. verify missing evidence is being counted correctly,
2. switch `plan_uncertainty_aggregation` from `scenario_weighted_mean` to `reachable_node_max`,
3. increase uncertainty composition terms,
4. increase `estimators.uncertainty.weight` within a new weight epoch.

### The scorer treats uncertainty too strongly

Try, in order:

1. verify semantic familiarity is not collapsing to zero too often,
2. lower `unknown_unfamiliarity_prior`,
3. reduce `uncertainty_missing_evidence_weight` if missing data is common but operationally tolerable,
4. reduce `estimators.uncertainty.weight` only after confirming the uncertainty signal itself is valid.

## Tuning `w_u`

The uncertainty weight is special.

Do not tune `RapfsScorerPlugin.estimators.uncertainty.weight` from plain task success or
failure labels alone. Those outcomes do not directly measure uncertainty.

Current policy:

- keep `RapfsScorerPlugin.uncertainty_weight_locked: true`,
- leave `RapfsScorerPlugin.uncertainty_weight_target: ""` unless a separate uncertainty target exists.

Only unlock `w_u` if you are using a separate target such as:

- operator intervention,
- operator override,
- human safety review,
- abort triggered by uncertainty,
- interval coverage or confidence failure.

## Epoch Practice

When changing transforms or weights:

1. create a new version label,
2. record a new scoring epoch,
3. run evaluation requests,
4. ingest external plan feedback,
5. inspect `get_scoring_epoch.py` output,
6. compare calibration and selection-bias summaries against the prior epoch.

Example:

```bash
ros2 run experience_server record_scoring_epoch.py \
	epoch_2026q3 calibrated_baseline calib_v2 calibrated_v2 baseline_v3 baseline_v3 \
	--calibration-summary "runtime transform retuned from warehouse set" \
	--selection-bias-summary "same task mix as epoch_2026q2"
```

## A Minimal Safe Workflow

For most changes, use this loop:

1. change one parameter group,
2. increment the matching version label,
3. rebuild and relaunch,
4. generate plans on a fixed request set,
5. execute or simulate the chosen plans,
6. ingest feedback with `ingest_plan_feedback.py`,
7. inspect the epoch with `get_scoring_epoch.py`,
8. keep or revert the change based on calibration and selection-bias evidence.

## Current Default Intent

The current defaults encode this policy:

- prefer success first,
- penalize uncertainty materially but not overwhelmingly,
- treat missing evidence as real uncertainty,
- aggregate plan uncertainty by scenario likelihood unless a stricter deployment demands worst-case behavior,
- keep `w_u` fixed until a proper uncertainty target exists.
