# TO-DO

## Current Integration Findings

Goal: finish folding the Experience runtime into Fabric without keeping brittle Fabric/Supervisor/Experience synchronization boundaries.

Current code inspection already shows the main breakpoints:

- `fabric_server/CMakeLists.txt` has been rewritten into one coherent package definition and `fabric_server` now compiles again; remaining package work is narrower dependency/test auditing, not foundation repair.
- The copied Experience Python package and scripts have already been migrated substantially toward Fabric-owned imports and `/fabric/experience/...` endpoints, but smoke validation and leftover naming/test cleanup still remain.
- `fabric_server/include/fabric_server/server.hpp` still owns the high-level Fabric and Experience runtime, plugin loading, plan generation, scoring, and feedback persistence, but several support-heavy slices have already been extracted into helper headers under `include/fabric_server/utils/`.
- `fabric_server/launch/fabric.launch.py` still includes `experience_server.launch.py` and Supervisor parameters, so launch ownership has not actually been consolidated yet.
- Fabric runtime feedback still depends on `supervisor_msgs/msg/SystemStatus`; the conversion and status-shaping helpers have been narrowed into `utils/status.hpp`, but the runtime dependency itself is still present.
- `capabilities2` already exposes the authoritative capability event stream and snapshot service (`/capabilities/get_event_snapshot`), so moving raw capability-run monitoring ownership toward `capabilities2` is viable.
- `fabric_base/include/fabric_base/utils/status_structs.hpp` already provides a Fabric-owned status model close to the Supervisor aggregation shape, so the migration can port logic onto an existing Fabric type surface instead of inventing a new schema from scratch.

Validated current state:

- `colcon build --packages-select fabric_server` is passing again after the packaging repair and current `server.hpp` decomposition slices.
- Score explanation, scoring epoch, plan feedback, feedback persistence, planning helpers, and plan-status shaping have already been pulled out of the hottest parts of `server.hpp`.


## Stage 5: Fabric Server Foundation Repair

Goal: make `fabric_server` build and package cleanly as the primary integrated runtime before doing larger ownership moves.

### Build And Packaging

- [x] Rewrite `CMakeLists.txt` into one coherent package definition.
- [x] Keep exactly one `ament_package()` call.
- [x] Define a single clear build story for the Fabric executable/component and the integrated Experience support code.
- [x] Replace stale `experience_*` package references with the actual Fabric-side packages now present in this repo (`fabric_experience_*`, `fabric_validator`, `fabric_parser_xml`, etc.) where appropriate.
- [x] Install the Python package and scripts only once from the unified package definition.
- [ ] Fix test target dependencies and header include paths that still reference `experience_server`.
- [x] Update `package.xml` to match the real integrated dependency graph.

### Python Utilities Migrated From Experience

- [x] Update `fabric_server/__init__.py` to export Fabric-owned modules instead of importing from `experience_server`.
- [x] Update copied Python modules to use intra-package `fabric_server` imports.
- [x] Update ROS service names in copied scripts to Fabric-owned endpoints under `/fabric/experience/...`.
- [ ] Audit command-line help text, package names, and output text so they refer to Fabric rather than Experience where Fabric is now the runtime owner.
- [ ] Fix Python tests that still assume the old Experience package/module layout.

### Immediate Validation

- [x] Keep running `colcon build --packages-select fabric_server` after each packaging slice until the package compiles.
- [ ] Add a focused smoke check for the copied Python entry points after imports and service names are updated.

Evidence: `colcon build --packages-select fabric_server` is currently passing after the unified `CMakeLists.txt` repair, package manifest/schema fixes, and the latest `server.hpp` extraction slices.

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
- [ ] Remove legacy Experience naming inside the Fabric runtime where the code is now Fabric-owned.
- [ ] Reduce cross-cutting direct access to shared state by giving each worker flow a narrower helper surface.

Completed decomposition slices so far:

- [x] Extract feedback persistence support into `include/fabric_server/utils/feedback.hpp`.
- [x] Extract planning and score-only candidate helpers into `include/fabric_server/utils/planning.hpp`.
- [x] Extract score explanation and scoring-epoch/plan-feedback service shaping into `include/fabric_server/utils/score_services.hpp` and `include/fabric_server/utils/score_explanation_adapter.hpp`.
- [x] Extract Supervisor-status conversion and plan-status shaping helpers into `include/fabric_server/utils/status.hpp`.
- [x] Split `initialize()` in `server.hpp` into smaller setup methods while keeping the top-level orchestration in `server.hpp`.
- [x] Split plan-generation support into smaller private helper methods while keeping `executePlanGeneration()` as the orchestration entry point.

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

- [ ] Merge the intent of `experience_server.launch.py` into `fabric.launch.py`.
- [ ] Remove the need for `fabric.launch.py` to include `experience_server.launch.py`.
- [ ] Replace launch arguments that still treat Experience as a separate stack with Fabric-owned toggles for optional subsystems.
- [ ] Decide whether a compatibility launch file should remain temporarily as a thin wrapper that delegates to Fabric launch.

### Configuration Consolidation

- [ ] Merge `config/experience_server.yaml` into Fabric-owned configuration layout.
- [ ] Keep Fabric defaults in `config/fabric.yaml` or a small set of Fabric-owned config files, not separate Experience-owned runtime configs.
- [ ] Normalize plugin class names, parameter namespaces, and default model/scoring/refinement settings under the Fabric package.
- [ ] Remove package-share lookups that still point at `experience_server` when Fabric now ships the runtime.

Likely local surface:

- `fabric_server/launch/fabric.launch.py`
- `fabric_server/launch/experience_server.launch.py`
- `fabric_server/config/fabric.yaml`
- `fabric_server/config/experience_server.yaml`


## Stage 8: Monitoring Ownership Migration

Goal: move telemetry collection closer to the real owners while replacing Supervisor coupling with Fabric-native monitoring interfaces.

### 8A. Capability Monitoring Ownership

- [ ] Move raw capability lifecycle/run evidence ownership to `capabilities2` where appropriate.
- [ ] Treat `capabilities2` as the source of truth for capability event publication and event snapshot access.
- [ ] Extend `fabric_server/include/fabric_server/capability_client.hpp` with any missing snapshot/query helpers Fabric needs.
- [ ] Replace Fabric’s dependency on Supervisor capability summaries with Fabric-side processing of `capabilities2` event data.
- [ ] Keep high-level cross-source analytics out of `capabilities2`; only move capability facts and event history there.

### 8B. Fabric Monitoring Ownership

- [ ] Move Fabric plan execution monitoring to Fabric itself instead of polling it back through Supervisor.
- [ ] Replace the current Supervisor `FabricMonitor` polling contract with a Fabric-native internal telemetry path.
- [ ] Decide whether Fabric still needs an external status topic/message for downstream consumers after Supervisor is removed.

### 8C. Fabric Monitoring Plugin Layer

- [ ] Introduce a new Fabric monitoring base class for telemetry collectors/processors.
- [ ] Add initial monitoring plugins for:
	- capability event monitoring
	- Fabric execution monitoring
	- log monitoring
- [ ] Keep monitoring collection separate from Experience scoring/persistence so telemetry ingestion remains reusable even if scoring policy changes.
- [ ] Process capability, Fabric, and log monitoring flows under Fabric rather than routing them through Supervisor.
- [ ] Keep log summarization off the critical execution path; log analysis should not become a reason plan execution stalls.

### 8D. Replace Supervisor-Derived Aggregation

- [ ] Replace direct use of `supervisor_msgs/msg/SystemStatus` in Fabric runtime.
- [ ] Reuse and extend `fabric_base/include/fabric_base/utils/status_structs.hpp` as the canonical Fabric telemetry model.
- [ ] Port Supervisor aggregation helpers onto Fabric-owned types, especially:
	- shutdown-safety assessment
	- capability lifecycle and run-evidence accumulation
	- Fabric/log fault correlation while capabilities are active
	- per-capability related-node tracking
- [ ] Define any missing Fabric-owned telemetry fields needed by:
	- `core_plugin_->update(...)`
	- terminal feedback persistence
	- stop-safety guidance
	- execution/log fault correlation
- [ ] Decide which of the current Supervisor helper code should move unchanged into Fabric utilities versus which should be redesigned around Fabric threads/plugins.

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
- [ ] `colcon test --packages-select fabric_server`
- [ ] Build any directly affected Fabric Experience plugin packages after interface or class-name changes.
- [ ] Run launch smoke tests for the unified Fabric launch/config flow.

### Runtime Validation

- [ ] Validate that every triggered capability run still produces exactly one terminal outcome after the monitoring migration.
- [ ] Validate the end-to-end explanation path from plan generation to node score explanation under Fabric-owned services.
- [ ] Validate parameter/order plan checks still reject malformed plans after the runtime refactor.
- [ ] Validate terminal feedback persistence still records enough evidence for RAPFS analysis after Supervisor removal.

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
3. Move telemetry ownership: capabilities in `capabilities2`, Fabric state in Fabric, logs via Fabric monitoring plugins.
4. Replace Supervisor-derived aggregation with Fabric-native telemetry structs and processing, reusing `fabric_base` status structs and ported helper logic.
5. Remove Supervisor from the runtime path only after parity validation.
6. Run `colcon test --packages-select fabric_server` plus launch/runtime smoke coverage on the integrated Fabric path.
7. Re-run Experience validation, retrieval analysis, and RAPFS policy tuning on the integrated stack.