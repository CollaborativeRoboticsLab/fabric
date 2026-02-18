#pragma once
#include <cerrno>
#include <cstdlib>
#include <string>
#include <vector>
#include <tinyxml2.h>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/parser_base.hpp>
#include <capabilities2_events/event_parameters.hpp>

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
   * @brief load a file into an XML document
   *
   * @param document The path to the XML file to be loaded
   * @return true if the file was loaded successfully, false otherwise
   */
  bool load_file(const std::string& file_path, fabric::Plan& plan) override
  {
    tinyxml2::XMLError xml_status = default_document.LoadFile(file_path.c_str());

    // check if the file loading failed
    if (xml_status != tinyxml2::XMLError::XML_SUCCESS)
    {
      RCLCPP_ERROR(node_->get_logger(), "[xml_parser] Error loading plan: %s, Error: %s", file_path.c_str(), default_document.ErrorName());
      return false;
    }

    RCLCPP_INFO(node_->get_logger(), "Plan loaded from : %s", file_path.c_str());

    convert_to_string(default_document, plan.plan);
    RCLCPP_INFO(node_->get_logger(), "Plan converted to string. Content are: \n\n %s", plan.plan.c_str());

    return true;
  }

  /**
   * @brief Check the compatibility of the given plan with the given parser.
   *
   * @param plan The plan to check for compatibility.
   * @return true if the plan is compatible, false otherwise.
   */
  bool check_compatibility(const fabric::Plan& plan) override
  {
    // XML Document to check the validity of the plan
    tinyxml2::XMLDocument documentChecking;

    // try to parse the std::string plan from fabric_msgs/Plan to the to a XMLDocument file
    tinyxml2::XMLError xml_status = documentChecking.Parse(plan.plan.c_str());

    // check if the file parsing failed
    if (xml_status != tinyxml2::XMLError::XML_SUCCESS)
    {
      RCLCPP_ERROR(node_->get_logger(), "Parsing the plan from service request message failed with error: %s", documentChecking.ErrorName());
      return false;
    }

    return true;
  }

  /**
   * @brief Parse the given XML plan.
   *
   * @param document The XMLDocument representing the plan.
   * @param plan The parsed plan.
   */
  void parse(fabric::Plan& plan) override
  {
    // parse the fabric::plan into a XML document
    current_document_.Parse(plan.plan.c_str());

    // Add a completion runner to the plan
    RCLCPP_INFO(node_->get_logger(), "[xml_parser] Adding completion runner to the plan");
    add_completion_runner(current_document_, plan.plan_id);

    // Debug: print the modified plan
    std::string modified_plan;
    convert_to_string(current_document_, modified_plan);
    RCLCPP_DEBUG(node_->get_logger(), "[xml_parser] Plan after adding closing event :\n\n %s", modified_plan.c_str());

    RCLCPP_INFO(node_->get_logger(), "[xml_parser] Completion runner added. Extracting the plan element.");

    // extract the plan element
    plan_ = extract_plan(current_document_);

    if (plan_ == nullptr)
      throw fabric::fabric_exception("No <Plan> element found.");

    RCLCPP_INFO(node_->get_logger(), "[xml_parser] <Plan> element extracted successfully.");

    // check the syntax of the plan to make sure it contains valid XML elements and required attributes
    std::string error_msg;
    std::vector<std::string> control_list = { "sequential", "parallel_any", "parallel_all", "recovery" };

    bool syntax_valid = check_syntax(plan_, control_list, plan.rejected_list, error_msg);

    if (!syntax_valid)
    {
      throw fabric::fabric_exception("XML plan parsing failed: " + error_msg);
    }
    RCLCPP_INFO(node_->get_logger(), "[xml_parser] Plan syntax validation successful");

    // extract connections from the plan
    extract_connections(plan_, plan);

    RCLCPP_INFO(node_->get_logger(), "Finished parsing the plan");
  }

protected:
  /**
   * @brief convert XMLDocument to std::string
   *
   * @param document element to be converted
   *
   * @return std::string converted document
   */
  void convert_to_string(tinyxml2::XMLDocument& document_xml, std::string& document_string)
  {
    tinyxml2::XMLPrinter printer;
    document_xml.Print(&printer);
    document_string = printer.CStr();
  }

  /**
   * @brief search a string in a vector of strings
   *
   * @param list vector of strings to be searched
   * @param value string to be searched in the vector
   *
   * @return `true` if value is found in list and `false` otherwise
   */
  bool search(std::vector<std::string> list, std::string value)
  {
    return (std::find(list.begin(), list.end(), value) != list.end());
  }

  /**
   * @brief Extract the <Plan> element from the XML document
   *
   * This function checks if the root element of the provided XML document is <Plan>.
   * If so, it returns the first child element of <Plan>. Otherwise, it indicates failure.
   *
   * @param document The XMLDocument to extract the plan from
   * @return tinyxml2::XMLElement* Pointer to the first child of <Plan> if successful, nullptr otherwise
   */
  tinyxml2::XMLElement* extract_plan(tinyxml2::XMLDocument& document)
  {
    std::string plan_tag(document.FirstChildElement()->Name());

    if (plan_tag == "Plan")
      return document.FirstChildElement("Plan")->FirstChildElement();
    else
      return nullptr;
  }

  /**
   * @brief convert XMLElement to std::string
   *
   * @param element XMLElement element to be converted
   * @param paramters parameter to hold std::string
   *
   * @return `true` if element is not nullptr and conversion successful, `false` if element is nullptr
   */
  inline bool convert_to_string(tinyxml2::XMLElement* element, std::string& parameters)
  {
    if (element)
    {
      tinyxml2::XMLPrinter printer;
      element->Accept(&printer);
      parameters = printer.CStr();
      return true;
    }
    else
    {
      parameters = "";
      return false;
    }
  }

  /**
   * @brief check the plan to make sure all control and runner XML elements are valid with
   * minimal required attributes. The function uses recursive approach to go through the xml plan
   *
   * @param element XML Element to be evaluated
   * @param control_list list of valid control tags
   * @param rejected list containing invalid tags
   * @param error output string for error messages
   *
   * @return `true` if element valid and supported and `false` otherwise
   */
  bool check_syntax(tinyxml2::XMLElement* element, std::vector<std::string>& control_list, std::vector<std::string>& rejected, std::string& error)
  {
    const char* type = nullptr;
    const char* interface = nullptr;
    const char* provider = nullptr;

    std::string elementTag(element->Name());

    std::string parameter_string;
    convert_to_string(element, parameter_string);

    bool returnValue = true;

    std::string typetag = "";
    std::string interfacetag = "";
    std::string providertag = "";

    bool hasChildren = !element->NoChildren();
    bool hasSiblings = (element->NextSiblingElement() != nullptr);

    if (elementTag == "Control")
    {
      element->QueryStringAttribute("type", &type);

      if (type)
      {
        typetag = type;

        if (!search(control_list, typetag))
        {
          error = "Control tag '" + typetag + "' not available in the valid list";
          rejected.push_back(parameter_string);
          return false;
        }
      }
      else
      {
        error = "Control tag missing 'type' attribute: " + parameter_string;
        rejected.push_back(parameter_string);
        return false;
      }

      if (hasChildren)
        returnValue &= check_syntax(element->FirstChildElement(), control_list, rejected, error);

      if (hasSiblings)
        returnValue &= check_syntax(element->NextSiblingElement(), control_list, rejected, error);
    }
    else if (elementTag == "Runner")
    {
      element->QueryStringAttribute("interface", &interface);
      element->QueryStringAttribute("provider", &provider);

      if (not interface)
      {
        error = "Runner tag missing 'interface' attribute: " + parameter_string;
        rejected.push_back(parameter_string);
        return false;
      }

      if (not provider)
      {
        error = "Runner tag missing 'provider' attribute: " + parameter_string;
        rejected.push_back(parameter_string);
        return false;
      }

      if (hasSiblings)
        returnValue &= check_syntax(element->NextSiblingElement(), control_list, rejected, error);
    }
    else
    {
      error = "XML element is not valid :" + parameter_string;
      rejected.push_back(parameter_string);
      return false;
    }

    return returnValue;
  }

  /**
   * @brief add a completion runner to the plan
   * This function adds a new <Control> element with type "sequential" and a <Runner> element for the completion runner
   * to the existing <Plan> element in the XML document.
   *
   * @param document XML document to which the completion runner will be added
   * @param plan_id ID of the plan to be associated with the completion runner
   */
  void add_completion_runner(tinyxml2::XMLDocument& document, const std::string& plan_id)
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
    newRunner->SetAttribute("interface", "fabric_capabilities/FabricCompletionRunner");
    newRunner->SetAttribute("provider", "fabric_capabilities/FabricCompletionRunner");
    newRunner->SetAttribute("plan_id", plan_id.c_str());
    outerControl->InsertEndChild(newRunner);

    RCLCPP_INFO(node_->get_logger(), "[xml_parser] CompletionRunner added to the plan");
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

    // Since we are adding a parallel all connection, we need to identify the number of parallel runners
    // that need to be multiplexed, which is equivalent to the number of success connections without a
    // target on_success interface in the current plan
    for (const auto& connection : plan.connections)
      if (connection.second.on_success.interface == "")
        input_count += 1;

    fabric::connection node;
    node.source.interface = "capabilities2_runner/InputMultiplexRunner";
    node.source.provider = "capabilities2_runner/InputMultiplexRunner";
    node.source.instance_id = runner_index;
    node.source.parameters.set_value("input_count", input_count, capabilities2_events::OptionType::INT);

    plan.connections[connection_id] = node;
    plan.connections[connection_id].description = description;

    // set the target_on_success for the new connection
    for (auto& connection : plan.connections)
      if (connection.second.on_success.interface == "")
        connection.second.on_success = plan.connections[connection_id].source;

    // increment the index for the next parallel all connection
    runner_index += 1;

    RCLCPP_INFO(node_->get_logger(), "[xml_parser] ParallelAll connection added to the plan with %d inputs, id %d", input_count, connection_id);

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
    // Since we are adding a parallel_any connection, we only need to wait for a single success
    // trigger to succeed, so we can set the input count to 1 for the multiplex runner
    int input_count = 1;

    // create a system runner for parallel any with the identified number of inputs
    fabric::connection node;
    node.source.interface = "capabilities2_runner/InputMultiplexRunner";
    node.source.provider = "capabilities2_runner/InputMultiplexRunner";
    node.source.instance_id = runner_index;
    node.source.parameters.set_value("input_count", input_count, capabilities2_events::OptionType::INT);

    plan.connections[connection_id] = node;
    plan.connections[connection_id].description = description;

    // set the on_success for the new connection
    for (auto& connection : plan.connections)
      if (connection.second.on_success.interface == "")
        connection.second.on_success = plan.connections[connection_id].source;

    // increment the index for the next parallel any connection
    runner_index += 1;

    RCLCPP_INFO(node_->get_logger(), "[xml_parser] ParallelAny connection added to the plan with id %d", connection_id);

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
  // void check_and_update_runner_id(fabric::connection& predecessor, fabric::connection& successor)
  // {
  //   // check if predecessor is a system runner and has parameters
  //   if (predecessor.source.interface.find("capabilities2_runner/InputMultiplexRunner") != std::string::npos)
  //   {
  //     // If the predecessor is a system runner, we need to update the successor's parameters with the predecessor's id
  //     if (predecessor.source.parameters.has_value("id"))
  //     {
  //       // Get the id attribute from the predecessor system runner
  //       int id = std::any_cast<int>(predecessor.source.parameters.get_value("id", 0));

  //       // Set the id attribute for the successor system runner
  //       successor.source.parameters.set_value("id", id, capabilities2_events::OptionType::INT);

  //       RCLCPP_INFO(node_->get_logger(), "[xml_parser] Updated successor runner id to %d", id);
  //     }
  //   }
  // }

  /**
   * @brief convert tinyxml2::XMLElement attributes to EventParameters. Treats the original attributes as string
   * and let the runners that they are related to handle the conversion from string to correct data type
   *
   * @param element tinyxml2::XMLElement element to be converted.
   *
   * @return capabilities2_events::EventParameters converted parameters
   */
  capabilities2_events::EventParameters convert_xml_to_event_parameters(tinyxml2::XMLElement* element)
  {
    capabilities2_events::EventParameters parameters;

    // Iterate through the attributes of the XML element and add them to the EventParameters
    const tinyxml2::XMLAttribute* attr = element->FirstAttribute();

    while (attr)
    {
      // key will always be a string.
      std::string key = attr->Name();
      std::string string_value = attr->Value();

      // skip the interface attribute as it is already used for the connection's source interface
      if (key == "interface")
      {
        attr = attr->Next();
        continue;
      }

      // skip the provider attribute as it is already used for the connection's source provider
      if (key == "provider")
      {
        attr = attr->Next();
        continue;
      }

      // Treat booleans strictly as literal true/false (avoid misclassifying numeric strings like "0.5").
      if (string_value == "true" || string_value == "false")
      {
        const bool bool_value = (string_value == "true");
        RCLCPP_INFO(node_->get_logger(), "[xml_parser] Extracted attribute: %s = %s (bool)", key.c_str(), bool_value ? "true" : "false");
        parameters.set_value(key, bool_value, capabilities2_events::OptionType::BOOL);
      }
      else
      {
        // Prefer int when the value is an integer; otherwise parse as double if fully consumable.
        char* int_end = nullptr;
        errno = 0;
        const long int_value_long = std::strtol(string_value.c_str(), &int_end, 10);

        if (int_end != nullptr && *int_end == '\0' && errno == 0)
        {
          const int int_value = static_cast<int>(int_value_long);
          RCLCPP_INFO(node_->get_logger(), "[xml_parser] Extracted attribute: %s = %d (int)", key.c_str(), int_value);
          parameters.set_value(key, int_value, capabilities2_events::OptionType::INT);
        }
        else
        {
          char* double_end = nullptr;
          errno = 0;
          const double double_value = std::strtod(string_value.c_str(), &double_end);

          if (double_end != nullptr && *double_end == '\0' && errno == 0)
          {
            RCLCPP_INFO(node_->get_logger(), "[xml_parser] Extracted attribute: %s = %f (double)", key.c_str(), double_value);
            parameters.set_value(key, double_value, capabilities2_events::OptionType::DOUBLE);
          }
          else
          {
            RCLCPP_INFO(node_->get_logger(), "[xml_parser] Extracted attribute: %s = %s (string)", key.c_str(), string_value.c_str());
            parameters.set_value(key, string_value, capabilities2_events::OptionType::STRING);
          }
        }
      }

      attr = attr->Next();
    }

    return parameters;
  }

  /**
   * @brief parse through the plan and extract the connections
   *
   * @param element XML Element to be evaluated
   * @param connections std::map containing extracted connections
   */
  int extract_connections(tinyxml2::XMLElement* element, fabric::Plan& plan, int connection_id = 0,
                          fabric::event connection_type = fabric::event::ON_SUCCESS, std::string conn_description = "")
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
          last_conn_id = add_parallel_any(plan, last_conn_id + 1, "parallel_any_for_muxing_outputs");
        }
      }
      else if (typetag == "parallel_all")
      {
        if (hasChildren)
        {
          // extract connections from the first child element
          last_conn_id = extract_connections(element->FirstChildElement(), plan, connection_id, fabric::event::ON_START, description);

          // add a system connection for parallel_all to proceed when all parallel runners are completed
          last_conn_id = add_parallel_all(plan, last_conn_id + 1, "parallel_all_for_muxing_outputs");
        }
      }
      else if (typetag == "recovery")
      {
        if (hasChildren)
        {
          // extract connections from the first child element
          last_conn_id = extract_connections(element->FirstChildElement(), plan, connection_id, fabric::event::ON_FAILURE, description);

          // add a system connection for recovery to proceed when the original runner or at least one recovery runner is completed
          last_conn_id = add_parallel_any(plan, last_conn_id + 1, "parallel_any_for_muxing_recovery");
        }
      }

      if (hasSiblings)
      {
        // continue extracting connections from the next sibling element
        last_conn_id = extract_connections(element->NextSiblingElement(), plan, last_conn_id + 1, connection_type, conn_description);
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

      RCLCPP_INFO(node_->get_logger(), "[xml_parser] Creating fabric connection: interface = %s, provider = %s", interfacetag.c_str(),
                  providertag.c_str());

      fabric::connection connection;

      connection.source.interface = interfacetag;
      connection.source.provider = providertag;
      connection.source.parameters = convert_xml_to_event_parameters(element);
      connection.source.instance_id = runner_index;

      predecessor_id = connection_id - 1;

      plan.connections[connection_id] = connection;
      plan.connections[connection_id].description = conn_description;

      runner_index += 1;

      if (connection_id != 0)
      {
        // if the predecessor is a system runner, we need to update the successor's parameters with the predecessor's id
        // this->check_and_update_runner_id(connections[predecessor_id], connections[connection_id]);

        if (connection_type == fabric::event::ON_SUCCESS)
        {
          // Set the target_on_success for the predecessor connection
          plan.connections[predecessor_id].on_success = plan.connections[connection_id].source;

          RCLCPP_INFO(node_->get_logger(), "[xml_parser] set on_success for id %d to interface %s", predecessor_id,
                      plan.connections[predecessor_id].on_success.interface.c_str());
        }
        else if (connection_type == fabric::event::ON_START)
        {
          // Set the target_on_start for the predecessor connection
          plan.connections[predecessor_id].on_start = plan.connections[connection_id].source;
          RCLCPP_INFO(node_->get_logger(), "[xml_parser] set on_start for id %d to interface %s", predecessor_id,
                      plan.connections[predecessor_id].on_start.interface.c_str());
        }
        else if (connection_type == fabric::event::ON_FAILURE)
        {
          // Set the target_on_failure for the predecessor connection
          plan.connections[predecessor_id].on_failure = plan.connections[connection_id].source;
          RCLCPP_INFO(node_->get_logger(), "[xml_parser] set on_failure for id %d to interface %s", predecessor_id,
                      plan.connections[predecessor_id].on_failure.interface.c_str());
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

  /**
   * @brief Default XML document for loading from file
   */
  tinyxml2::XMLDocument default_document;

  /**
   * @brief XML Document to hold the current plan
   */
  tinyxml2::XMLDocument current_document_;
};
}  // namespace fabric
