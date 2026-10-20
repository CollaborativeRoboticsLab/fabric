# TO-DO

## Current Integration Findings

Goal: finish folding the Experience runtime into Fabric without keeping brittle Fabric/Supervisor/Experience synchronization boundaries.

Current code inspection already shows the main breakpoints:

- `fabric_server/CMakeLists.txt` has been rewritten into one coherent package definition and `fabric_server` now compiles/tests again; remaining package work is now narrower dependency auditing rather than foundation repair.
- The copied Experience Python package and scripts have already been migrated substantially toward Fabric-owned imports and `/fabric/experience/...` endpoints; direct smoke/wrapper pytest coverage passes, and the package flake8/uncrustify gates are now clean under the intended local flake8 config.
- `fabric_server/include/fabric_server/server.hpp` still owns the high-level Fabric and Experience runtime, plugin loading, plan generation, scoring, and feedback persistence, but several support-heavy slices have already been extracted into helper headers under `include/fabric_server/utils/`, and the integrated generation path has been narrowed behind smaller private helpers.
- `fabric_server/launch/fabric.launch.py` is now the single Fabric-owned launch entry point; `experience_server.launch.py` and the external readiness shim have been removed, Supervisor is launched directly from Fabric launch, and Experience enablement now comes from merged `fabric.yaml` parameters.
- Fabric runtime feedback now uses Fabric-owned `status_structs.hpp` `SystemStatus` snapshots; remaining work is focused on parity and ownership cleanup rather than Supervisor message replacement.
- Fabric telemetry aggregation has been pulled out of `server.hpp` into `include/fabric_server/utils/telemetry.hpp`; execution-status merge, log/diagnostic correlation, and per-capability battery intervals now live behind a Fabric-owned helper.
- Terminal feedback persistence now derives terminal outcome/success from Fabric capability run evidence, so failed, timed-out, and aborted runs are no longer collapsed into a generic cancelled outcome.
- `capabilities2` already exposes the authoritative capability event stream and snapshot service (`/capabilities/get_event_snapshot`), so moving raw capability-run monitoring ownership toward `capabilities2` is viable.
- `fabric_base/include/fabric_base/utils/status_structs.hpp` already provides a Fabric-owned status model close to the Supervisor aggregation shape, so the migration can port logic onto an existing Fabric type surface instead of inventing a new schema from scratch.
- `fabric_experience_inspector` has unrelated in-progress changes migrating imports/dependencies from `experience_server`/`experience_msgs` to `fabric_server`/`fabric_msgs`; its live scoring endpoint has been aligned with the Fabric server's active `/fabric/experience/score` service.

Validated current state:

- `colcon build --packages-up-to fabric_server` and `colcon test --packages-select fabric_server` are passing again after the packaging repair, lint/test cleanup, and current `server.hpp` decomposition slices.
- Latest focused validation after telemetry/helper extraction: `colcon build --packages-select fabric_server`, `colcon test --packages-select fabric_server`, and `colcon test-result --test-result-base build/fabric_server --verbose` pass with 82 tests, 0 failures, 13 skipped.
- Score explanation, scoring epoch, plan feedback, feedback persistence, planning helpers, plan-status shaping, and the first integrated generation/selection helper slices have already been pulled out of the hottest parts of `server.hpp`.


## Stage 5: Fabric Server Foundation Repair

Goal: make `fabric_server` build and package cleanly as the primary integrated runtime before doing larger ownership moves.

### Build And Packaging

- [x] Rewrite `CMakeLists.txt` into one coherent package definition.
- [x] Keep exactly one `ament_package()` call.
- [x] Define a single clear build story for the Fabric executable/component and the integrated Experience support code.
- [x] Replace stale `experience_*` package references with the actual Fabric-side packages now present in this repo (`fabric_experience_*`, `fabric_validator`, `fabric_parser_xml`, etc.) where appropriate.
- [x] Install the Python package and scripts only once from the unified package definition.
- [x] Fix test target dependencies and header include paths that still reference `experience_server`.
- [x] Update `package.xml` to match the real integrated dependency graph.

### Python Utilities Migrated From Experience

- [x] Update `fabric_server/__init__.py` to export Fabric-owned modules instead of importing from `experience_server`.
- [x] Update copied Python modules to use intra-package `fabric_server` imports.
- [x] Update ROS service names in copied scripts to Fabric-owned endpoints under `/fabric/experience/...`.
- [x] Audit command-line help text, package names, and output text so they refer to Fabric rather than Experience where Fabric is now the runtime owner.
- [x] Fix Python tests that still assume the old Experience package/module layout.

### Immediate Validation

- [x] Keep running `colcon build --packages-select fabric_server` after each packaging slice until the package compiles.
- [x] Add a focused smoke check for the copied Python entry points after imports and service names are updated.

Evidence: `colcon build --packages-up-to fabric_server` and `colcon test --packages-select fabric_server` are currently passing after the unified `CMakeLists.txt` repair, package manifest/schema fixes, flake8/uncrustify cleanup, and the latest `server.hpp` extraction slices. Direct pytest coverage for the copied wrapper/entry-point scripts is also passing.

Likely local surface:

- `fabric_server/CMakeLists.txt`
- `fabric_server/package.xml`
- `fabric_server/fabric_server/__init__.py`
- `fabric_server/fabric_server/*.py`
- `fabric_server/scripts/*.py`
- `fabric_server/test/*`


## Stage 6: Fabric Server Decomposition

Goal: keep `fabric_server` responsible for ROS 2 interfaces, main processing threads, plugin loading, and high-level orchestration only.

### Runtime Boundary Cleanup

- [ ] Refactor `include/fabric_server/server.hpp` so the `Fabric` node owns only:
	- ROS 2 service/action/subscription setup
	- worker-thread lifecycle
	- plugin loading and initialization
	- high-level plan execution orchestration
- [x] Move pure support code into helper headers under `include/fabric_server/utils/`.
- [x] Prefer extracting support logic by concern: planning helpers, feedback persistence helpers, status/telemetry adapters, file/path helpers, and explanation/score adapters.
- [x] Remove legacy Experience naming inside the Fabric runtime where the code is now Fabric-owned.
- [x] Reduce cross-cutting direct access to shared state by giving each worker flow a narrower helper surface.

Completed decomposition slices so far:

- [x] Extract feedback persistence support into `include/fabric_server/utils/feedback.hpp`.
- [x] Extract planning and score-only candidate helpers into `include/fabric_server/utils/planning.hpp`.
- [x] Extract score explanation and scoring-epoch/plan-feedback service shaping into `include/fabric_server/utils/score_services.hpp` and `include/fabric_server/utils/score_explanation_adapter.hpp`.
- [x] Extract Supervisor-status conversion and plan-status shaping helpers into `include/fabric_server/utils/status.hpp`.
- [x] Split `initialize()` in `server.hpp` into smaller setup methods while keeping the top-level orchestration in `server.hpp`.
- [x] Split plan-generation support into smaller private helper methods while keeping `executePlanGeneration()` as the orchestration entry point.
- [x] Split integrated planning/scoring/persistence selection support into smaller private helper methods while keeping `handleIntegratedPlanGeneration()` as the orchestration entry point.
- [x] Split integrated result-completion support into a private helper so `handleIntegratedPlanGeneration()` no longer assembles the ROS action result inline.
- [x] Rename Fabric-owned integrated runtime setup/generation helpers away from legacy external Experience-server phrasing inside `server.hpp`.
- [x] Narrow queue/completion shared-state access behind helper methods in `server.hpp` instead of open-coding plan-queue and completion mutations across callbacks and worker flows.
- [x] Extract telemetry aggregation, execution-status merge, log/diagnostic correlation, and battery interval bookkeeping into `include/fabric_server/utils/telemetry.hpp`.
- [x] Extract Fabric Experience runtime parameter-name and persisted-core helpers into `include/fabric_server/utils/experience_runtime.hpp`.

### Experience Runtime Isolation Inside Fabric

- [ ] Isolate Experience-oriented services and persistence flows from the main plan-dispatch loop.
- [ ] Keep plan parsing/validation/dispatch functional while decomposition happens.
- [ ] Make the refactor behavior-preserving first; do not combine this slice with policy changes unless required for correctness.

Likely local surface:

- `fabric_server/include/fabric_server/server.hpp`
- `fabric_server/include/fabric_server/utils/*.hpp`
- `fabric_server/src/server_component.cpp`
- `fabric_server/src/server_node.cpp`


## Stage 7: Launch And Config Convergence

Goal: make Fabric the single launch/config entry point for the integrated stack.

### Launch Consolidation

- [x] Merge the intent of `experience_server.launch.py` into `fabric.launch.py`.
- [x] Remove the need for `fabric.launch.py` to include `experience_server.launch.py`.
- [x] Replace launch arguments that still treat Experience as a separate stack with Fabric-owned toggles for optional subsystems.
- [x] Decide whether a compatibility launch file should remain temporarily as a thin wrapper that delegates to Fabric launch.
- [x] Remove the remaining Supervisor launch inclusion from `fabric.launch.py`; Fabric launch now starts only Fabric-owned runtime dependencies.

### Configuration Consolidation

- [x] Merge `config/experience_server.yaml` into Fabric-owned configuration layout.
- [x] Keep Fabric defaults in `config/fabric.yaml` or a small set of Fabric-owned config files, not separate Experience-owned runtime configs.
- [x] Normalize plugin class names, parameter namespaces, and default model/scoring/refinement settings under the Fabric package.
- [x] Remove package-share lookups that still point at `experience_server` when Fabric now ships the runtime.
- [x] Port retained Supervisor runtime config into `fabric.yaml` for the merged stack where still needed, specifically battery monitoring.

Likely local surface:

- `fabric_server/launch/fabric.launch.py`
- `fabric_server/config/fabric.yaml`


## Stage 8: Monitoring Ownership Migration

Goal: move telemetry collection closer to the real owners while replacing Supervisor coupling with Fabric-native monitoring interfaces.

### 8A. Capability Monitoring Ownership

- [x] Move raw capability lifecycle/run evidence ownership to `capabilities2` where appropriate.
- [x] Treat `capabilities2` as the source of truth for capability event publication and event snapshot access.
- [x] Extend `fabric_server/include/fabric_server/capability_client.hpp` with any missing snapshot/query helpers Fabric needs.
- [x] Replace Fabric’s dependency on Supervisor capability summaries with Fabric-side processing of `capabilities2` event data.
- [ ] Keep high-level cross-source analytics out of `capabilities2`; only move capability facts and event history there.

Concrete ownership split for the current Supervisor capability monitor:

- [x] Do **not** move `supervisor::CapabilityMonitor` wholesale into `capabilities2`; split its responsibilities first.
- [x] Keep only authoritative capability facts in `capabilities2`: event publication, bounded event history, snapshot/query services, and any small fact-level helpers Fabric needs.
- [x] Move capability-event consumption, plan/instance correlation, lifecycle shaping, run-evidence accumulation, and terminal-outcome reconstruction into Fabric.
- [ ] Keep `capabilities2` unaware of Fabric policy, Experience persistence, log correlation, or stop-safety decisions.

### 8B. Fabric Monitoring Ownership

- [x] Move Fabric plan execution monitoring to Fabric itself instead of polling it back through Supervisor.
- [x] Replace the old Supervisor `FabricMonitor` polling contract with a Fabric-owned execution monitor plugin subscribed to `/fabric/status`.
- [x] Keep `fabric_msgs/msg/FabricStatus` on `/fabric/status` as the public execution-status interface for downstream consumers and Fabric monitor plugins.

Immediate decision for the current Fabric monitor plugin:

- [x] Remove `supervisor::FabricMonitor` as a long-term runtime dependency; Fabric execution truth now comes from `fabric_server` publishing `/fabric/status` and `fabric_monitor::ExecutionStatusMonitor` consuming that topic.
- [x] Add a Fabric-owned execution telemetry path inside `fabric_server` for plan status, current request/candidate IDs, graph hash, and terminal completion signals.
- [x] Use the external Fabric status topic as the compatibility/public execution interface rather than reintroducing an internal-only telemetry merge path.

### 8C. Fabric Monitoring Plugin Layer

- [x] Introduce a new Fabric monitoring base class for telemetry collectors/processors.
- [x] Introduce a Fabric-owned `LlmAnalysisBase` interface with PromptTools-shaped prompt, embedding, and tokenization operations so monitoring and Experience plugins can share one model-service contract without pulling `prompt_msgs` into `fabric_base` consumers that do not need it.
- [x] Add initial monitoring plugins for capability event monitoring, Fabric execution monitoring, and log monitoring.
- [x] Route rosout and diagnostics monitor output into Fabric telemetry ingestion and remove the duplicate direct subscriptions from `fabric_server`.
- [x] Move capability snapshot polling and capability-event reconstruction out of `fabric_server` into a Fabric monitor plugin that emits Fabric-owned `SystemStatus` snapshots.
- [ ] Keep monitoring collection separate from Experience scoring/persistence so telemetry ingestion remains reusable even if scoring policy changes.
- [x] Process capability, Fabric, and log monitoring flows under Fabric rather than routing them through Supervisor.
- [ ] Keep log summarization off the critical execution path; log analysis should not become a reason plan execution stalls.
- [x] Keep `MonitorBase` data-oriented; concrete monitoring plugins may optionally depend on an `LlmAnalysisBase` implementation for deferred summarization/extraction.
- [x] Load configured Fabric monitor plugins in `fabric_server` and inject the shared analysis plugin at startup.

Initial Fabric monitoring destinations:

- [x] Move `supervisor_log/RosoutMonitor` semantics into the new Fabric monitoring layer rather than `capabilities2`; log ingestion is cross-cutting runtime telemetry, not capability registry state.
- [x] Move `supervisor_diagnostic/RosDiagnosticsMonitor` semantics into the same Fabric monitoring layer if those signals are still required after parity validation.
- [x] Drop the old Dynamics and Odom monitor path from the merged Fabric runtime instead of carrying their training-heavy Supervisor configs forward.
- [ ] Keep the new Fabric monitoring layer focused on collection and normalization; leave scoring, persistence, and policy decisions in the existing Fabric Experience runtime.
- [x] Back the initial monitor-analysis path with a PromptTools-provided `LlmAnalysisBase` plugin rather than embedding prompt-service clients inside the base monitor interface.

### 8E. Shared LLM Analysis Abstraction

- [x] Add a single Fabric `LlmAnalysisBase` with three overridable operations: prompt completion, embedding, and tokenization.
- [x] Allow `GenerationBase` implementations to receive an optional shared `LlmAnalysisBase` instance for prompt-side work instead of hard-coding PromptTools service calls.
- [x] Migrate the current CoreGraphRAG embedded prompt/embedding/tokenization client behind the same `LlmAnalysisBase` contract once the interface is stable.
- [x] Keep PromptTools-specific ROS service wiring in a separate implementation package so `fabric_base` stays interface-only.

### 8D. Replace Supervisor-Derived Aggregation

- [x] Replace direct use of `supervisor_msgs/msg/SystemStatus` in Fabric runtime.
- [x] Reuse and extend `fabric_base/include/fabric_base/utils/status_structs.hpp` as the canonical Fabric telemetry model.
- [ ] Port Supervisor aggregation helpers onto Fabric-owned types, especially:
	- capability lifecycle and run-evidence accumulation
	- Fabric/log fault correlation while capabilities are active
	- per-capability related-node tracking
- [x] Port battery-usage accumulation onto Fabric-owned telemetry/status processing.
- [x] Port Fabric/log fault correlation while capabilities are active and per-capability related-node tracking into Fabric-owned status helpers.
- [x] Port shutdown-safety assessment helpers onto Fabric-owned types.
- [ ] Define any missing Fabric-owned telemetry fields needed by:
	- `core_plugin_->update(...)`
	- terminal feedback persistence
	- stop-safety guidance
	- execution/log fault correlation
- [x] Terminal feedback persistence now derives terminal outcome/success from Fabric-owned capability run evidence instead of collapsing all non-completed terminal states to cancelled.
- [ ] Decide which of the current Supervisor helper code should move unchanged into Fabric utilities versus which should be redesigned around Fabric threads/plugins.

Specific aggregation direction:

- [x] Port the join logic now centered in `supervisor_server` into Fabric: capability records grouped by `plan_id`, matched with Fabric execution state, then enriched with related-node and log evidence.
- [ ] Treat Supervisor only as a temporary compatibility publisher once Fabric owns the aggregation path; Supervisor should not remain the long-term join point.
- [ ] Preserve the current execution identity scheme during the move: `plan_id + "/" + instance_id + "/" + capability_name`.
- [ ] Keep stop-safety and terminal-outcome rules behavior-preserving during the first port, then redesign only after parity validation passes.

Likely local surface:

- `fabric_server/include/fabric_server/server.hpp`
- `fabric_server/include/fabric_server/capability_client.hpp`
- `fabric_server/include/fabric_server/utils/status.hpp`
- `supervisor/supervisor_capability/include/supervisor_capability/capability_monitor.hpp`
- `supervisor/supervisor_fabric/include/supervisor_fabric/fabric_monitor.hpp`
- `supervisor/supervisor_log/include/supervisor_log/rosout_monitor.hpp`
- `supervisor/supervisor_server/include/supervisor_server/server.hpp`
- `capabilities2/capabilities2_server/include/capabilities2_server/capabilities_server.hpp`


## Stage 9: Supervisor Retirement Gate

Goal: remove the Supervisor stack only after Fabric reaches telemetry and feedback parity.

- [ ] Reimplement the current Supervisor aggregation semantics inside Fabric rather than depending on compatibility shims long term.
- [ ] Prove Fabric can reproduce the execution evidence currently needed for Experience feedback persistence.
- [ ] Prove Fabric can derive terminal outcomes and stop-safety decisions without `supervisor_msgs/SystemStatus`.
- [ ] Prove capability activity, Fabric state, and log evidence can still be correlated after Supervisor is removed.
- [ ] Identify any remaining external consumers of Supervisor topics/services and either migrate them or provide a compatibility adapter.
- [ ] Remove Supervisor runtime dependencies from Fabric only after the replacement path is validated.
- [ ] If needed, keep a temporary Fabric-published compatibility topic/service layer for old Supervisor consumers while the internal implementation has already moved.


## Stage 10: Integration Validation And Compile Test

Goal: validate the integrated Fabric-owned stack after the structural refactor, not before it.

### Build/Test Gates

- [x] `colcon build --packages-select fabric_server`
- [x] `colcon test --packages-select fabric_server`
- [x] Clear remaining `flake8` and `uncrustify` package-test failures in `fabric_server`.
- [x] Build any directly affected Fabric Experience plugin packages after interface or class-name changes.
- [ ] Clear remaining `fabric_experience_inspector` package flake8 failures before using it as an integration gate; the live scoring endpoint is fixed, but `colcon test --packages-select fabric_experience_inspector` still fails style checks.
- [ ] Run launch smoke tests for the unified Fabric launch/config flow.
- [ ] Run a runtime smoke test with a real or replayed `/battery_state` publisher to verify Fabric battery telemetry and per-capability battery intervals after Supervisor removal.

### Runtime Validation

- [ ] Validate that every triggered capability run still produces exactly one terminal outcome after the monitoring migration.
- [ ] Validate the end-to-end explanation path from plan generation to node score explanation under Fabric-owned services.
- [ ] Validate parameter/order plan checks still reject malformed plans after the runtime refactor.
- [ ] Validate terminal feedback persistence still records enough evidence for RAPFS analysis after Supervisor removal.
- [x] Reconcile `fabric_experience_inspector` live scoring migration with the active Fabric score service name (`/fabric/experience/score`).

### Policy/Analysis Validation After Integration Stabilizes

- [ ] Re-run detailed vs community retrieval prompt comparison.
- [ ] Re-run RAPFS policy evaluation and calibration checks on the integrated Fabric-owned stack.
- [ ] Report constraint violations and rejected-plan reasons separately from final ranking quality.


## Deferred Follow-Up After Integration Stabilizes

### Retrieval And Prediction Engineering

- [ ] Calibrate transform parameters from evaluation data and record the chosen checkpoint/version.
- [ ] Tune retrieval, explanation output, and prompt-selection behavior from validation results.

### Semantic Validation Data Access

- [ ] Pass core access into validation plugins, or attach equivalent CoreGraph-backed node-description snapshots onto candidate plan nodes before validation.

### Resource Telemetry

- [ ] Add a telemetry source for quantitative resource measurements.
- [ ] Estimate per-capability resource distributions after telemetry exists.
- [ ] Activate resource-cost scoring and peak-resource feasibility only after resource telemetry has been validated.


## Recommended Order

1. Finish the remaining `server.hpp` decomposition so Experience flows are narrower but orchestration still stays in Fabric.
2. Merge launch/config ownership into Fabric.
3. Remove the `supervisor::FabricMonitor` polling path first by introducing a Fabric-native execution telemetry path inside `fabric_server`.
4. Split `supervisor::CapabilityMonitor` ownership: keep raw capability event facts and snapshot helpers in `capabilities2`, move correlation and run-evidence shaping into Fabric.
5. Introduce the Fabric monitoring plugin layer and migrate log/diagnostic monitoring semantics there.
6. Replace Supervisor-derived aggregation with Fabric-native telemetry structs and processing, reusing `fabric_base` status structs and ported helper logic.
7. Keep Supervisor only as a temporary compatibility publisher if downstream consumers still require `supervisor_msgs/SystemStatus`.
8. Remove Supervisor from the runtime path only after parity validation.
9. Run `colcon test --packages-select fabric_server` plus launch/runtime smoke coverage on the integrated Fabric path.
10. Re-run Experience validation, retrieval analysis, and RAPFS policy tuning on the integrated stack.