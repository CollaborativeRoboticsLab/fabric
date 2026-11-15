# Plan Ranking and Selection

## A Plan Is a Graph, Not a List

A Fabric plan is modeled as a **directed graph** $G_\pi=(V_\pi,E_\pi)$:

- Each **node** ($V_\pi$) is one capability or runner instance — one "step".
- Each **edge** ($E_\pi$) is labeled with the event that triggers it: start,  success, failure, or stop. So an arrow can mean "when A succeeds, start B" or "if A fails, start the recovery step".

This matches how the Fabric parser already works internally: `sequential`, `parallel_all`, `parallel_any`, and `recovery` controls all compile down to a map of directed connections.

A purely sequential plan (do A, then B, then C) is just the special case where the graph is a straight chain — representable as a linked list. But as soon as a plan branches (parallel work, backup paths), a single linked list cannot hold it without losing the branching structure. That is why the general model is a graph.

## The Planner Produces Multiple Candidates

For each request the planner generates a finite set of candidate plans:

$$
\mathcal{P}=\{G_{\pi_1},G_{\pi_2},\ldots,G_{\pi_m}\}.
$$

The job of the experience layer is to pick the best one from this set.

## Each Step Gets a Feature Vector

Every capability instance $i$ is described by a small bundle of values:

$$
x_i = [p_i, t_i, e_i, f_i, \mathbf{r}_i, u_i]
$$

Think of it as the step's "stat sheet", learned from past executions:

| Symbol | Plain meaning |
|---|---|
| $p_i$ | How often has this step succeeded before? (a probability) |
| $t_i$ | How long does it usually take? |
| $e_i$ | How much battery/energy does it usually cost? |
| $f_i$ | How many faults does it tend to produce? |
| $\mathbf{r}_i$ | How much CPU, memory, bandwidth, storage, and actuator load does it need? (a vector — one number per resource type) |
| $u_i$ | How much should we doubt all the numbers above? (0 = confident, 1 = guessing) |

One distinction is worth memorizing because it appears everywhere in these documents:

- $1-p_i$ is **failure risk**: the step might genuinely fail, and we may know that very precisely.
- $u_i$ is **uncertainty**: we do not have enough evidence to trust our own estimates.

These are different problems with different fixes (retry/backup vs. gather more data), so they are kept as separate terms.

## The Selection Problem

Choose the best feasible plan:

$$
G_{\pi^*}=\arg\max_{G_\pi\in\mathcal{P}\cap\mathcal{F}}
\operatorname{RAPFS}(G_\pi)
$$

Out of all candidates ($\mathcal{P}$) that pass the feasibility
checks ($\mathcal{F}$), pick the one with the highest
[RAPFS](risk_aware_plan_score.md) score.

The feasibility checks are budgets and thresholds:

- total expected time must fit in the time available,
- total expected energy must fit in the battery available,
- predicted success probability must reach a minimum bar $\tau$,
- peak simultaneous resource use (checked per resource type, using a pessimistic percentile) must stay within the robot's limits.

One subtlety: resources that are *occupied and released* (CPU, memory) are checked by their peak concurrent use, while resources that are *consumed permanently* (transferred bytes, storage growth) need separate cumulative budgets — a peak check would miss slow accumulation.

> **Status note:** the resource numbers $\mathbf{r}_i$ are not measured yet. The current scorer already has a resource term and a resource-feasibility threshold in its config, but those values are only weakly informed until the ROS measurement tool exists. Today the lack of resource data mainly shows up as higher $u_i$, while the resource term remains a placeholder for future telemetry.
