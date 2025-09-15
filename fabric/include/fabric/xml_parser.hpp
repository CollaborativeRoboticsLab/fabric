#pragma once
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <tinyxml2.h>
#include <rclcpp/rclcpp.hpp>
#include <capabilities2_utils/connection.hpp>
#include <event_logger/event_client.hpp>

class XMLParser
{
public:
  using CType = capabilities2::connection_type_t;

  /**
   * @brief Constructor for XMLParser
   *
   */
  XMLParser()
  {
    control_list.push_back("sequential");
    control_list.push_back("parallel_any");
    control_list.push_back("parallel_all");
    control_list.push_back("recovery");

    auto* decl = system_doc.NewDeclaration(R"(xml version="1.0" encoding="UTF-8")");
    system_doc.InsertEndChild(decl);
  }

  /**
   * @brief extract elements related plan and return the first child element
   * This function extracts the first child element of the <Plan> tag from the XML document.
   *
   * @param document XML document to extract plan from
   * @param success boolean to indicate if the plan was found
   *
   * @return plan in the form of tinyxml2::XMLElement* if found, otherwise nullptr
   */
  tinyxml2::XMLElement* extract_plan(tinyxml2::XMLDocument& document, bool& success)
  {
    std::string plan_tag(document.FirstChildElement()->Name());

    if (plan_tag == "Plan")
    {
      success = true;
      return document.FirstChildElement("Plan")->FirstChildElement();
    }
    else
    {
      success = false;
      return nullptr;
    }
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
   * @brief convert XMLElement to std::string
   *
   * @param element XMLElement element to be converted
   * @param paramters parameter to hold std::string
   *
   * @return `true` if element is not nullptr and conversion successful, `false` if element is nullptr
   */
  bool convert_to_string(tinyxml2::XMLElement* element, std::string& parameters)
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
   * @brief check the plan for invalid/unsupported control and event tags
   * uses recursive approach to go through the plan
   *
   * @param element XML Element to be evaluated
   * @param events list containing valid event tags
   * @param providers list containing providers
   * @param rejected list containing invalid tags
   *
   * @return `true` if element valid and supported and `false` otherwise
   */
  bool check_tags(tinyxml2::XMLElement* element, std::vector<std::string>& events, std::vector<std::string>& providers,
                  std::vector<std::string>& rejected, std::string& error)
  {
    const char* type = nullptr;
    const char* interface = nullptr;
    const char* provider = nullptr;

    std::string elementTag(element->Name());

    std::string parameter_string;
    this->convert_to_string(element, parameter_string);

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
        typetag = type;

      bool foundInControl = this->search(control_list, typetag);

      if (!foundInControl)
      {
        error = "Control tag '" + typetag + "' not available in the valid list";
        rejected.push_back(parameter_string);
        return false;
      }

      if (hasChildren)
        returnValue &= this->check_tags(element->FirstChildElement(), events, providers, rejected, error);

      if (hasSiblings)
        returnValue &= this->check_tags(element->NextSiblingElement(), events, providers, rejected, error);
    }
    else if (elementTag == "Runner")
    {
      element->QueryStringAttribute("interface", &interface);
      element->QueryStringAttribute("provider", &provider);

      if (interface)
        interfacetag = interface;
      if (provider)
        providertag = provider;

      bool foundInRunners = this->search(events, interfacetag);
      bool foundInProviders = this->search(providers, providertag);

      if (!foundInRunners || !foundInProviders)
      {
        error = "Runner tag interface '" + interfacetag + "' or provider '" + providertag + "' not available in the valid list";
        rejected.push_back(parameter_string);
        return false;
      }

      if (hasSiblings)
        returnValue &= this->check_tags(element->NextSiblingElement(), events, providers, rejected, error);
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
   * @param connections std::map containing connections
   * @param connection_id connection id to be used for the new connection
   * @param description description of the connection
   * @return int next connection id
   */
  int add_parallel_all(std::map<int, capabilities2::node_t>& connections, int connection_id = 0, std::string description = "")
  {
    int input_count = 0;

    // check for parallel connections without success connections to identify number of connections
    for (const auto& connection : connections)
    {
      if (connection.second.target_on_success.runner == "")
        input_count += 1;
    }

    capabilities2::node_t node;

    node.source.runner = "capabilities2_runner_system/InputMultiplexAllRunner";
    node.source.provider = "capabilities2_runner_system/InputMultiplexAllRunner";
    node.source.parameters = this->system_runner_xml(node.source.runner, node.source.provider, input_count, runner_index);

    connections[connection_id] = node;
    connections[connection_id].connection_description = description;
    connections[connection_id].trigger_id = runner_index;

    // set the target_on_success for the new connection
    for (auto& connection : connections)
      if (connection.second.target_on_success.runner == "")
        connection.second.target_on_success = connections[connection_id].source;

    // increment the index for the next parallel all connection
    runner_index += 1;

    // return the next connection id
    return connection_id;
  }

  /**
   * @brief Adds a system connection for parallel input multiplexing with waiting for any inputs
   *
   * @param connections std::map containing connections
   * @param connection_id connection id to be used for the new connection
   * @param description description of the connection
   * @return int next connection id
   */
  int add_parallel_any(std::map<int, capabilities2::node_t>& connections, int connection_id = 0, std::string description = "")
  {
    int input_count = 0;

    // check for parallel connections without success connections to identify number of connections
    for (const auto& connection : connections)
      if (connection.second.target_on_success.runner == "")
        input_count += 1;

    capabilities2::node_t node;

    node.source.runner = "capabilities2_runner_system/InputMultiplexAnyRunner";
    node.source.provider = "capabilities2_runner_system/InputMultiplexAnyRunner";
    node.source.parameters = this->system_runner_xml(node.source.runner, node.source.provider, input_count, runner_index);

    connections[connection_id] = node;
    connections[connection_id].connection_description = description;
    connections[connection_id].trigger_id = runner_index;

    // set the target_on_success for the new connection
    for (auto& connection : connections)
      if (connection.second.target_on_success.runner == "")
        connection.second.target_on_success = connections[connection_id].source;

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
  void check_and_update_runner_id(capabilities2::node_t& predecessor, capabilities2::node_t& successor)
  {
    // check if predecessor is a system runner and has parameters
    if (predecessor.source.runner.find("capabilities2_runner_system/InputMultiplexAnyRunner") != std::string::npos ||
        predecessor.source.runner.find("capabilities2_runner_system/InputMultiplexAllRunner") != std::string::npos)
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
   * @param connection_description the name of the control tag
   */
  int extract_connections(tinyxml2::XMLElement* element, std::map<int, capabilities2::node_t>& connections, int connection_id = 0,
                          CType connection_type = CType::ON_SUCCESS, std::string connection_description = "")
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
          last_conn_id = this->extract_connections(element->FirstChildElement(), connections, connection_id, CType::ON_SUCCESS, description);
      }
      else if (typetag == "parallel_any")
      {
        if (hasChildren)
        {
          // extract connections from the first child element
          last_conn_id = this->extract_connections(element->FirstChildElement(), connections, connection_id, CType::ON_START, description);

          // add a system connection for parallel_any to proceed when at least one parallel runner is completed
          last_conn_id = this->add_parallel_any(connections, last_conn_id + 1, "parallel_any_for_collecting_outputs");
        }
      }
      else if (typetag == "parallel_all")
      {
        if (hasChildren)
        {
          // extract connections from the first child element
          last_conn_id = this->extract_connections(element->FirstChildElement(), connections, connection_id, CType::ON_START, description);

          // add a system connection for parallel_all to proceed when all parallel runners are completed
          last_conn_id = this->add_parallel_all(connections, last_conn_id + 1, "parallel_all_for_collecting_outputs");
        }
      }
      else if (typetag == "recovery")
      {
        if (hasChildren)
        {
          // extract connections from the first child element
          last_conn_id = this->extract_connections(element->FirstChildElement(), connections, connection_id, CType::ON_FAILURE, description);

          // add a system connection for recovery to proceed when the original runner or at least one recovery runner is completed
          last_conn_id = this->add_parallel_any(connections, last_conn_id + 1, "parallel_any_for_collecting_recovery");
        }
      }

      if (hasSiblings)
      {
        // continue extracting connections from the next sibling element
        last_conn_id =
            this->extract_connections(element->NextSiblingElement(), connections, last_conn_id + 1, connection_type, connection_description);
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

      capabilities2::node_t node;

      node.source.runner = interfacetag;
      node.source.provider = providertag;
      node.source.parameters = element;

      // set runner id unique identifier
      node.source.parameters->SetAttribute("id", runner_index);

      predecessor_id = connection_id - 1;

      connections[connection_id] = node;
      connections[connection_id].connection_description = connection_description;

      // match the trigger id with the runner index
      connections[connection_id].trigger_id = runner_index;

      runner_index += 1;

      if (connection_id != 0)
      {
        // if the predecessor is a system runner, we need to update the successor's parameters with the predecessor's id
        // this->check_and_update_runner_id(connections[predecessor_id], connections[connection_id]);

        if (connection_type == CType::ON_SUCCESS)
        {
          // Set the target_on_success for the predecessor connection
          connections[predecessor_id].target_on_success = connections[connection_id].source;
        }
        else if (connection_type == CType::ON_START)
        {
          // Set the target_on_start for the predecessor connection
          connections[predecessor_id].target_on_start = connections[connection_id].source;
        }
        else if (connection_type == CType::ON_FAILURE)
        {
          // Set the target_on_failure for the predecessor connection
          connections[predecessor_id].target_on_failure = connections[connection_id].source;
        }
      }

      if (hasSiblings)
        last_conn_id = this->extract_connections(element->NextSiblingElement(), connections, connection_id + 1, connection_type);
      else
        last_conn_id = connection_id;

      return last_conn_id;
    }

    // Fallback if an unexpected tag appears: return the current id unchanged
    return last_conn_id;
  }

private:
  // List of valid control tags
  std::vector<std::string> control_list;

  // System xml document
  tinyxml2::XMLDocument system_doc;

  // Unique index for runners
  int runner_index = 0;
};
