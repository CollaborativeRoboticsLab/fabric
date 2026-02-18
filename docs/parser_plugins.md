# Parser as a Plugin in Fabric

## Motivation and Design

The parser in Fabric is implemented as a plugin to provide maximum flexibility and extensibility for plan interpretation. By decoupling the parsing logic from the core system, Fabric can support multiple versions and dialects of XML-based plans **simultaneously**, without requiring changes to the core codebase. This design allows for:

- **Backward and forward compatibility**: New XML plan formats or schema changes can be supported by simply adding new parser plugins.

- **Isolation of parsing logic**: Each parser encapsulates its own logic for extraction and transformation, reducing the risk of regressions in the core.

- **Runtime selection**: The appropriate parser can be loaded at runtime based on the plan version or user configuration.

The parser's main responsibility is to convert an XML plan into a `fabric::Plan` object, which is the internal representation used by the Fabric execution engine.

## ParserBase Interface

All parser plugins inherit from the `ParserBase` abstract class, which defines the required interface:

- `initialize(const rclcpp::Node::SharedPtr&)`: Prepares the plugin with the ROS2 node context.

- `parse(tinyxml2::XMLDocument&, fabric::Plan&)`: Parses the XML document and populates the `fabric::Plan`.

This ensures that all plugins are interchangeable and can be managed uniformly by the Fabric core.

## XMLParserPlugin: Reference Implementation

The `XMLParserPlugin` is the default parser for standard XML plans. Its key features include:

- **Syntax validation**: Checks for well-formedness, required root tags, and valid control structures.

- **Plan transformation**: Automatically adds a completion runner to ensure all plans have a defined end state.

- **Control flow extraction**: Supports control tags such as `sequential`, `parallel_any`, `parallel_all`, and `recovery`.

- **Connection extraction**: Recursively traverses the XML tree to build the execution graph (connections) in the `fabric::Plan`.

### Handling Parallelism: `parallel_all` and `parallel_any`

- **`parallel_all`**: Represents a control structure where all child runners must complete before proceeding. The plugin adds a system connection using the `InputMultiplexAllRunner`, which waits for all inputs.

- **`parallel_any`**: Represents a control structure where the plan proceeds as soon as any child runner completes. The plugin adds a system connection using the `InputMultiplexAnyRunner`, which waits for any input.

These are implemented via the `add_parallel_all` and `add_parallel_any` methods, which:
- Count the number of parallel branches.
- Insert a system runner node into the plan's connection graph.
- Update successor nodes to depend on the correct completion condition (all or any).

### Extracting Connections

The parser traverses the XML plan recursively:
- For each `<Control>` element, it determines the type (e.g., `sequential`, `parallel_any`, `parallel_all`) and processes its children accordingly.

- For `<Runner>` elements, it creates connection nodes in the `fabric::Plan`.

- System runners for parallelism are inserted as needed, and unique IDs are assigned to each runner for tracking.

- The process ensures that the resulting `fabric::Plan` accurately represents the intended execution flow, including complex parallel and recovery structures.

## Extending for Multiple Plan Versions

To support a new XML plan version or dialect:
1. **Implement a new parser plugin** inheriting from `ParserBase`.

2. Register the plugin so it can be discovered and loaded by Fabric.
3. The core remains unchanged; only the new plugin is added.

This approach allows Fabric to evolve and support new plan formats without risking stability or requiring invasive changes to the core system.

---

**Summary:**
- The parser plugin system enables Fabric to support multiple XML plan versions in parallel.

- The `XMLParserPlugin` demonstrates how to handle control flow, parallelism, and connection extraction.

- New plan formats can be supported by adding new plugins, keeping the core stable and extensible.
