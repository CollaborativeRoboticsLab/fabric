
# Validation Plugins in Fabric

## Motivation and Design

Validation plugins in Fabric provide a flexible and extensible mechanism for checking the compatibility and correctness of Fabric plans before execution. By decoupling validation logic from the core, Fabric can support multiple validation strategies and policies without requiring changes to the main system.

- **Early error detection:** Catch configuration and compatibility issues before plan execution.

- **Extensibility:** New validation plugins can be added to enforce custom rules or policies.

- **Runtime selection:** Validation plugins can be loaded and chained at runtime.

## ValidationBase Interface

All validation plugins inherit from the `ValidationBase` abstract class, which defines the required interface:

- `initialize(const rclcpp::Node::SharedPtr&)`: Prepares the plugin with the ROS2 node context.

- `validate(fabric::Plan&, std::vector<CapabilityInfo>&)`: Validates the plan against available capabilities.

This ensures that all plugins are interchangeable and can be managed uniformly by the Fabric core.

## CompatibilityValidation: Reference Implementation

The `CompatibilityValidation` plugin is the default validator for plan compatibility. Its key features include:

- **Interface/provider matching:** Ensures every connection in the plan references a valid capability interface and provider.

- **Provider flexibility:** Supports both default and alternative providers for redundancy.

- **Error reporting:** Logs and rejects any connection with unmatched interface/provider combinations, preventing invalid plans from executing.

### How CompatibilityValidation Works

1. **Initialization:** The plugin is initialized with the ROS2 node context.

2. **Validation:** For each connection in the plan:
   - Checks if the source node's interface and provider match any entry in the provided capabilities list.
   - If not matched, checks alternative providers for the interface.
   - If still unmatched, logs an error, adds the connection to the rejected list, and throws an exception to halt processing.

3. **Result:** Only plans with valid capability assignments are allowed to proceed to execution.

## Main Method

| Method                | Purpose                                                                 |
|-----------------------|-------------------------------------------------------------------------|
| `validate(plan, capabilities)` | Validates each connection's interface/provider against available capabilities. |

## Example Usage

- The Fabric server loads the validation plugin at startup.

- After parsing a plan, the server calls `validate(plan, capabilities)` before proceeding to capability allocation and execution.

- If validation fails, the plan is rejected and errors are logged for review.

## Extending Validation

To support new validation policies:
1. **Implement a new validation plugin** inheriting from `ValidationBase`.

2. Register the plugin so it can be discovered and loaded by Fabric.

3. The core remains unchanged; only the new plugin is added.

Plugins can be chained or selected at runtime to enforce different validation policies.

---

**Summary:**
- The validation plugin system enables Fabric to support multiple validation strategies in parallel.

- The `CompatibilityValidation` plugin demonstrates how to enforce capability compatibility and prevent invalid plans from executing.

- New validation policies can be supported by adding new plugins, keeping the core stable and extensible.
