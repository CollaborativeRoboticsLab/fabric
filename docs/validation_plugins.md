
## Motivation and Design

Validation plugins in Fabric provide a flexible and extensible mechanism for checking the compatibility and correctness of Fabric plans before execution. By decoupling validation logic from the core, Fabric can support multiple validation strategies and policies without requiring changes to the main system.

- **Early error detection:** Catch configuration and compatibility issues before plan execution.

- **Extensibility:** New validation plugins can be added to enforce custom rules or policies.

- **Runtime selection:** Validation plugins are selected at runtime through the ordered `validation_plugins` parameter.

## ValidationBase Interface

All validation plugins inherit from the `ValidationBase` abstract class, which defines the required interface:

- `initialize(const rclcpp::Node::SharedPtr&)`: Prepares the plugin with the ROS2 node context.

- `validate(fabric::Plan&, std::vector<CapabilityInfo>&)`: Validates the plan against available capabilities.

This ensures that all plugins are interchangeable and can be managed uniformly by the Fabric core.

Fabric applies validators in the configured order. This allows a plan to first pass cheap structural checks, then stricter parameter-link and policy checks.

## CompatibilityValidation: Reference Implementation

The `CompatibilityValidation` plugin is the default validator for plan compatibility. Its key features include:

- **Interface/provider matching:** Ensures every connection source in the parsed plan references a valid capability interface and provider.

- **Provider flexibility:** Supports both default and alternative providers for redundancy.

- **Error reporting:** Throws on the first unmatched interface/provider combination, preventing invalid plans from executing.

### How CompatibilityValidation Works

1. **Initialization:** The plugin is initialized with the ROS2 node context.

2. **Validation:** For each connection in the plan:
   - Checks whether the source node's interface and provider match any entry in the provided capabilities list.
   - If not matched, checks the interface's `alt_providers` list.
   - If still unmatched, throws an exception to halt processing.

3. **Result:** Only plans with valid capability assignments are allowed to proceed to execution.

## ParameterValidation: Runtime Data-Flow Validation

`ParameterValidation` extends Fabric validation beyond interface/provider matching.

Its current role is to verify that required runtime inputs on each selected provider can be satisfied by one of the allowed sources declared in the provider metadata.

Accepted satisfaction paths are:

- a matching upstream runtime output,
- an explicit value in the Fabric plan for that runner,
- a declared configuration fallback parameter,
- a declared default value.

Matching currently prefers exact semantic-key or parameter-name overlap with type equality when both sides declare a type.

This validator depends on runnable-spec metadata loaded from `capabilities2/get_runnable_specs`.

## Main Method

| Method                | Purpose                                                                 |
|-----------------------|-------------------------------------------------------------------------|
| `validate(plan, capabilities)` | Validates compatibility and, depending on the plugin, runtime parameter coverage. |

## Example Usage

- The Fabric server loads the configured validation plugins at startup.

- After parsing a plan, the server calls each configured validator in order before proceeding to capability allocation and execution.

- If validation fails, the server marks the plan as `VALIDATION_FAILED` and logs the exception.

## Extending Validation

To support new validation policies:
1. **Implement a new validation plugin** inheriting from `ValidationBase`.

2. Register the plugin so it can be discovered and loaded by Fabric.

3. Add the plugin name to the `validation_plugins` parameter in the desired execution order.

Read more about the parameter validation in the [Parameter Validation](./parameter_validation.md) document.

---

**Summary:**
- The validation plugin system allows Fabric to compose validation strategies without changing the server.

- `CompatibilityValidation` enforces capability compatibility before deeper checks run.

- `ParameterValidation` enforces required runtime-input coverage using provider metadata and declared fallback semantics.

- New validation policies can be supported by adding new plugins, keeping the core stable and extensible.
