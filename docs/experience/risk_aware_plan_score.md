# Risk-Aware Plan Feasibility Score (RAPFS)

## What Problem Does RAPFS Solve?

When the robot is asked to do a task, the planner does not produce one plan; it produces several candidate plans. Each plan is a flowchart of robot skills (capabilities): arrows say "when this step succeeds, start that step" or "if this step fails, run the backup step". Some plans are simple chains (do A, then B, then C), some run steps in parallel, and some include  recovery branches.

RAPFS gives each candidate plan a single score so the system can automatically pick the best one. That is the whole job: **turn a plan into one comparable score.**

Formally a plan is written $G_\pi=(V_\pi,E_\pi)$, which just means "a graph with nodes $V_\pi$ (the skills) and arrows $E_\pi$ (the event connections)".

## RAPFS Definition

$$
\operatorname{RAPFS}(G_\pi)
= w_s S(G_\pi)
- w_t \hat{T}(G_\pi)
- w_e \hat{E}(G_\pi)
- w_f \hat{F}(G_\pi)
- w_r \hat{C}_r(G_\pi)
- w_u \hat{U}(G_\pi)
$$

Read this as: **reward minus penalties.**

- Start with a reward for how likely the plan is to succeed ($S$).
- Subtract penalties for everything undesirable: expected duration ($\hat T$), battery drain ($\hat E$), faults ($\hat F$), resource load ($\hat C_r$), and "how unsure we are about our own estimates" ($\hat U$).

Two conventions make the formula work:

1. **The values are normalized** means "rescaled to 0–1". Raw costs come in different units (seconds, percent battery, fault counts). Before they can be added together, each is squashed onto a common 0-to-1 scale, similar to converting raw exam marks into percentiles.
2. **The weights $w_k$ express priorities.** A large $w_t$ means "speed matters most"; a large $w_e$ means "save the battery". Making the weights sum to 1 keeps the score on a predictable scale, like a weighted average grade.

## Why Costs Depend on the Execution Path

The same plan can play out differently every time it runs:

- A `recovery` (backup) branch may or may not fire.
- A `parallel_any` block is a race: it stops as soon as one branch succeeds.
- A `parallel_all` block normally runs everything, but an early failure can abort the remaining branches.

Each possible "way the run unfolds" is called a **scenario**, written $\omega$. To cost a plan fairly, we imagine all the ways it could unfold, cost each one,
and average them weighted by how likely each is (that is what the expected value $\mathbb{E}_\omega[\cdot]$ notation means). A recovery branch that triggers only 5% of the time therefore contributes only about 5% of its cost.

With that, the three main cost formulas answer simple questions about an average run:

- **Time $T$**: how long does the whole plan take from first start to last finish? This is wall-clock time, not the sum of step times, because parallel steps overlap. (In the formula, $\max_i d_i^\omega - \min_i s_i^\omega$ is   literally "latest finish minus earliest start".)

- **Energy $E$ and faults $F$**: add up the energy and fault burden of the steps that actually ran in each scenario, then average across scenarios.

- **Peak resource demand $\mathbf{R}_{\mathrm{peak}}^\omega$**: a different kind of question — at the busiest single instant, how much CPU, memory, bandwidth, etc. is in use *at the same time*? This matters because a robot can run out of a resource momentarily even when total usage looks fine. It is a vector (bold $\mathbf{R}$) because each resource type is tracked separately.

### Normalization must be frozen

Each raw cost is passed through a fixed "conversion rule" $g_K$ that maps it to a 0-to-1 value. The rule must be decided once and then frozen for the whole experiment. If the conversion changed between experiments, the same plan could get different scores, and the weights would silently mean different things. (Controlled changes between experiments are allowed — see the weight-adaptation section below.)

## Predicting Plan Success

$S(G_\pi)$ answers: "if we run this plan now, in this context, what is the chance it finishes successfully?" The context $z$ includes the task, capability/context IDs, the environment, and the robot state.

Each step gets its own success probability, conditioned on what happened before it — the notation $Y_{\operatorname{pa}(i)}$ means "the outcomes of the steps feeding into step $i$" (pa = parents in the graph).

An important pitfall: **you cannot always multiply step probabilities together.**

- For a simple chain, multiplying works: every step must succeed, so   $S \approx p_1 \cdot p_2 \cdots p_n$.

- For branching plans it is wrong. A `parallel_any` plan succeeds if *any*  branch works. A recovery plan succeeds even when the primary step fails, provided the backup works. Multiplying everything would badly underestimate those plans.

When the exact math is too tangled, run the plan many times in simulation and count how often it succeeds (**Monte Carlo simulation**).

> **Status note:** the current implementation already scores the original Fabric XML tree for `sequential`, `parallel_all`, `parallel_any`, and `recovery`, but it still uses baseline independence approximations for branching behavior. Exact inference or Monte Carlo evaluation remains future work.

Finally, $S$ must be **calibrated**: the number must be honest. Among plans scored 0.8, roughly 80% should actually succeed when executed. Calibration is checked against real recorded outcomes.

## The Optional Risk Term (Consistency vs. Average)

Two plans can have the same *average* outcome while one is steady and the other is erratic — sometimes brilliant, sometimes terrible. The risk-sensitive extension penalizes the erratic one.

$Y(G_\pi)$ is the score a single real run would actually earn (success bonus minus observed time, energy, faults, and resource cost). Its **variance** measures the run-to-run spread. Subtracting $\eta \cdot \operatorname{Var}(Y)$ says "prefer the dependable plan", where $\eta$ controls how cautious to be.

Do not confuse this with the uncertainty term $\hat U$:

- **Variance of $Y$** = outcomes genuinely vary between runs. 

- **$\hat U$** = we do not know enough to trust our estimates.

A fair coin has high outcome variability but zero mystery. A coin we have never flipped is the opposite.

## Uncertainty: "How Much Should We Doubt Our Own Numbers?"

Every capability instance $i$ gets an uncertainty score $u_i \in [0,1]$ built from four separately measured flavors:

| Component | Question it answers | Example |
|---|---|---|
| $u_i^{\mathrm{stat}}$ | Do we have enough data? | Only 3 recorded runs of this skill |
| $u_i^{\mathrm{amb}}$ | Is the task itself ambiguous? | "Put it away" — where is *away*? |
| $u_i^{\mathrm{unfam}}$ | Have we seen a situation like this before? | New room, new object, new provider |
| $u_i^{\mathrm{miss}}$ | Are inputs we rely on missing? | No battery telemetry available |

Each flavor is scored 0 to 1 and blended with weights
($\alpha,\beta,\gamma,\delta$) that sum to 1 — a weighted average.

### Avoiding double-counting

A step that fails 30% of the time is *risky*, not necessarily *uncertain* — we may know its 30% failure rate very precisely. Failure risk is already handled
by $p_i$ and $S$, so it is **not** inserted again into $u_i$ as $1-p_i$. Likewise, run-to-run variability lives only in the optional variance term. Each kind of "badness" is counted exactly once.

### How the flavors are measured

- **Statistical uncertainty** uses confidence-interval widths. A confidence interval says "the true value is probably between here and here". Few or noisy samples make that band wide; many consistent samples make it narrow.   Wide bands mean we are guessing, narrow bands mean we know.

- **Ambiguity** can be estimated from disagreement between valid interpretations of the task.

- **Unfamiliarity** looks up the $k$ most similar past situations in the vector/graph database and turns that familiarity into an uncertainty penalty through a versioned transform. If even the closest matches are poor, we are in unfamiliar territory. The raw similarity is not used directly because "0.7 cosine similarity" is just a geometric number — it does not automatically mean "70% familiar".

- **Missing evidence** is simply the fraction of expected inputs that are missing or unusable: $m_i / M$.

### Rolling step uncertainty up to the plan

Once every step has an uncertainty score, the plan needs one overall number. Two sensible choices:

- **Weighted average** ($\hat U_{\mathrm{mean}}$): weight each step by how likely it is to actually run — a rarely used backup step should not count as much. Reflects overall evidence quality.

- **Weakest link** ($\hat U_{\mathrm{max}}$): the single most uncertain step that could run. The cautious option — nine well-understood steps cannot compensate for one total mystery.

Pick one rule *before* running the evaluation, not after.

## Should the Weights Change Over Time?

It is tempting to let the priorities $w_k$ drift automatically as the robot gathers experience. The policy here is: **usually no — new data should improve the estimates, not the priorities.**

The distinction:

- **Estimates** (success chances, durations, energy, uncertainty) are facts about the world. More data makes them sharper, and they already update continuously.
- **Weights** are preferences — "how many minutes is 5% extra success worth?" A thousand more executions cannot answer that question; it is a mission-level choice.

There are only a few honest reasons to change weights:

1. **The initial guess was wrong.** If logs show the scorer keeps preferring plans that turn out badly, the weights are being *corrected*, not adapted. Corrections should shrink over time — weights that never settle signal a modeling problem, not healthy learning.
2. **The trade-off itself genuinely changed.** An aging battery really does make energy more precious. But that is better expressed explicitly (a smaller energy budget) than by letting the weights wander.
3. **The 0-to-1 conversion rules went stale.** Then fix the conversion rules at a declared checkpoint — do not paper over it with weight drift.

To keep everything auditable, the system updates in **layers, from fast to slow**:

| Layer | Updates | How often |
|---|---|---|
| Estimates (success, time, energy, uncertainty) | Every execution | Continuously |
| Conversion rules ($g_K$) | At declared checkpoints, versioned | Slowly |
| Weights ($w_k$) | Only between evaluation epochs, versioned | Slowest |
| Budgets and thresholds | When the mission context changes | Event-driven |

House rules for the weight layer:

- **Freeze within an epoch.** Scores computed under different weights are not comparable — like comparing exam grades from two different marking schemes. Log which weight version made every decision, and evaluate each epoch separately.
- **One layer at a time.** Never change the conversion rules and the weights at the same checkpoint; if both move, nobody can tell which change caused what.
- **Beware self-confirming loops.** The weights choose which plans run, and only executed plans produce data. Without occasionally trying a non-top-ranked plan, the system can lock into its own bias (e.g., never running slow plans, so never learning that slow plans succeed more).
- **The uncertainty weight is special.** Real outcomes contain no "uncertainty" measurement, so fitting $w_u$ from outcomes silently drives it to zero. Keep it as a fixed policy choice, or tune it against events like operator interventions.

**How the adaptive-weights ablation works.** Instead of one weight vector, keep a *belief* over plausible weight vectors — "the time weight is probably around 0.2, give or take". After each epoch, update that belief in batch from recorded comparisons ("plan A actually beat plan B on this request"). Rank with the belief's average weights, and log them as that epoch's version. For a small declared fraction of requests, rank with a *random draw* from the belief instead: when the system is still unsure about the weights, the draws vary more and it naturally tries different trade-offs — which is exactly the escape hatch the self-confirming-loop rule demands. The belief should also tighten over time; if it keeps widening or wobbling, something in the model is wrong.

For the first experiment, fixed weights are the baseline; epoch-by-epoch updates may appear as an ablation. Fully self-adjusting weights are excluded because they make results impossible to compare across time. The current scorer already loads versioned piecewise-linear transforms from config and records the active normalizer, estimator, and weight versions with each selected plan explanation.

## Hard Feasibility: Pass/Fail Before Ranking

Before ranking plans by preference, apply pass/fail checks:

- Does the plan fit in the available time and battery?

- Is its success chance at least the required minimum $\tau$?

- Does its busiest moment stay within the robot's resource limits? (This uses a pessimistic percentile $\mathbf{Q}_q$ of the peak demand, not the average, so occasional spikes are respected.)

> **Status note:** resource usage is not measured yet. The scorer already exposes a resource-limit check and a resource cost term, but until quantitative telemetry exists those values are only weakly informed and missing data still shows up mainly as higher uncertainty.

Plans that fail any check get a score of $-\infty$, meaning "never choose this, no matter what". Scoring them 0 instead would be a bug: a feasible-but-costly plan can legitimately score below 0, and would then lose to a plan that cannot
run at all.

## Soft Constraints: Graded Fines Instead of Pass/Fail

The soft version replaces the pass/fail gate with graded fines. The bracket $[z]_+ = \max(0, z)$ means "only count the overshoot":

- A plan under budget pays nothing.

- A plan 10% over its time budget pays a fine proportional to that 10%.

This is useful for optimizers that need a smooth signal ("you're close, adjust a little") instead of a cliff edge. Each resource dimension keeps its own separate fine, so overusing memory cannot be traded away against underusing
bandwidth.

## Putting It All Together

The scoring system has three layers:

1. **Feasibility layer** — reject plans that cannot possibly run within the budgets (hard checks above).

2. **Expected utility layer** — rank the survivors with the RAPFS formula: reward success, penalize cost and uncertainty.

3. **Optional risk layer** — additionally penalize plans whose outcomes swing wildly between runs.

For each planning request: generate candidates, discard the infeasible ones, rank the rest by RAPFS, execute the winner. All conversion rules and weights are fixed on training data beforehand, and every score component is reported separately so results stay interpretable.
