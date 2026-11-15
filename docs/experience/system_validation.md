# System Validation

This document captures the staged validation plan for the semantic explanation and retrieval path.

These validation stages are intentionally deferred until the current implementation pass is complete. The goal is to avoid tuning intermediate behavior before the remaining implementation items are in place.

## Scope

The staged checks below focus on three things:

1. verifying that semantic explanation data is present end to end, not only declared in structs and messages,
2. inspecting whether community-level retrieval carries enough signal to replace some detailed retrieved items, and
3. measuring the prompt-size impact of detailed versus community semantic retrieval at the planner boundary.

After those checks are run, retrieval, explanation output, and planner prompt-selection behavior can be tuned based on the observed results.

## Stage 1: Explanation Pipeline Validation

Objective: verify the explanation pipeline end to end.

Relevant implementation path:

- `planning.hpp`
- `rapfs_scorer_plugin.hpp`
- `server.hpp`
- `core_graphrag.hpp`
- `plan_score_explanation_adapter.hpp`
- `get_plan_score_explanation.py`

Concrete check:

1. Generate one plan through the normal planning flow.
2. Capture the resulting `planning_request_id`, `candidate_plan_id`, and `graph_hash`.
3. Query one node explanation through `experience/plan/score_explanation` using `get_plan_score_explanation.py`.
4. Confirm that the semantic explanation fields are populated with real values, not only structurally present.

Offline helper:

When you already have a persisted `core_graphrag.sqlite3`, you can verify one stored explanation row without bringing ROS services up:

```bash
ros2 run experience_server verify_plan_score_explanation.py \
	/path/to/core_graphrag.sqlite3 \
	<planning_request_id> \
	<candidate_plan_id> \
	<graph_hash> \
	--candidate-node-id runner_1 \
	--json
```

This helper does not replace the full end-to-end validation step above. It only checks that the persisted node explanation row already contains semantic and estimator signal once a real planning request has been recorded.

Fields to inspect:

- `semantic_familiarity`
- `semantic_relevant_capabilities`
- `semantic_related_entities`
- `semantic_dependencies`
- `semantic_plan_patterns`
- `semantic_fault_concepts`
- `semantic_community_summaries`
- per-node estimator outputs and RAPFS feature vector

Success criterion:

The returned node explanation contains non-empty or non-default semantic content for a request where retrieval should reasonably return context.

## Stage 2: Community Retrieval Inspection

Objective: inspect the community retrieval path.

Relevant implementation path:

- `core_graphrag.hpp`, especially the retrieval code that fills `community_summaries`
- `planning_plugin.hpp`, where `semantic_context_mode` selects detailed versus community prompt context

Question to answer:

Are community summaries being returned with enough signal to replace some detailed retrieved items?

Concrete check:

1. Inspect retrieval results for the same or comparable planning requests.
2. Compare `community_summaries` against detailed retrieved items such as relevant capabilities, related entities, dependencies, plan patterns, and fault concepts.
3. Check whether community summaries preserve the distinctions that matter for planning.

Evaluation focus:

- whether the summaries are specific enough,
- whether they preserve important dependencies or failure patterns,
- whether they collapse too much detail to support reliable prompt construction.

Success criterion:

Community summaries retain enough information to serve as a smaller semantic-context representation without obviously dropping key planning signal.

## Stage 3: Planner-Boundary Prompt Comparison

Objective: compare detailed versus community prompt size on real requests.

Relevant implementation path:

- `experience_server.yaml`
- `planning_plugin.hpp`

Configuration inputs:

- `PlanningPlugin.semantic_context_mode`
- `PlanningPlugin.log_semantic_context_comparison`

Concrete check:

1. Run real planning requests with comparison logging enabled.
2. Use the planner logging path to compare detailed and community prompt sizes.
3. Record whether community mode materially reduces prompt size.
4. Correlate the prompt-size reduction with the semantic content observed in Stage 2.

Success criterion:

Community mode shows a meaningful prompt-size reduction while still carrying the semantic signal needed for planning.

## Post-Stage Tuning

Tuning should only happen after Stage 1 through Stage 3 are executed against the completed implementation.

Tuning targets:

- retrieval composition and ranking,
- formatting and exposure of semantic explanation fields,
- use of community summaries versus detailed items in planner prompts,
- planner defaults for `semantic_context_mode`.

This tuning pass should be based on observed results, not on static inspection alone.

## Headless Offline Workflow

Use the offline workflow runner when you want the retrieval, prompt, and outcome checks to run against saved artifacts without robot connections:

```bash
ros2 run experience_inspector run_offline_functional_workflow.py \
	--db-path /path/to/core_graphrag.sqlite3 \
	--audit-root /path/to/planner_prompt_audit \
	--feedback-root /path/to/feedback_root \
	--out-dir /tmp/offline_functional_outputs
```

You can also attach one persisted explanation verification step for a specific selected plan or node:

```bash
ros2 run experience_inspector run_offline_functional_workflow.py \
	--db-path /path/to/core_graphrag.sqlite3 \
	--audit-root /path/to/planner_prompt_audit \
	--feedback-root /path/to/feedback_root \
	--out-dir /tmp/offline_functional_outputs \
	--planning-request-id pr_20260927_001 \
	--candidate-plan-id cp_001 \
	--graph-hash graph_001 \
	--candidate-node-id runner_1
```

Inputs:

- `core_graphrag.sqlite3` for persisted explanation and feedback rows
- `planner_prompt_audit` for detailed-vs-community prompt comparisons
- `feedback_root` containing per-execution `generated_plans.json` and optional `user_feedback.json`

Outputs:

- `retrieval_prompt_audit/retrieval_prompt_audit.json`
- `planner_prompt_audit_exports/planner_prompt_audit_summary.json`
- `reasoning_outcomes/reasoning_outcomes.json`
- `prompt_decision_support/prompt_decision_support.json`
- `terminal_success_analysis/terminal_success_analysis.json`
- `terminal_success_analysis/terminal_success_calibration_proposal.json`
- optional `explanation_verification.json` when one request/plan/hash tuple is supplied

This workflow remains offline and artifact-driven. Interface correctness should still be enforced by the focused automated tests in the package test targets, or by the focused check runner:

```bash
ros2 run experience_inspector run_focused_checks.py --workspace-root /home/ubuntu/colcon_ws
```

## Request-Level Decision Support

Use the prompt decision support export when you want a request-by-request recommendation for whether detailed or community semantic context is looking more appropriate on saved planning requests:

```bash
ros2 run experience_server export_prompt_decision_support.py \
	/path/to/planner_prompt_audit \
	/path/to/feedback_root \
	--db /path/to/core_graphrag.sqlite3 \
	--out-dir /tmp/prompt_decision_support
```

This export joins:

- prompt-audit iteration metrics,
- selected generated-plan snapshots and reasoning,
- optional persisted feedback rows,
- optional observed outcomes from `user_feedback.json`.

It does not make the Stage 5 decision for you automatically, but it gives a request-level `recommendation` field and the supporting prompt-reduction, community-signal, and selected-plan outcome fields needed for that review.

The selected-plan fields now keep prediction and observation separate: `selected_predicted_success_probability` captures the scorer output, `selected_predicted_terminal_success_label` captures the scorer's boolean plan-level prediction, and the calibration review export adds a threshold-derived `calibrated_terminal_success_label` with explicit threshold/version metadata for offline policy review.
