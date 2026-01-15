#pragma once

#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <tinyxml2.h>

#include <fabric_base/utils/structs.hpp>
#include <fabric_base/utils/exception.hpp>

namespace fabric
{
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

}  // namespace fabric