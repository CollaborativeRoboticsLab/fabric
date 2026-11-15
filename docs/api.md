# ROS2 Interface

| Node | Description |
| :--- | :--- |
| `Fabric` | Main server node. Loads the configured parser and validation plugins, queues plans, validates them against capabilities2, allocates/connects capabilities, and tracks execution status. |


| Service                   | Service Message           | Description |
| :---                      | :---                      | :---        |
| `/fabric/cancel_plan`     | `CancelFabricPlan.srv`    | Cancel the current plan running in the Fabric |
| `/fabric/set_completion`  | `CompleteFabric.srv`      | Mark the current plan complete. The request contains only `plan_id`; the response is empty. |
| `/fabric/plan/submit`     | `SubmitFabricPlan.srv`    | Submit a plan plus optional Fabric-owned metadata. On success the response contains a generated `plan_id`; on rejection it returns an empty `plan_id` and an error string. |
| `/fabric/get_plan_status` | `GetPlanStatus.srv`       | Query the current queued or active status for a `plan_id`. Returns a plan-keyed `FabricStatus` code and, for parse failures, any rejected XML fragments collected during syntax validation. |
| `/fabric/plan/get_snapshot` | `GetFabricPlanSnapshot.srv` | Read the latest cached snapshot of the current dequeued plan. The header stamp is the ROS time when that state was captured, not the response time; a zero stamp means no state has been captured yet (and ROS simulated time may also be zero). `has_plan` is false when none is available; `connections_complete` distinguishes successfully parsed connections from pre-parse or partial parse data. `instances` lists distinct capability endpoints by interface, provider, and instance ID. Connections describe the plan's intended event links, not confirmed runtime connections. |

Snapshot data is published at plan state changes, so an old timestamp alone does not mean a running plan is unhealthy. The service does not wait for the execution mutex and has its own callback group; capability-client responses also use a separate group. A multithreaded executor is required for responsiveness while another callback is blocked, and executor saturation can still delay responses. Parsing failures may contain partial connections; lifecycle failure codes do not include an error message. Completed, cancelled, and failed states may be cleared or replaced quickly and are not an execution history. A queued plan is not exposed as the current plan. The response does not include running capabilities or dependencies.

Capabilities2 keeps connection ownership by bond internally. Its event snapshot is bounded history, not a complete current connection inventory, so it cannot by itself confirm that the plan's connections remain active.

## Concurrency and lifecycle

The standalone server uses a multithreaded executor. A component uses its host's
executor; the host controls its thread count. Except for the snapshot service,
the adapter's services and action callbacks use the default mutually exclusive
callback group. Capability clients use a separate reentrant group. A separate
group does not reserve a thread, and a single-threaded host cannot service a
client reply while blocked in another callback.

The runtime owns one configured parser instance. Execution, parsing requests,
and experience planning share it: `parsePlan()` and `checkCompatibility()`
serialize on that instance. Initialization and starter-plan `load_file()` run
before workers start. Third-party plugins can have shared or global state, so
separate plugin instances must not be assumed independent or cheap to initialize.
The execution validators are initialized once from `validation_plugins` and run
in order. The unfinished runtime-only `validatePlan()` also uses those instances
without serialization; it is not safe to invoke concurrently with execution and
is not exposed as `/fabric/plan/validate`. Candidate validators are separate
instances used by the generation/scoring pipeline.

Execution uses a single worker. It holds the current-plan mutex through parsing,
discovery, validation and capability setup, but releases it while waiting for
completion. Thus cancellation and status can wait for blocking setup operations;
the snapshot service reads a separately published copy. Execution can acquire
the queue or completion mutex while holding the current-plan mutex; code taking
the queue mutex must release it before acquiring the current-plan mutex. The
generation operation mutex serializes generation and explicit scoring. Accepted
generation requests currently create workers without an admission limit, and
the plan and status queues are also unbounded. No validation overload policy has
been implemented.

Runtime shutdown signals cancellation, stops monitors, joins owned workers, and
only then releases plugins and clients. Waiting for an unavailable startup
service observes cancellation, but arbitrary blocking plugin calls are not
forcibly interrupted. Standalone shutdown stops and removes the executor before
calling runtime shutdown. The component host can invoke shutdown or unload while
its executor is active; runtime API calls and shutdown are not generally
coordinated. The node must outlive the runtime, and external owners must drain
adapter calls before destroying runtime resources.

Before exposing validation, its implementation must isolate or serialize parser
and validator operations, keep their loaders and instances alive until requests
drain, bound accepted work and discovery time, and define overload responses.
Discovery must not occupy all executor threads needed to receive replies. The
selected component-host executor requirements and unload behavior must be tested.
