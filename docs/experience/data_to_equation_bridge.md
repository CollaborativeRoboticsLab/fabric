# Gathered Data To Equation Bridge

## The Big Picture

Planning needs a "stat sheet" for every capability:

$$
x_i = [p_i, t_i, e_i, f_i, r_i, u_i]
$$

(success probability, expected time, energy cost, fault burden, resource
demand, uncertainty — see
[mathematical_framing_simplified.md](mathematical_framing_simplified.md) for
what each term means.)

Those numbers have to come from somewhere. The division of labor is:

- The **supervisor** watches the running system and publishes raw observations
  (a `SystemStatus` message on the `system_status` topic, one entry per
  capability).
- The **experience** side subscribes, accumulates history, and computes the
  actual statistics.

Rule of thumb: **the supervisor reports facts; the experience layer does the
math.** The supervisor should never publish pre-computed planning scores,
because that would hard-code modeling decisions in the wrong component.

## What the Current Message Already Supports

Scorecard for the current `SystemStatus` message:

| Term | Support today | Why |
|---|---|---|
| $t_i$ time | Partial | Start/stop timestamps are published, so durations can be derived |
| $e_i$ energy | Good | Battery percentage deltas per run interval are published |
| $f_i$ faults | Good | Log records carry a severity label (warning/error), so they can be counted and weighted per severity |
| $p_i$ success | Good | `run_evidence` now counts starts, successes, failures, and abort-like terminations |
| $r_i$ resources | Weak | Only related-node names — presence of a node is not a CPU/memory measurement |
| $u_i$ uncertainty | None | No sample counts or variances are published; must be inferred |

So today the experience side can already compute run durations, per-run and
cumulative battery cost, fault rates, and a direct success-probability
estimate. It still cannot reliably compute resource demand or uncertainty from
the message alone.

## What Still Needs Care, in Plain Terms

1. **Success semantics ($p_i$).** The message now publishes counters for
   starts, completions, successes, failures, and abort-like terminations.
   The remaining caveat is meaning: the estimate is only as good as the event
   convention. It assumes `SUCCEEDED` really means "the run achieved its
   goal," `FAILED` really means failure, and `TERMINATED` really means
   cancelled or externally stopped.
2. **Resource measurements ($r_i$).** CPU, memory, network, storage, and
   actuator effort are simply not collected yet. Knowing which ROS nodes are
   related to a capability tells you *who* was involved, not *how much* they
   consumed.
3. **Uncertainty support ($u_i$).** To say "this estimate is based on 3 samples
   and is shaky" you need sample counts and variances. None are published, so
   the experience side can only build uncertainty from its own accumulated
   history.
4. **Text fields.** Fault descriptions, source names, and outcome labels are
   strings. Useful for clustering, retrieval, and explanations — but a string
   cannot be added to a numeric score without a transformation step
   (embeddings, categories, severity weights).

## The Message Shape Going Forward

Keep each per-capability entry centered on three evidence blocks:

### 1. Run evidence → enables $p_i$

Counters like `runs_started`, `runs_completed`, `runs_succeeded`,
`runs_failed`, `runs_aborted`, `runs_timed_out`, plus the last outcome label.

This block is now implemented. In the current supervisor semantics:

- `TRIGGERED` means a run started,
- `SUCCEEDED` means a successful completion,
- `FAILED` means a failed completion,
- `TERMINATED` means cancelled or externally stopped,
- `STARTED` means loading or preparing,
- `STOPPED` means unloading from memory,
- `DISCONNECTED` means cleanup,
- `UNDEFINED` means unknown.

With those, success probability is a simple ratio:

$$
\hat{p}_i = \frac{\text{runs succeeded}}{\max(1,\ \text{runs completed})}
$$

(The $\max(1,\cdot)$ just avoids dividing by zero before any run completes.)

The professional document also shows a **Beta-distribution** version. The idea
in one sentence: instead of a single ratio, keep a probability *distribution*
over $p_i$ that starts as a prior guess and sharpens as successes and failures
accumulate — this gives new capabilities honest wide error bars instead of
overconfident point estimates.

Separate counters for failed / aborted / timed-out runs let the experience
layer choose its own definition of "failure" later without changing the
supervisor.

### 2. Resource evidence → enables $r_i$

Actual measurements: CPU time and utilization, memory mean/peak, network and
disk byte counts, actuator effort. The experience side can then either keep
$r_i$ as a vector (one number per resource, needed for feasibility checks) or
collapse it into a single weighted scalar for ranking.

Caveat: this data does not exist yet — collecting it means adding resource
monitors on the supervisor side and correlating their output with which
capabilities were active.

### 3. Estimation evidence → enables $u_i$

For each measured quantity, publish three running numbers: the **count** of
samples, the **sum**, and the **sum of squares**. These are called *sufficient
statistics* because they are all you need to reconstruct the mean and variance:

$$
\mu = \frac{\sum x}{N},
\qquad
\sigma^2 = \frac{\sum x^2}{N} - \mu^2
$$

From mean and variance the experience layer builds confidence intervals, and
from interval widths it builds the statistical part of the uncertainty term
$u_i$. Three numbers per quantity buy the whole uncertainty machinery.

### Optional: per-run records

Instead of only running totals, the supervisor could publish one record per
completed run (start, stop, outcome, duration, energy delta, faults). More
message traffic, but maximum flexibility: the experience side could then
recompute anything with new definitions, use sliding windows, or do temporal
analyses. This is the "keep the raw logs" option.

## Recommended Path

1. Keep the current `SystemStatus` as the base snapshot.
2. Keep `CapabilityRunEvidence` as the current path for success tracking, and
   add resource and estimation evidence next.
3. Let the experience server accumulate history and compute all six terms
   itself.

Supervisor observes; experience infers. Each component stays replaceable.
