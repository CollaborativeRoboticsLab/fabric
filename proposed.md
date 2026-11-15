# Fabric Concurrency and Validation Plan

## Goal

Make plan parsing and non-dispatching validation responsive during execution and
generation, with bounded concurrency and predictable shutdown. Evaluate other
callbacks and workers individually; do not replace threads merely to eliminate
`std::thread`.

## Current Baseline

- The standalone server already uses a `MultiThreadedExecutor`; a component's
	executor and thread count are chosen by its host.
- Most adapter services share the default mutually exclusive callback group.
	The snapshot service has its own group, and capability clients use a separate
	reentrant group.
- `ParserBase::parsePlan()` serializes calls on the same parser instance. The
	XML parser has mutable instance state; `check_compatibility()` is outside that
	mutex. Separate instances can allow parallel parsing, subject to plugin safety.
- The active adapter is `fabric_server/include/fabric_server/fabric_server.hpp`.
	The older `server.hpp` is not used by the standalone server or component.
- `FabricRuntime::validatePlan()` is currently an uncommitted, incomplete
	implementation: the active adapter does not expose `/fabric/plan/validate`,
	and concurrency and shutdown safety have not been established. Preserve and
	evaluate this work rather than assuming it is a finished service.
- Execution holds `current_plan_mutex_` across potentially blocking work;
	generation and scoring share `generation_operation_mutex_`. More executor
	threads alone will not remove these bottlenecks.

## Work

1. **Define concurrency and lifecycle contracts.** Inventory the active adapter,
	 runtime workers, plugin initialization, shared state and shutdown paths.
	 Document which plugin operations may run concurrently, who owns each instance,
	 lock ordering, request limits and overload behavior. Account for component
	 unload as well as standalone shutdown. Do not assume third-party plugins are
	 reentrant, or that repeated initialization is free of parameter declarations,
	 ROS entities, shared resources or global state.

2. **Isolate parsing and validation.** Use the same configured plugin classes
	 and settings as execution, but separate initialized instances where required
	 for parallelism. Prefer a bounded pool or per-role instances over unbounded
	 per-request construction. Audit validator instances too: isolate or serialize
	 them unless their contract permits concurrent use. Keep plugin loaders and
	 instances alive for the full duration of in-flight operations, and cover
	 `check_compatibility()` as well as `parsePlan()` in the safety audit.

3. **Implement the active validation service.** Wire `/fabric/plan/validate` in
	 the active adapter and adapt the partial runtime method to the chosen ownership
	 model. Parse the submitted XML, retrieve runnable descriptors, and invoke the
	 configured validators without enqueueing, dispatching, bonding, using,
	 connecting or triggering capabilities. Return `completed=true, valid=false`
	 for parse or validation rejection; return `completed=false, valid=false` for
	 discovery unavailable or timed out. A valid result reflects configured
	 validators only; it is not a guarantee of general parameter type/value safety.

4. **Choose and implement bounded scheduling.** Put eligible endpoints in
	 explicit callback groups based on audited shared state and ordering. Validation
	 must not block all executor threads while waiting for capability-client replies:
	 prefer asynchronous discovery with a supported deferred service-response API,
	 or bounded off-executor work with a verified response/lifecycle mechanism.
	 If synchronous callbacks are chosen, enforce admission limits and reserve
	 executor capacity for replies and other essential callbacks, with timeouts and
	 documented host requirements. Support single-threaded component hosts with the
	 chosen design, or explicitly make a multithreaded host a documented prerequisite
	 and test that configuration. Define behavior when capacity is exhausted.

5. **Improve other callbacks selectively.** Audit submit, cancel, completion,
	 status, snapshot, scoring and feedback for lock contention, ordering, plugin
	 safety and responsiveness before moving them to separate groups. Reduce lock
	 scope around blocking execution operations where safe, preserving state
	 transitions. Do not claim scoring and generation are parallel while they share
	 their operation lock.

6. **Evaluate worker conversions.** Consider a periodic timer for monitor polling
	 only if plugin calls and ingestion are bounded and nonblocking; prevent
	 overlapping polls. Consider event-driven readiness for startup. Keep execution
	 and generation on bounded workers while they can block; an asynchronous
	 callback/state-machine redesign is a separate change with explicit cancellation,
	 ordering, timeout and backpressure semantics. Convert status processing only
	 if core updates and persistence can be kept short and ordered. A one-shot
	 timer is an executor callback, not a dedicated worker.

7. **Finish integration and documentation.** Verify the GUI's validation flow
	 against the active service and the current capabilities2 runnable-spec index
	 contract (`interface_name|provider_name`); update its parsing and fixtures if
	 needed. Document executor and plugin requirements, configuration, limits,
	 validation semantics and any remaining worker threads.

## Verification

- Build the affected packages and run focused server and GUI tests. Rebuild before
	running changed C++ tests; report any unavailable integration environment.
- Exercise concurrent parse and validation requests while a plan executes and
	generation runs. Verify independent results, plugin isolation, bounded resource
	use and responsiveness of snapshot, status and cancellation.
- Cover invalid XML, validator rejection, unavailable discovery, timeout and
	overload; assert validation never causes execution or capability side effects.
- Test delayed client replies under saturation, including the supported component
	executor configuration, to detect starvation and deadlocks.
- Test cancellation, shutdown and component unload with requests and workers in
	flight; ensure callbacks drain or are safely stopped before plugins and clients
	are destroyed. Test ordering and non-overlap for any converted worker.
- Run a real service/GUI integration check where dependencies are available;
	distinguish configured-validator success from full semantic validation.

## Suggested Delivery Order

Land the contract and lifecycle decisions first, then isolated parsing and the
active validation service with focused tests. Follow with individually audited
callback groups and worker changes; keep the larger execution/generation
state-machine redesign separate unless it is required to meet the service goals.
