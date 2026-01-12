#pragma once
#include <string>
#include <vector>
#include <tinyxml2.h>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/parser_base.hpp>
#include <fabric_base/xml_helper.hpp>

namespace fabric
{
/**
 * @brief Syntax validation plugin for XML plans.
 *
 * This plugin checks the XML plan for basic syntax errors (well-formedness, root tag, etc).
 */
class XMLParser : public ParserBase
{
public:
  XMLParser() = default;
  virtual ~XMLParser() = default;

  /**
   * @brief Initialize the XML parser plugin.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "XMLParserPlugin");

    auto* decl = system_doc.NewDeclaration(R"(xml version="1.0" encoding="UTF-8")");
    system_doc.InsertEndChild(decl);
  }

  /**
   * @brief Parse the given XML plan.
   *
   * @param document The XMLDocument representing the plan.
   * @param plan The parsed plan.
   */
  void parse(tinyxml2::XMLDocument& document, fabric::Plan& plan) override
  {
    // Add a completion runner to the plan
    RCLCPP_INFO(node_->get_logger(), "Adding completion runner to the plan");
    add_completion_runner(document);

    // Debug: print the modified plan
    std::string modified_plan;
    convert_to_string(document, modified_plan);
    RCLCPP_DEBUG(node_->get_logger(), "Plan after adding closing event :\n\n %s", modified_plan.c_str());

    RCLCPP_INFO(node_->get_logger(), "Completion runner added successfully. Extracting the plan element.");

    // extract the plan element
    plan_ = extract_plan(document);

    if (plan_ == nullptr)
    {
      RCLCPP_ERROR(node_->get_logger(), "No <Plan> element found in the provided plan.");
      throw fabric::fabric_exception("XML plan parsing failed: No <Plan> element found.");
    }
    RCLCPP_INFO(node_->get_logger(), "<Plan> element extracted successfully. Checking required attributes availability.");

    // check the syntax of the plan to make sure it contains valid XML elements and required attributes
    std::string error_msg;
    std::vector<std::string> control_list = { "sequential", "parallel_any", "parallel_all", "recovery" };

    bool syntax_valid = check_syntax(plan_, control_list, plan.rejected_list, error_msg);

    if (!syntax_valid)
    {
      RCLCPP_ERROR(node_->get_logger(), "Plan syntax validation failed: %s", error_msg.c_str());

      for (const auto& rejected_element : plan.rejected_list)
        RCLCPP_ERROR(node_->get_logger(), "Rejected element: %s", rejected_element.c_str());

      throw fabric::fabric_exception("XML plan parsing failed: " + error_msg);
    }
    RCLCPP_INFO(node_->get_logger(), "Plan syntax validation successful. Proceeding to capability retrieval.");

    // extract connections from the plan
    if (plan_ != nullptr)
    {
      extract_connections(plan_, plan);
      RCLCPP_INFO(node_->get_logger(), "Finished parsing the plan");
    }
    else
    {
      RCLCPP_ERROR(node_->get_logger(), "No <Plan> element found in the provided plan.");
      throw fabric::fabric_exception("XML plan parsing failed: No <Plan> element found.");
    }
  }

protected:
  /**
   * @brief add a completion runner to the plan
   * This function adds a new <Control> element with type "sequential" and a <Runner> element for the completion runner
   * to the existing <Plan> element in the XML document.
   *
   * @param document XML document to which the completion runner will be added
   */
  void add_completion_runner(tinyxml2::XMLDocument& document)
  {
    // Get the root <Plan> element
    tinyxml2::XMLElement* plan = document.FirstChildElement("Plan");

    // Create the outer <Control type="sequential"> element
    tinyxml2::XMLElement* outerControl = document.NewElement("Control");
    outerControl->SetAttribute("type", "sequential");
    outerControl->SetAttribute("name", "fabric_completion_control");

    // Move all existing children of <Plan> into the new outer control
    while (tinyxml2::XMLNode* child = plan->FirstChild())
      outerControl->InsertEndChild(child);

    // Add the new outer control to the <Plan>
    plan->InsertEndChild(outerControl);

    // Create and append the new <Runner> element
    tinyxml2::XMLElement* newRunner = document.NewElement("Runner");
    newRunner->SetAttribute("interface", "capabilities2_runner_fabric/FabricCompletionRunner");
    newRunner->SetAttribute("provider", "capabilities2_runner_fabric/FabricCompletionRunner");
    outerControl->InsertEndChild(newRunner);
  }

  /**
   * @brief Build a system runner document
   *
   * This function initializes the system XML document and adds a <Runner> element with the specified attributes.
   *
   * @param interface The interface of the runner
   * @param provider The provider of the runner
   * @param input_count The number of inputs for the runner
   * @param id The ID of the runner
   */
  tinyxml2::XMLElement* system_runner_xml(const std::string& interface, const std::string& provider, int input_count = 0, int id = 0)
  {
    // Create the <Runner .../> element
    tinyxml2::XMLElement* runner = system_doc.NewElement("Runner");
    runner->SetAttribute("interface", interface.c_str());
    runner->SetAttribute("provider", provider.c_str());
    runner->SetAttribute("input_count", input_count);
    runner->SetAttribute("id", id);

    system_doc.InsertEndChild(runner);

    // Return the created <Runner> element
    return runner;
  }

  /**
   * @brief Adds a system connection for parallel input multiplexing with waiting for all inputs
   *
   * @param connections fabric::Plan containing connections
   * @param connection_id connection id to be used for the new connection
   * @param description description of the connection
   * @return int next connection id
   */
  int add_parallel_all(fabric::Plan& plan, int connection_id = 0, std::string description = "")
  {
    int input_count = 0;

    // check for parallel connections without success connections to identify number of connections
    for (const auto& connection : plan.connections)
    {
      if (connection.second.target_on_success.interface == "")
        input_count += 1;
    }

    fabric::connection node;

    node.source.interface = "capabilities2_runner_system/InputMultiplexAllRunner";
    node.source.provider = "capabilities2_runner_system/InputMultiplexAllRunner";
    node.source.parameters = this->system_runner_xml(node.source.interface, node.source.provider, input_count, runner_index);

    plan.connections[connection_id] = node;
    plan.connections[connection_id].description = description;
    plan.connections[connection_id].trigger_id = runner_index;

    // set the target_on_success for the new connection
    for (auto& connection : plan.connections)
      if (connection.second.target_on_success.interface == "")
        connection.second.target_on_success = plan.connections[connection_id].source;

    // increment the index for the next parallel all connection
    runner_index += 1;

    // return the next connection id
    return connection_id;
  }

  /**
   * @brief Adds a system connection for parallel input multiplexing with waiting for any inputs
   *
   * @param plan fabric::Plan containing connections
   * @param connection_id connection id to be used for the new connection
   * @param description description of the connection
   * @return int next connection id
   */
  int add_parallel_any(fabric::Plan& plan, int connection_id = 0, std::string description = "")
  {
    int input_count = 0;

    // check for parallel connections without success connections to identify number of connections
    for (const auto& connection : plan.connections)
      if (connection.second.target_on_success.interface == "")
        input_count += 1;

    fabric::connection node;

    node.source.interface = "capabilities2_runner_system/InputMultiplexAnyRunner";
    node.source.provider = "capabilities2_runner_system/InputMultiplexAnyRunner";
    node.source.parameters = this->system_runner_xml(node.source.interface, node.source.provider, input_count, runner_index);

    plan.connections[connection_id] = node;
    plan.connections[connection_id].description = description;
    plan.connections[connection_id].trigger_id = runner_index;

    // set the target_on_success for the new connection
    for (auto& connection : plan.connections)
      if (connection.second.target_on_success.interface == "")
        connection.second.target_on_success = plan.connections[connection_id].source;

    // increment the index for the next parallel any connection
    runner_index += 1;

    // return the next connection id
    return connection_id;
  }

  /**
   * @brief Check and update the system runner id for the successor based on the predecessor's id
   *
   * This function checks if the predecessor is a system runner and has parameters.
   * If so, it updates the successor's parameters with the predecessor's id.
   *
   * @param predecessor The predecessor node_t containing the source runner
   * @param successor The successor node_t to be updated
   */
  void check_and_update_runner_id(fabric::connection& predecessor, fabric::connection& successor)
  {
    // check if predecessor is a system runner and has parameters
    if (predecessor.source.interface.find("capabilities2_runner_system/InputMultiplexAnyRunner") != std::string::npos ||
        predecessor.source.interface.find("capabilities2_runner_system/InputMultiplexAllRunner") != std::string::npos)
    {
      // If the predecessor is a system runner, we need to update the successor's parameters with the predecessor's id
      if (predecessor.source.parameters)
      {
        // Get the id attribute from the predecessor system runner
        const char* id = nullptr;
        id = predecessor.source.parameters->Attribute("id");

        // Set the id attribute for the successor system runner
        successor.source.parameters->SetAttribute("id", id);
      }
    }
  }

  /**
   * @brief parse through the plan and extract the connections
   *
   * @param element XML Element to be evaluated
   * @param connections std::map containing extracted connections
   * @param connection_id numerical id of the connection
   * @param connection_type the type of connection
   * @param description the name of the control tag
   */
  int extract_connections(tinyxml2::XMLElement* element, fabric::Plan& plan, int connection_id = 0,
                          fabric::event connection_type = fabric::event::ON_SUCCESS, std::string description = "")
  {
    int predecessor_id;
    int last_conn_id = connection_id;

    const char* type = nullptr;
    const char* name = nullptr;
    const char* interface = nullptr;
    const char* provider = nullptr;

    std::string elementTag(element->Name());

    std::string typetag = "";
    std::string description = "";
    std::string interfacetag = "";
    std::string providertag = "";

    bool hasChildren = (element->FirstChildElement() != nullptr);
    bool hasSiblings = (element->NextSiblingElement() != nullptr);

    if (elementTag == "Control")
    {
      type = element->Attribute("type");
      name = element->Attribute("name");

      if (type)
        typetag = type;

      if (name)
        description = name;

      if (typetag == "sequential")
      {
        if (hasChildren)
          last_conn_id = extract_connections(element->FirstChildElement(), plan, connection_id, fabric::event::ON_SUCCESS, description);
      }
      else if (typetag == "parallel_any")
      {
        if (hasChildren)
        {
          // extract plan.connections from the first child element
          last_conn_id = extract_connections(element->FirstChildElement(), plan, connection_id, fabric::event::ON_START, description);

          // add a system connection for parallel_any to proceed when at least one parallel runner is completed
          last_conn_id = add_parallel_any(plan, last_conn_id + 1, "parallel_any_for_collecting_outputs");
        }
      }
      else if (typetag == "parallel_all")
      {
        if (hasChildren)
        {
          // extract connections from the first child element
          last_conn_id = extract_connections(element->FirstChildElement(), plan, connection_id, fabric::event::ON_START, description);

          // add a system connection for parallel_all to proceed when all parallel runners are completed
          last_conn_id = add_parallel_all(plan, last_conn_id + 1, "parallel_all_for_collecting_outputs");
        }
      }
      else if (typetag == "recovery")
      {
        if (hasChildren)
        {
          // extract connections from the first child element
          last_conn_id = extract_connections(element->FirstChildElement(), plan, connection_id, fabric::event::ON_FAILURE, description);

          // add a system connection for recovery to proceed when the original runner or at least one recovery runner is completed
          last_conn_id = add_parallel_any(plan, last_conn_id + 1, "parallel_any_for_collecting_recovery");
        }
      }

      if (hasSiblings)
      {
        // continue extracting connections from the next sibling element
        last_conn_id = extract_connections(element->NextSiblingElement(), plan, last_conn_id + 1, connection_type, description);
      }

      return last_conn_id;
    }
    else if (elementTag == "Runner")
    {
      interface = element->Attribute("interface");
      provider = element->Attribute("provider");

      if (interface)
        interfacetag = interface;

      if (provider)
        providertag = provider;

      fabric::connection connection;

      connection.source.interface = interfacetag;
      connection.source.provider = providertag;
      connection.source.parameters = element;

      // set runner id unique identifier
      connection.source.parameters->SetAttribute("id", runner_index);

      predecessor_id = connection_id - 1;

      plan.connections[connection_id] = connection;
      plan.connections[connection_id].description = description;

      // match the trigger id with the runner index
      plan.connections[connection_id].trigger_id = runner_index;

      runner_index += 1;

      if (connection_id != 0)
      {
        // if the predecessor is a system runner, we need to update the successor's parameters with the predecessor's id
        // this->check_and_update_runner_id(connections[predecessor_id], connections[connection_id]);

        if (connection_type == fabric::event::ON_SUCCESS)
        {
          // Set the target_on_success for the predecessor connection
          plan.connections[predecessor_id].on_success = plan.connections[connection_id].source;
        }
        else if (connection_type == fabric::event::ON_START)
        {
          // Set the target_on_start for the predecessor connection
          plan.connections[predecessor_id].on_start = plan.connections[connection_id].source;
        }
        else if (connection_type == fabric::event::ON_FAILURE)
        {
          // Set the target_on_failure for the predecessor connection
          plan.connections[predecessor_id].on_failure = plan.connections[connection_id].source;
        }
      }

      if (hasSiblings)
        last_conn_id = this->extract_connections(element->NextSiblingElement(), plan, connection_id + 1, connection_type);
      else
        last_conn_id = connection_id;

      return last_conn_id;
    }

    // Fallback if an unexpected tag appears: return the current id unchanged
    return last_conn_id;
  }

  /**
   * @brief List of valid control tags
   */
  std::vector<std::string> control_list;

  /**
   * @brief XML Document for system runners
   */
  tinyxml2::XMLDocument system_doc;

  /**
   * @brief Index for assigning unique IDs to system runners
   */
  int runner_index = 0;

  /**
   * @brief Pointer to the plan element in the XML document
   */
  tinyxml2::XMLElement* plan_ = nullptr;
};
}  // namespace fabric
