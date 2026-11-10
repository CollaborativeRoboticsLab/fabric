# Fabric Inspector: Planned Work

## Implementation Progress

- Phase 1 remains in progress. Open runtime and inspector work is listed below.
- The snapshot does not report confirmed connections or running-capability dependencies. Its timestamp is the last state capture, not a live health check; terminal states can be cleared before a client observes them. Instance entries are derived from valid connection endpoints, not an independent inventory.
- The existing Graph Overview is the Experience knowledge graph, and the Plan Sandbox is not the requested Fabric graph or draft editor.
- Existing database and proposal workflows are intended to remain unchanged; their regression tests and standalone/rqt integration have not yet been verified for this work.

## Goal And Scope

Keep the Experience database inspection and non-destructive proposal workflows intact. Extend the inspector to visualize a loaded plan and a live active Fabric, then add a separate draft editor and explicit plan submission. The graph is not the Experience knowledge graph: its nodes represent capabilities or capability instances, and its directed event edges represent execution connections. Dependency edges are a separate relationship.

Database mutation is not part of this work. Existing proposal exports remain non-destructive; any direct Experience database augmentation requires a separately agreed write API, validation, and safety policy.

## Current State

- The Plan Sandbox loads XML and displays plan/control/runner structure; it is not an authoritative live capability graph.
- `/fabric/plan/parse` returns parsed Fabric connections for supplied XML. `/fabric/plan/get_status` returns lifecycle status; `/fabric/plan/get_snapshot` returns the cached intended graph for the current dequeued plan.
- capabilities2 exposes running capabilities and dependency information. Its current running-capability response does not populate instance IDs, so it cannot reliably assign dependencies to specific plan instances. The `dependent_capabilities` field is populated from the running model's dependencies; preserve the actual direction and verify it in tests.
- `/fabric/plan/submit` accepts plan XML and returns an accepted plan ID or an error. Acceptance is not proof of successful execution.

## Phase 1: Remaining Runtime Work

- Extend runtime snapshot coverage to `on_start`, `on_stop`, and `on_failure`, and verify partial/connecting/shutdown transitions. The active fixture now checks repeated capability instances and directed `on_success` wiring across `RUNNING`, `COMPLETED`, and `CANCELLED`; manufactured snapshots alone do not establish runtime transition behavior.
- Test service disappearance and recovery, error paths, and bounded responsiveness while execution is held in a controlled transition. Verify snapshot reads neither dispatch nor mutate execution. Exercise the service with controlled Fabric/capabilities2, then with the inspector clients when available.
- Preserve the single-current-plan contract: event links are intended, not confirmed; capture time is not a health check; terminal snapshots are not history. Surface dependencies only with verified direction and identity, otherwise use provider-level relationships with unknown instance ownership. Coordinate any contract change with inspector clients.

**Acceptance:** Remaining failure and concurrent-transition cases are demonstrated with deterministic tests. Reads remain internally consistent and do not dispatch, mutate, or wait indefinitely on execution. Do not mark Phase 1 complete from conversion or store tests alone.

## Luna: Next-Part Guidelines

- During blocked parsing, connecting, and shutdown/failure scenarios, issue bounded snapshot requests and verify consistent plan ID, lifecycle, connection completeness, instances, and event direction. Test no unexpected dispatch or state changes, and confirm service reconnection after a controlled restart. Keep fixtures isolated and clean up processes and test data.
- Run focused message/server tests and the opt-in component matrix in a sourced workspace; rebuild dynamically loaded consumers after plugin ABI changes. Run inspector regressions and standalone/rqt integration when their clients exist. Report commands, actual results, skipped or blocked checks, and package-wide failures separately; avoid reformatting unrelated changes or claiming the existing formatter failures are all unrelated to this work.

## Phase 2: Read-Only Visualization

Add a dedicated Fabric Graph view alongside the existing database and sandbox tabs. Provide a loaded-plan source using the parser service and a live source using the snapshot service. Live refresh must not block the Qt event loop; show the source, last successful refresh, unavailable service, stale snapshot, and partial or empty graph states.

Provide three selectable levels:

1. **Main capabilities:** aggregate plan instances by capability interface and provider, preserving and labeling event connections between groups.
2. **All instances:** show each parsed capability instance independently, with directed event labels.
3. **Instances and dependencies:** retain instance/event edges and add provider-level dependencies. Do not assign dependencies to an instance unless the source proves that identity.

Give users node and edge details, selection, zoom/pan, filtering, and a legend distinguishing event, dependency, and uncertain relationships. Preserve self-loops and parallel events when aggregation combines nodes. Use stable identities and a graph model independent of Qt rendering so the editor can reuse it.

**Acceptance:** Focused model and Qt tests verify each level, graph direction, aggregation, duplicate instances, dependency ambiguity, refresh failures, and source switching. Existing database browsing and sandbox tests continue to pass.

## Phase 3: Draft Editor And Validation

Introduce an isolated draft model; editing it must never alter a live plan. Support discovery of available interfaces and providers, adding/removing capability instances, setting parameters, and creating/removing event edges. Provide load/save and a reviewable XML representation. Determine which graph structures can be represented by the Fabric parser and preserve supported XML semantics; reject or clearly mark unsupported structures instead of silently losing data. Do not claim arbitrary graphs round-trip until parser and serialization tests prove it.

Validate identity, required fields, parameters, event endpoints, and parser-supported structure locally. Use the existing parser and appropriate Fabric validation path to check the exact serialized draft; parsing alone is not full runtime validation. If no non-dispatching validation API exists, define and implement one or explicitly report the limitation before enabling dispatch. Invalidate validation when the draft changes.

**Acceptance:** Tests cover round trips, unsupported XML, invalid or missing providers and parameters, cyclic or otherwise unsupported structures, edit-after-validation, and the absence of side effects while editing or validating.

## Phase 4: Explicit Submission

Submit only a validated, unchanged draft after a user reviews the exact XML and confirms the action. Call `/fabric/plan/submit` and show the returned ID or rejection; monitor lifecycle separately using Fabric status. Make it clear that accepted means queued, not running or completed. Handle disconnection, server rejection, and timeout without automatically retrying an ambiguous request, since it may already have been accepted. Keep draft and live views separate.

**Acceptance:** Tests verify confirmation, exact payload, validation freshness, accepted/rejected responses, timeout handling, lifecycle updates, and prevention of accidental or duplicate submissions. No edit, refresh, validation, or view change triggers dispatch.

## Integration And Open Decisions

- Test the runtime snapshot and inspector clients in isolation, then exercise the complete flow with a controlled Fabric/capabilities2 setup. Cover standalone and rqt operation and regression-test existing inspector workflows.
- Document service names, ROS dependencies, configuration, graph semantics, identity limitations, refresh behavior, and submission risks. Build and test affected message, server, and inspector packages in a sourced workspace.
- Decide the inspector refresh policy. The existing snapshot service is `/fabric/plan/get_snapshot`; capabilities2's bounded event history cannot alone confirm current connection state. Revisit confirmation only if a reliable current-state source becomes available.
- Confirm whether capability aggregation by interface and provider matches the desired meaning of "main capabilities"; retain provider distinctions to avoid misleading merges.
- Determine the supported editor graph/XML subset and validation API before committing to arbitrary graph construction or dispatch.
