#pragma once
#include <string>
#include <vector>
#include <tinyxml2.h>
#include <rclcpp/rclcpp.hpp>
#include <capabilities2_utils/connection.hpp>
#include <event_logger/event_client.hpp>

namespace xml_parser
{

using CType = capabilities2::connection_type_t;

/**
 * @brief extract elements related plan and return the first child element
 *
 * @param document XML document to extract plan from
 * @param success boolean to indicate if the plan was found
 *
 * @return plan in the form of tinyxml2::XMLElement*
 */
tinyxml2::XMLElement* get_plan(tinyxml2::XMLDocument& document, bool& success)
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

void add_closing_event(tinyxml2::XMLDocument& document)
{
  // Get the root <Plan> element
  tinyxml2::XMLElement* plan = document.FirstChildElement("Plan");

  // Get the existing <Control> element inside <Plan>
  tinyxml2::XMLElement* innerControl = plan->FirstChildElement("Control");

  // Create the outer <Control type="sequential"> element
  tinyxml2::XMLElement* outerControl = document.NewElement("Control");
  outerControl->SetAttribute("type", "sequential");
  outerControl->SetAttribute("name", "fabric_completion_control");

  // Clone the existing <Control> element instead of deleting it
  tinyxml2::XMLElement* clonedControl = innerControl->DeepClone(&document)->ToElement();

  // Insert the cloned inner control inside the new outer control
  outerControl->InsertEndChild(clonedControl);

  // Create and append the new <Runner> element
  tinyxml2::XMLElement* newRunner = document.NewElement("Runner");
  newRunner->SetAttribute("interface", "system_capabilities/CompletionRunner");
  newRunner->SetAttribute("provider", "system_capabilities/CompletionRunner");
  outerControl->InsertEndChild(newRunner);

  // Remove the original innerControl (after cloning)
  plan->DeleteChild(innerControl);

  // Append the new outer control to <Plan>
  plan->InsertEndChild(outerControl);
}

/**
 * @brief check the plan for invalid/unsupported control and event tags
 * uses recursive approach to go through the plan
 *
 * @param element XML Element to be evaluated
 * @param events list containing valid event tags
 * @param providers list containing providers
 * @param control list containing valid control tags
 * @param rejected list containing invalid tags
 *
 * @return `true` if element valid and supported and `false` otherwise
 */
bool check_tags(tinyxml2::XMLElement* element, std::vector<std::string>& events, std::vector<std::string>& providers,
                std::vector<std::string>& control, std::vector<std::string>& rejected, std::string& error)
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
      typetag = type;

    bool foundInControl = xml_parser::search(control, typetag);

    if (!foundInControl)
    {
      error = "Control tag '" + typetag + "' not available in the valid list";
      rejected.push_back(parameter_string);
      return false;
    }

    if (hasChildren)
      returnValue &= xml_parser::check_tags(element->FirstChildElement(), events, providers, control, rejected, error);

    if (hasSiblings)
      returnValue &= xml_parser::check_tags(element->NextSiblingElement(), events, providers, control, rejected, error);
  }
  else if (elementTag == "Runner")
  {
    element->QueryStringAttribute("interface", &interface);
    element->QueryStringAttribute("provider", &provider);

    if (interface)
      interfacetag = interface;

    if (provider)
      providertag = provider;

    bool foundInRunners = xml_parser::search(events, interfacetag);
    bool foundInProviders = xml_parser::search(providers, providertag);

    if (!foundInRunners || !foundInProviders)
    {
      error = "Runner tag interface '" + interfacetag + "' or provider '" + providertag + "' not available in the valid list";
      rejected.push_back(parameter_string);
      return false;
    }

    if (hasSiblings)
      returnValue &= xml_parser::check_tags(element->NextSiblingElement(), events, providers, control, rejected, error);
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
 * @brief Returns control xml tags supported in extract_connections method
 *
 */
std::vector<std::string> get_control_list()
{
  std::vector<std::string> tag_list;

  tag_list.push_back("sequential");
  tag_list.push_back("parallel_any");
  tag_list.push_back("parallel_all");
  tag_list.push_back("recovery");

  return tag_list;
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

  node.source.runner = "system_capabilities/InputMultiplexAllRunner";
  node.source.provider = "system_capabilities/InputMultiplexAllRunner";
  node.source.parameters = nullptr;
  node.source.input_count = input_count;

  connections[connection_id] = node;
  connections[connection_id].connection_description = description;

  // set the target_on_success for the new connection
  for (auto& connection : connections)
    if (connection.second.target_on_success.runner == "")
      connection.second.target_on_success = connections[connection_id].source;

  return connection_id;  // return the next connection id
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

  node.source.runner = "system_capabilities/InputMultiplexAnyRunner";
  node.source.provider = "system_capabilities/InputMultiplexAnyRunner";
  node.source.parameters = nullptr;
  node.source.input_count = input_count;

  connections[connection_id] = node;
  connections[connection_id].connection_description = description;

  // set the target_on_success for the new connection
  for (auto& connection : connections)
    if (connection.second.target_on_success.runner == "")
      connection.second.target_on_success = connections[connection_id].source;

  return connection_id;  // return the next connection id
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
  int last_conn_id;

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
        last_conn_id = xml_parser::extract_connections(element->FirstChildElement(), connections, connection_id, CType::ON_SUCCESS, description);
    }
    else if (typetag == "parallel_any")
    {
      if (hasChildren)
      {
        last_conn_id = xml_parser::extract_connections(element->FirstChildElement(), connections, connection_id, CType::ON_START, description);

        // add a system connection for parallel_any to proceed when at least one parallel runner is completed
        last_conn_id = add_parallel_any(connections, last_conn_id + 1, "System capability for collecting inputs from multiple parallel runners");
      }
    }
    else if (typetag == "parallel_all")
    {
      if (hasChildren)
      {
        last_conn_id = xml_parser::extract_connections(element->FirstChildElement(), connections, connection_id, CType::ON_START, description);

        // add a system connection for parallel_all to proceed when all parallel runners are completed
        last_conn_id = add_parallel_all(connections, last_conn_id + 1, "System capability for collecting inputs from multiple parallel runners");
      }
    }
    else if (typetag == "recovery")
    {
      if (hasChildren)
      {
        last_conn_id = xml_parser::extract_connections(element->FirstChildElement(), connections, connection_id, CType::ON_FAILURE, description);

        // add a system connection for recovery to proceed when the original runner or at least one recovery runner is completed
        last_conn_id = add_parallel_any(connections, last_conn_id + 1, "System capability for collecting inputs from multiple recovery runners");
      }
    }

    if (hasSiblings)
    {
      last_conn_id =
          xml_parser::extract_connections(element->NextSiblingElement(), connections, last_conn_id + 1, connection_type, connection_description);
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
    node.source.input_count = 1;
    node.source.parameters = element;

    predecessor_id = connection_id - 1;

    // while (connections.count(connection_id) > 0)
    //   connection_id += 1;

    connections[connection_id] = node;
    connections[connection_id].connection_description = connection_description;

    if (connection_id != 0)
    {
      if (connection_type == CType::ON_SUCCESS)
      {
        connections[predecessor_id].target_on_success = connections[connection_id].source;
      }

      else if (connection_type == CType::ON_START)
      {
        connections[predecessor_id].target_on_start = connections[connection_id].source;
      }

      else if (connection_type == CType::ON_FAILURE)
      {
        connections[predecessor_id].target_on_failure = connections[connection_id].source;
      }
    }

    if (hasSiblings)
      last_conn_id = extract_connections(element->NextSiblingElement(), connections, connection_id + 1, connection_type);
    else
      last_conn_id = predecessor_id + 1;  // connection_id

    return last_conn_id;
  }
}

}  // namespace xml_parser
