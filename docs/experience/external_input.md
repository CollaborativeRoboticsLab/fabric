# External Input And Plan Feedback

External input is useful information that does **not** come from the Fabric
runtime telemetry stream. It can include the user's task request, environment reports,
retrieved documents, operator judgement, or a label after execution saying
whether the whole plan actually worked.

This document focuses on plan-success feedback: how the experience layer should
use an outside judgement that a completed plan succeeded, partly succeeded, or
failed.

## What Feedback Means

Feedback is evidence about the **whole plan**, not just one capability.

For example, every capability may report no local `FAILED` event, but the task
can still fail if the robot delivered the object to the wrong place. The reverse
can also happen: one capability can fail, a recovery branch can run, and the
overall plan can still succeed.

That is why external feedback should be stored as plan-level evidence first.
Only attach it to a specific capability when the cause is clear.

## What To Store

For each executed plan $G_\pi$, keep enough information to explain what happened
later:

- planning request ID and candidate plan ID,
- graph structure hash,
- task and context IDs,
- score components used during ranking,
- estimator, normalizer, and weight versions,
- final outcome: success, partial success, or failure,
- optional human or external-system judgement.

The version fields matter because a future score may use different estimators,
normalizers, or weights. Without versions, old feedback becomes hard to
interpret.

## How Feedback Improves Estimates

The first job of feedback is calibration. It checks whether the predicted plan
success $S(G_\pi)$ matched reality.

Feedback can update the normal RAPFS terms:

- success estimates use success and failure labels,
- time estimates use observed completion time,
- energy estimates use observed battery or energy deltas,
- fault estimates use warnings, errors, and external fault reports,
- uncertainty estimates use prediction mistakes, missing evidence, and
	unfamiliar contexts.

These updates make the score inputs more accurate. They do not automatically
change the weights.

## When Weights May Change

Weights are preferences. They answer questions like: how much extra time is
worth a higher chance of success?

Because weights are preferences, they should not change after every feedback
item. If weights moved constantly, scores from different decisions would stop
being comparable.

Weight updates are allowed only at evaluation epoch boundaries. At that point,
many feedback records can be used together to ask whether the scorer repeatedly
preferred the wrong trade-off.

Simple example:

- Plan $G_a$ was slower but safer.
- Plan $G_b$ was faster but more uncertain.
- The scorer chose $G_b$.
- External feedback says $G_b$ failed.

One failure should not rewrite the scorer. But if many similar cases show the
same pattern, the next weight epoch may reduce the influence of speed or
increase the influence of success and uncertainty.

## Selection Bias

Most feedback comes from plans the current scorer chose to execute. That means
the data is biased toward plans the system already liked.

If adaptive weights are tested, the experiment should sometimes execute a
non-top-ranked feasible plan, or use an importance-correction method. Otherwise
the system may never learn what would have happened under alternatives.

The uncertainty weight $w_u$ needs special care. A success/failure label does
not directly measure uncertainty, so fitting $w_u$ only from task success tends
to push it toward zero. Keep $w_u$ fixed as a policy choice, or tune it against
a separate target such as operator intervention, overrides, aborts, or interval
coverage.

## Practical Rule

External plan feedback has two jobs:

1. continuously improve component estimates and success calibration,
2. slowly adjust RAPFS weights only at versioned epoch boundaries, when repeated
	 evidence shows the ranking trade-off is wrong.

In short: feedback calibrates first, and changes preferences only slowly.
