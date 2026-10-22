# TO-DO

Goal: consolidate Experience and monitoring into Fabric, then retire Supervisor only after telemetry and feedback parity is proven.

## Validation Snapshot

Last recorded runtime check: 2026-10-22, ROS Jazzy, default workspace install. Build and test results below remain from 2026-10-21; no rebuild or test-suite rerun was performed for this check.

- The original 20 selected runtime packages build without legacy Experience/Supervisor roots. The additionally approved `fabric_capabilities` completion-runner package also builds into the default install.
- The full Fabric package gate passes, including formatting and opt-in component integration: 110 aggregate records, 0 errors, 0 failures, 16 skipped. Earlier validator and monitor suites passed 19 and four behavioral cases respectively.
- Feedback regression tests cover score-field preservation, invalid requests, retry after a failed terminal write, duplicate-terminal suppression, and completed/failed/timed-out/cancelled outcomes. Battery replay verifies per-instance intervals and cleanup; these are not end-to-end parity proofs.
- Both component integration cases pass: initialization followed by explicit unload, and container shutdown with Fabric still loaded. Two live launch-proxy tests pass for main/child process measurements and concurrent launches.
- The isolated standard-config smoke passes fresh-core creation, plugin loading, readiness, status publication, bond formation, and clean standalone SIGINT shutdown of both Fabric and capabilities2.
- Smoke limits: isolated HOME/working directory, empty registry, harmless startup-plan override, local model-service stubs instead of prompt_bridge. Missing scan paths and invalid empty-plan runner requests mean this is not a normal capability-execution test. No paid inference or real robot capabilities were exercised.
- Optional runnable metadata is now guarded in both descriptor and provider-details conversion. Regression tests pass for absent/null/empty definitions and runtime parameters; Fabric rebuilds into the default install.
- The completion-only smoke now parses all 27 installed runnable descriptors without the missing-`definition` error. Startup subsequently stops during knowledge-core construction with `Tokenize service reported failure` from the empty local model stub; the plan still does not execute, and shutdown remains clean.
- Workspace executable-source and manifest scans found no active legacy Supervisor/Experience consumers. Documentation still contains old names/examples; consumers outside this workspace have not been audited.
- Full runtime decomposition, terminal-feedback parity, live battery/resource attribution, and model/hardware workflows remain open. Retirement is not complete.

Build from `src/fabric`, `src/capabilities2`, `src/prompt_tools`, `src/fabric_generator_prompttools`, and the approved `src/fabric_capabilities`. After shared plugin-interface changes, explicitly rebuild all runtime plugins; `--packages-up-to fabric_server` alone misses dynamically loaded consumers. Source the default install before tests. Enable component integration with `FABRIC_RUN_INTEGRATION_TESTS=1` and use an isolated `ROS_DOMAIN_ID`.

## Stage 5: Packaging And Migration

- [x] Unify Fabric executable/component packaging, dependencies, Python installation, and test targets.
- [x] Migrate copied Experience modules, scripts, imports, and service names to Fabric ownership.
- [x] Use Fabric's internal parser and `CapabilityClient` runnable metadata instead of legacy interface plugins.
- [x] Add passing Python entry-point and wrapper smoke coverage.

## Stage 6: Runtime Decomposition

- [x] Extract feedback, planning, score/explanation, status, telemetry, and Experience configuration helpers into `fabric_server/include/fabric_server/utils/`.
- [x] Split initialization, generation, selection, and result completion into smaller orchestration helpers.
- [x] Narrow queue/completion state access and remove legacy external Experience-server naming.
- [x] Extract external feedback read/merge/write and terminal-feedback finalization into existing helpers, with focused persistence regression tests.
- [ ] Finish narrowing `server.hpp` to ROS interfaces, worker lifecycle, plugin loading, and high-level orchestration.
- [ ] Finish isolating the remaining Experience services and generation/scoring flows from plan dispatch; feedback persistence has been extracted.
- [ ] Validate parsing, validation, and dispatch behavior throughout the remaining refactor; avoid unrelated policy changes.

## Stage 7: Launch And Configuration

- [x] Make `fabric_server/launch/fabric.launch.py` the unified entry point; remove separate Experience launch, readiness shim, and Supervisor inclusion.
- [x] Consolidate Experience, monitoring, battery, plugin, and model settings in `fabric_server/config/fabric.yaml`.
- [x] Remove obsolete Experience package-share lookups and normalize plugin names and parameter namespaces.

## Stage 8: Monitoring And LLM Ownership

Ownership rules: capabilities2 owns capability facts and event history. Fabric monitors collect and normalize telemetry; Fabric Experience owns scoring, persistence, and policy. Keep cross-source analytics and stop-safety decisions out of capabilities2.

### Implemented

- [x] Publish authoritative capability events and snapshots from capabilities2.
- [x] Consume and reconstruct capability lifecycle/run evidence in Fabric's `CapabilityEventMonitor`.
- [x] Publish execution truth on `/fabric/status` and consume it with `ExecutionStatusMonitor`.
- [x] Add `MonitorBase` and load resource, execution, capability-event, rosout, and diagnostics monitors in Fabric.
- [x] Move battery subscription, reading validation, and low/critical warnings into `BatteryMonitor`, configured through `BatteryMonitor.enable` and `BatteryMonitor.topic`; retain execution attribution and persistence in Fabric. Monitor and component-lifecycle regressions pass; the earlier missing `Fabric::shutdownRuntime()` rebuild blocker is superseded by the successful build and lifecycle gates above. Actual-execution battery validation remains open.
- [x] Route log/diagnostic output through monitor plugins instead of duplicate server subscriptions; drop the Dynamics/Odom path.
- [x] Share `LlmAnalysisBase` prompt, embedding, and tokenization operations across monitoring, generation, and CoreGraphRAG; keep PromptTools wiring outside `fabric_base`.
- [x] Use Fabric-owned `SystemStatus` types instead of `supervisor_msgs/SystemStatus`.
- [x] Implement plan/instance correlation, run-evidence accumulation, log/fault enrichment, related-node tracking, battery intervals, and stop-safety helpers.
- [x] Preserve execution identity as `plan_id + "/" + instance_id + "/" + capability_name`.
- [x] Derive persisted terminal outcomes from run evidence rather than treating every non-completed state as cancelled.
- [x] Consume launch-proxy resource snapshots and test subscription, plugin loading, field preservation, and native conversion.
- [x] Stop standalone workers and bonds before ROS context shutdown; cancel pending LLM/snapshot waits and cover unanswered snapshot cancellation with a regression test.
- [x] Use `MultiThreadedExecutor::spin()` with coordinated cancellation in both standalone executables.
- [x] Initialize Fabric components after shared ownership exists and explicitly clean up runtime ownership on unload or context pre-shutdown.
- [x] Retain pending terminal evidence when persistence fails; finalize only after successful writes and suppress subsequent duplicate terminal snapshots.

### Remaining

- [ ] Audit collection/normalization boundaries so monitoring remains reusable independently of scoring and persistence.
- [ ] Validate deferred log analysis cannot stall plan execution under load or unavailable model services.
- [ ] Resolve any telemetry-field gaps found by core-update, feedback, stop-safety, and correlation parity tests.
- [ ] Keep terminal-outcome and stop-safety policy changes deferred until parity validation passes.

## Stage 9: Supervisor Retirement Gate

- [x] Build and start the selected integrated runtime without legacy Experience/Supervisor source roots or Supervisor launch dependencies.
- [x] Implement Fabric-owned aggregation and terminal-outcome/stop-safety helpers; runtime parity is still pending.
- [ ] Prove every triggered run produces exactly one terminal outcome with sufficient evidence for Experience/RAPFS feedback persistence.
- [ ] Prove terminal outcomes and stop-safety decisions match required semantics without Supervisor messages.
- [ ] Prove capability activity, Fabric state, logs, and related nodes remain correctly correlated.
- [x] Scan workspace executable sources and manifests for legacy Supervisor/Experience consumers; none found in the scan.
- [ ] Audit deployed/external Supervisor consumers and migrate or adapt only where needed; update stale documentation examples.
- [ ] Complete parity gates before declaring Supervisor fully retired.

## Stage 10: Integration Validation

### Build And Test Gates

- [x] Build the 20 selected runtime packages into the default workspace install.
- [x] Build the additionally approved `fabric_capabilities` package for a harmless completion-only execution test.
- [x] Rebuild affected Experience and dynamically loaded plugins after interface/class-name changes.
- [x] Pass Fabric behavioral tests, Python smoke tests, flake8, and the other non-uncrustify CTest gates.
- [x] Pass focused core, estimator, planner, scorer, validator, and monitor tests.
- [x] Pass the isolated unified-launch smoke using existing standard config files and local model stubs.
- [x] Verify fresh-core startup, status publication, bond formation, and clean native standalone shutdown.
- [x] Fix the three Fabric uncrustify divergences and pass the full package test suite, including opt-in component integration.
- [ ] Clear `fabric_experience_inspector` flake8 failures before treating it as an integration gate.
- [x] Validate initialized component unload and active-component container shutdown with standard configs and local model stubs.
- [x] Fix runnable-metadata handling of missing/null/empty YAML definitions and runtime parameters, with descriptor/provider regression coverage.
- [ ] Supply successful local model-stub responses for knowledge-core initialization, then rerun the completion-only plan through normal dispatch.
- [ ] Investigate PromptBridge's repeated `Prompt family not found` errors and configure a working model backend before rerunning the model-backed launch; the 2026-10-22 OpenAI request was rejected for exhausted credits.
- [ ] Remove or correct the stale `/opt/ros/humble/share` capability scan path for ROS Jazzy.
- [ ] Report post-readiness knowledge-core initialization failures separately from capabilities2 readiness failures.

### Runtime Gates

- [ ] Validate parameter/order checks reject malformed plans through the integrated runtime; validator unit coverage already passes.
- [ ] Validate end-to-end plan generation, score explanation, and terminal feedback persistence.
- [x] Align the inspector's live scoring endpoint with `/fabric/experience/score`.
- [x] Replay capability lifecycle snapshots to verify separate battery intervals for instances, interval retention, and per-plan cleanup.
- [ ] Validate battery-topic ingestion and per-capability intervals during actual capability execution.
- [x] Run live proxy tests for main/child process resource production and concurrent local launches.
- [ ] Validate Fabric resource attribution, shared processes, and terminal samples. LaunchRunner currently uses provider/interface references, not a unique execution identity; do not infer attribution from them alone.
- [ ] Validate full model-backed and hardware workflows when authorized.

## Deferred Analysis

- [ ] Compare detailed versus community retrieval prompts.
- [ ] Re-run RAPFS evaluation/calibration; report constraint violations and rejection reasons separately from ranking quality.
- [ ] Calibrate prediction transforms, record checkpoint/version, and tune retrieval/explanation/prompt selection from results.
- [ ] Give semantic validation access to core-backed node descriptions.
- [ ] Estimate per-capability resource distributions and enable resource-cost scoring/peak feasibility only after telemetry validation.

## Next Steps

1. Supply valid local model-stub responses and get the harmless completion-only plan through normal dispatch. For a model-backed rerun, first resolve PromptBridge family lookup errors and backend availability; the latest launch passed tokenization but failed core construction on exhausted OpenAI credits. Do not treat plan loading as execution proof.
2. Finish Experience service/generation isolation while preserving dispatch behavior.
3. Validate end-to-end feedback and battery telemetry; establish reliable launch-to-execution identity before claiming resource attribution.
4. Audit deployed consumers, refresh legacy documentation examples, and complete Supervisor parity gates.
5. Run deferred model, policy, and calibration work after integration stabilizes.