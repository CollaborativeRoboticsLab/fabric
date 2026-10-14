# Knowledge Representation for Robotic Experience

The system keeps two linked kinds of knowledge about robot capabilities. One is the **semantic graph**: what things are and how they relate. The other is the **statistical graph**: how those things actually performed.

The write path is summarized in [Data To Knowledge Store](data_to_knowledge.md).
The read path into scoring is summarized in [Knowledge Store To RAPFS](knowledge_to_rapfs.md).

The design rule is: **meaning and measurement stay separate.** A capability can be  semantically perfect for a task and still be slow, unreliable, poorly measured, or risky under the current context.

## 1. Semantic Graph — Meaning

The semantic graph stores capability nodes, task entities, context entities, plan patterns, fault concepts, and relationships between them. It is built from capability descriptions, declared capability relations, runtime parameter schemas, supervisor context, external documents, task text, logs, and prior plans.

It stores nodes such as:

- capabilities,
- task entities such as objectives, constraints, and parameters,
- context entities such as objects, people, places, and environment conditions, 
- plan patterns such as sequential chains, parallel branches, and recovery 	branches,
- fault concepts from warnings and errors.

It stores relationships such as:

- capability depends on another capability,
- capability interacts with a context entity,
- capability can do a task objective,
- capability often precedes another capability,
- capability can cause or help recover from a fault,
- capability is used as recovery for another capability,
- a plan pattern contains a capability or recovery path,
- one capability can satisfy another capability's runtime inputs through parameter flow.

The GraphRAG core clusters this graph into communities so retrieval can happen at node, edge, and community levels.

## 2. Statistical Graph — Evidence

The statistical graph stores runtime evidence and estimator state. It records, per capability and comparable context when possible:

- success and failure counts,
- runtime duration samples,
- battery or energy cost,
- warning and error rates,
- resource demand once resource telemetry exists,
- uncertainty evidence such as sparse samples and missing measurements,
- plan-level outcomes for calibration.

This graph is what eventually feeds the planning vector:

$$
x_i=[p_i,t_i,e_i,f_i,\\mathbf{r}_i,u_i].
$$

## How They Connect

The graphs stay separate but share stable IDs for capabilities and context buckets.

Planning uses them in order:

1. retrieve relevant capabilities from the semantic graph and vector index,
2. resolve them to stable capability and context IDs,
3. join those IDs to statistical evidence,
4. fall back to broader aggregates when specific evidence is sparse,
5. raise uncertainty when fallbacks or missing inputs are used,
6. pass success, time, energy, faults, resources, and uncertainty to RAPFS.

For selected plans, Experience also persists a queryable score-explanation artifact per node. That record is still row-oriented relational data, not an adjacency matrix: one row stores the selected evidence identity, fallback trace, estimator outputs, and RAPFS-aligned feature vector for one candidate node, while separate edge-attribution rows preserve the plan graph structure.

This separation matters. Semantic relevance is not reliability. A capability can match a task very well and still have poor runtime evidence.

## Current Gap

The current supervisor data supports success, time, energy, and fault estimates reasonably well. It does not yet include quantitative CPU, memory, network, storage, or actuator measurements. The scorer already exposes resource cost and feasibility hooks, but until that telemetry is added those terms are only weakly informed and missing resource data still increases uncertainty.
