# Parser as a Plugin in Fabric

## Motivation and Design

The parser in Fabric is implemented as a plugin so plan interpretation can be changed without modifying the server. A parser is selected at runtime through the `parsing_plugin` parameter. This design allows for:

- **Backward and forward compatibility**: New XML plan formats or schema changes can be supported by simply adding new parser plugins.

- **Isolation of parsing logic**: Each parser encapsulates its own logic for extraction and transformation, reducing the risk of regressions in the core.

- **Runtime selection**: The appropriate parser can be loaded at runtime based on the deployed plan format.

The parser's main responsibility is to convert an XML plan into a `fabric::Plan` object, which is the internal representation used by the Fabric execution engine.

## ParserBase Interface

All parser plugins inherit from the `ParserBase` abstract class, which defines the required interface:

- `initialize(const rclcpp::Node::SharedPtr&)`: Prepares the plugin with the ROS2 node context.

- `load_file(const std::string&, fabric::Plan&)`: Loads a plan file and stores its XML text in `plan.plan`.

- `check_compatibility(const fabric::Plan&)`: Performs a lightweight compatibility check for externally submitted plan text.

- `parse(fabric::Plan&)`: Parses the XML text already stored in the `fabric::Plan`.

This ensures that all plugins are interchangeable and can be managed uniformly by the Fabric core.

## XMLParserPlugin: Reference Implementation

The `XMLParserPlugin` is the default parser for standard XML plans. Its key features include:

- **Syntax validation**: Checks for well-formedness, required root tags, and valid control structures.

- **Plan transformation**: Automatically wraps the plan in a top-level sequential control and appends a `fabric_capabilities/FabricCompletionRunner` with the generated `plan_id`.

- **Control flow extraction**: Supports control tags `sequential`, `parallel_any`, `parallel_all`, and `recovery`.

- **Connection extraction**: Recursively traverses the XML tree to build the execution graph (connections) in the `fabric::Plan`.

### Handling Parallelism: `parallel_all` and `parallel_any`

- **`parallel_all`**: Represents a control structure where all child runners must complete before proceeding. The plugin inserts a system runner using `capabilities2_runner/InputMultiplexRunner` with `input_count` set to the number of branches.

- **`parallel_any`**: Represents a control structure where the plan proceeds as soon as any child runner completes. The plugin inserts a system runner using `capabilities2_runner/InputMultiplexRunner` with `input_count=1`.

These are implemented via the `add_parallel_all` and `add_parallel_any` methods, which:
- Count the number of parallel branches.
- Insert a system runner node into the plan's connection graph.
- Update successor nodes to depend on the correct completion condition (all or any).

### Extracting Connections

The parser traverses the XML plan recursively:
- For each `<Control>` element, it determines the type (e.g., `sequential`, `parallel_any`, `parallel_all`) and processes its children accordingly.

- For `<Runner>` elements, it requires `interface` and `provider` attributes, then converts all other XML attributes into typed `EventParameters`.

- System runners for parallelism are inserted as needed, and unique IDs are assigned to each runner for tracking.

- The process ensures that the resulting `fabric::Plan` accurately represents the intended execution flow, including complex parallel and recovery structures.

## Extending for Multiple Plan Versions

To support a new XML plan version or dialect:
1. **Implement a new parser plugin** inheriting from `ParserBase`.

2. Register the plugin so it can be discovered and loaded by Fabric.
3. The core remains unchanged; only the new plugin is added.

This approach allows Fabric to evolve and support new plan formats without invasive changes to the core system.

---

**Summary:**
- The parser plugin system keeps XML parsing format-specific code out of the server.

- The `XMLParserPlugin` demonstrates how to handle control flow, parallelism, and connection extraction.

- New plan formats can be supported by adding new plugins, keeping the core stable and extensible.
