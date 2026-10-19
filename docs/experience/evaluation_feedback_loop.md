# Evaluation Feedback Loop

This document covers the evaluation-facing loop around Experience scoring: inspect the stored explanation for a selected plan, attach external outcome labels, and record scorer epoch checkpoints for later calibration and bias analysis.

The goal is not to restate the full planning architecture. Instead, this page focuses on the operational path used after candidate selection, when you want to understand why a plan was chosen and how to feed execution outcomes back into later evaluation.

## Inspect Selected Plan Explanations

After sourcing the workspace, you can inspect the stored explanation for a selected plan with:

```bash
ros2 run experience_server get_plan_score_explanation.py <planning_request_id> <candidate_plan_id> <graph_hash>
```

To drill down to a specific runner node inside that selected plan:

```bash
ros2 run experience_server get_plan_score_explanation.py <planning_request_id> <candidate_plan_id> <graph_hash> --candidate-node-id runner_1
```

When you want a headless persisted-data check before bringing the ROS service path into the loop, you can verify one stored explanation row directly from SQLite:

```bash
ros2 run experience_server verify_plan_score_explanation.py \
	/path/to/core_graphrag.sqlite3 \
	<planning_request_id> \
	<candidate_plan_id> \
	<graph_hash> \
	--candidate-node-id runner_1
```

For broader offline review across saved requests, combine that explanation verification step with:

```bash
ros2 run experience_inspector run_offline_functional_workflow.py \
	--db-path /path/to/core_graphrag.sqlite3 \
	--audit-root /path/to/planner_prompt_audit \
	--feedback-root /path/to/feedback_root
```

That workflow emits the request-level decision-support export and, when requested, the persisted explanation verification report. It helps decide whether detailed or community semantic prompt context is carrying enough signal on the requests that were actually selected and reviewed.

The full request-to-explanation lifecycle is shown in [Implementation Pipeline](implementation.md), including a concrete runtime example of how selected plan metadata is propagated into a stored score explanation record.

## Replay A Fixed XML Plan Without Execution

After sourcing the workspace, you can score a saved Fabric XML plan against the currently running Experience server without dispatching it for execution:

```bash
ros2 run experience_server replay_plan_score.py "pick up the object and place it on the table" /path/to/plan.xml
```

This calls the `experience/plan/score` service, which parses the supplied XML, runs the normal Experience validation and RAPFS scoring path, and returns a `PlanScoreExplanation`-shaped response entirely in memory. It does not submit the plan to Fabric, does not wait for Fabric runtime telemetry, and does not require any execution feedback.

The replay script accepts two optional flags that are only for experiment bookkeeping:

- `--epoch <epoch_id>` adds an arbitrary label such as `epoch_2026q3` to the emitted snapshot rows. This does not change scoring behavior and does not update the server's stored scoring-epoch records by itself.
- `--snapshot-out <path>` appends one JSON object per scored plan to the given JSONL file. Each row contains the epoch label, plan path, graph hash, and the plan-level score fields so repeated runs can be compared later.

You can omit both flags. If you do:

- the plan is still scored normally,
- nothing is written to a snapshot file, and
- the server uses whatever GraphRAG, SQLite-backed evidence, vector index, and runtime configuration are already loaded in the currently running Experience instance at the moment of the service call.

In other words, omitting `--epoch` and `--snapshot-out` does not fall back to an older dataset or disable retrieval. The score always reflects the latest knowledge state held by the running Experience server process.

Example with snapshot capture:

```bash
ros2 run experience_server replay_plan_score.py \
	"pick up the object and place it on the table" \
	/path/to/plan.xml \
	--epoch epoch_2026q3 \
	--snapshot-out /tmp/experience_plan_scores.jsonl
```

Example without any bookkeeping flags:

```bash
ros2 run experience_server replay_plan_score.py \
	"pick up the object and place it on the table" \
	/path/to/plan.xml
```

## Attach External Plan Feedback

After sourcing the workspace, you can attach an external outcome label and optional human judgement to a selected plan with:

```bash
ros2 run experience_server record_plan_feedback.py <planning_request_id> <candidate_plan_id> <graph_hash> success --terminal-success --human-judgement "operator confirmed goal completion"
```

The service merges that feedback onto any existing selected-plan score row, preserving previously stored score components, versions, and Fabric-derived execution metrics.

For batch-oriented evaluation harnesses, you can submit a JSON or JSONL file of feedback records with:

```bash
ros2 run experience_server ingest_plan_feedback.py feedback.jsonl
```

Each record should include `planning_request_id`, `candidate_plan_id`, `graph_hash`, and `terminal_outcome`, with optional versions, timestamps, and `human_judgement` fields.

The repository root already includes a runnable sample at `feedback.jsonl`, so from this checkout you can use the command above directly as an evaluation-harness smoke test.

For operator-in-the-loop runs, the Experience server now also writes a per-execution `user_feedback.json` file every time a plan is successfully selected. By default this is stored under `~/.ros/experience_server/user_feedback/<execution_uuid>/user_feedback.json`, with `planning_request_id` used as the directory key when no execution UUID is supplied.

Each file is a numbered object of stages:

```json
{
	"1": {
		"plan": "<fabric xml>",
		"reasoning": "<selection reasoning>",
		"scoring_supported": true,
		"feasible": true,
		"sequential_graph": true,
		"feasibility_reason": "meets all feasibility thresholds",
		"score_name_list": ["success", "time", "energy", "fault", "resource", "uncertainty"],
		"expected_scores": [0.83, 48.6, 7.2, 0.05, 0.41, 0.18],
		"normalized_scores": [0.83, 0.24, 0.11, 0.05, 0.41, 0.18],
		"weighted_scores": [0.83, -0.24, -0.11, -0.05, -0.41, -0.18],
		"score_weights": [1.0, 1.0, 1.0, 1.0, 1.0, 1.0],
		"final_rapfs": 0.41,
		"score_components": "{\"rapfs\":{...}}",
		"calibration_version": "calib_v1",
		"normalizer_version": "norm_v1",
		"estimator_version": "est_v1",
		"weight_version": "weights_v1",
		"candidate_node_id_list": ["runner_1", "runner_2"],
		"candidate_skill_name_list": ["grasping_capabilities/MoveToNamedPoseRunner", "fabric_capabilities/FabricGeneratePlanRunner"],
		"feedback_success": null,
		"feedback_failed_candidate_node_ids": [],
		"feedback_failed_skill_names": [],
		"outcome": "",
		"feedback": "",
		"outcome_consumed": false,
		"last_consumed_outcome": "",
		"feedback_consumed": false,
		"last_consumed_feedback": "",
		"last_consumed_signature": "",
		"feedback_consumed_at_sec": 0,
		"feedback_consumed_at_nanosec": 0,
		"planning_request_id": "planning_request_...",
		"candidate_plan_id": "candidate_...",
		"graph_hash": "...",
		"execution_uuid": "..."
	}
}
```

`score_name_list` defines the shared ordering for `expected_scores`, `normalized_scores`, `weighted_scores`, and `score_weights`. `weighted_scores` stores signed RAPFS contributions so summing that list yields the pre-validation formula score, while `score_components` preserves the scorer's full structured breakdown verbatim.

Use the structured feedback fields when you need calibration effects instead of just a note:

- `feedback_success`: optional boolean plan-level override. `false` marks the selected plan as a failure even if `outcome` is empty.
- `feedback_failed_candidate_node_ids`: optional list of candidate node ids to penalize at runner granularity.
- `feedback_failed_skill_names`: optional list of skill names to penalize at runner granularity when node ids are inconvenient.

If only `feedback` is populated, it is stored as human judgement text and does not by itself change success or failure statistics.

Use `outcome` for structured labels such as `success`, `partial_success`, or `failure`. Keep `feedback` as the freeform operator note.

When `consume_feedback:=true` is enabled at launch, the server re-reads those files on startup, maps any changed non-empty `outcome` or `feedback_success` override into the stored plan feedback record's `terminal_outcome` and `terminal_success` fields, merges `feedback` into `human_judgement`, applies any targeted runner penalties, marks that stage as consumed, and writes a scorer-epoch snapshot so calibration summaries reflect the new feedback batch.

## Record Scoring Epoch Checkpoints

To mark an evaluation epoch or a scorer-ablation boundary:

```bash
ros2 run experience_server record_scoring_epoch.py epoch_2026q3 fixed_weight_baseline calib_v1 calibrated_v1 baseline_v2 baseline_v2 --calibration-summary "held-out warehouse runs" --selection-bias-summary "top-1 execution only" 
```

Resubmitting the same `epoch_id` updates the stored record, which is useful when an epoch opens with `active=true` and is later closed with end timestamps and finalized notes.

To inspect a stored scoring epoch over ROS instead of opening SQLite directly:

```bash
ros2 run experience_server get_scoring_epoch.py epoch_2026q3
```

The returned `calibration_summary` now combines any operator-supplied epoch note with live aggregate feedback metrics for the same `(calibration_version, normalizer_version, estimator_version, weight_version)` tuple, including sample count, observed success rate, Brier score, and runtime or energy absolute-error summaries.

The returned `selection_bias_summary` now combines any stored epoch note with live exposure metrics for that same scorer-version tuple, including how many candidates were logged, how many were selected, mean candidates per request, and score gaps between selected and unselected alternatives.

The RAPFS scorer now exposes `RapfsScorerPlugin.plan_uncertainty_aggregation` with `scenario_weighted_mean` as the default and `reachable_node_max` as a stricter alternative. It also treats `w_u` as policy-locked by default through `RapfsScorerPlugin.uncertainty_weight_locked`, and will force that lock when no separate uncertainty calibration target is configured.

The scorer also exposes `RapfsScorerPlugin.non_success_terminal_policy` to lock how aborted and timed-out runs affect the success and uncertainty estimators. The current `count_as_failure` policy treats those terminal outcomes as non-successes for the Beta success model, and the emitted `estimator_version` now includes that policy suffix so score rows and explanations remain comparable only within the same estimator semantics.

Taken together, these three access paths form the evaluation feedback loop:

1. Query the stored explanation to inspect why Experience selected a plan.
2. Attach observed terminal outcomes or human judgement after execution.
3. Record scorer epoch boundaries so calibration and selection-bias summaries stay tied to a declared version tuple.