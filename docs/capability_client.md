# CapabilityClient

The `CapabilityClient` class in Fabric is responsible for all interactions with the capabilities2 server. It manages service clients for querying available capabilities, establishing bonds, allocating and freeing capabilities, connecting them according to the plan, and triggering execution.

## Responsibilities
- Discover available interfaces and providers from the capabilities2 server

- Establish a bond for plan execution

- Allocate (use) and free capabilities as required by the plan

- Connect capabilities according to the parsed plan's connections

- Trigger the first node to start execution

## Main Methods

## Main Methods and Related Services

| Method                          | Purpose                                                                 | Related Capability Service                |
|----------------------------------|-------------------------------------------------------------------------|-------------------------------------------|
| `initialize(node)`               | Set up all service clients and parameters                               | (all services below)                      |
| `getInterfaces(capabilities)`    | Query all available interfaces and populate the list                    | `/capabilities/get_interfaces`            |
| `getSemanticInterfaces(capabilities)` | Query and append semantic interfaces for each capability         | `/capabilities/get_semantic_interfaces`   |
| `getProviders(capabilities)`     | Query and set providers and alternatives for each capability            | `/capabilities/get_providers`             |
| `request_bond()`                 | Establish a bond with the server and return the bond ID                 | `/capabilities/establish_bond`            |
| `use_capabilities(plan)`         | Request allocation of all capabilities in the plan                      | `/capabilities/use_capability`            |
| `free_capabilities(plan)`        | Free all started capabilities in the plan                               | `/capabilities/free_capability`           |
| `connect_capabilities(plan)`     | Configure connections between capabilities as per the plan              | `/capabilities/connect_capability`        |
| `trigger_first_node(plan)`       | Trigger the first capability in the plan to start execution             | `/capabilities/trigger_capability`        |

## Usage in Fabric

- The Fabric server creates and initializes a `CapabilityClient` at startup.

- When a plan is processed, the client:
  1. Queries interfaces and providers for all required capabilities.
  
  2. Establishes a bond for the plan.

  3. Allocates (uses) all required capabilities.

  4. Configures connections between capabilities as specified in the plan's connections.

  5. Triggers the first node to start execution.

  6. Frees capabilities when the plan is complete or on error.

## Example Flow

1. **Initialization**: `initialize(node)` sets up all service clients and waits for them to become available.

2. **Discovery**: `getInterfaces()` and `getProviders()` populate the list of capabilities and their providers.

3. **Bonding**: `request_bond()` establishes a bond for the plan.

4. **Allocation**: `use_capabilities(plan)` allocates all required capabilities.

5. **Connection**: `connect_capabilities(plan)` configures the event-driven connections between capabilities.

6. **Execution**: `trigger_first_node(plan)` starts the plan execution.

7. **Cleanup**: `free_capabilities(plan)` releases all used capabilities.

## Error Handling
- All service calls are checked for validity and throw `fabric::fabric_exception` on failure.

- The client waits for each service to become available before proceeding.

- If any step fails, capabilities are freed and errors are logged.

---

The `CapabilityClient` is a central component for orchestrating the dynamic allocation, connection, and management of capabilities in Fabric, ensuring robust and flexible plan execution.
