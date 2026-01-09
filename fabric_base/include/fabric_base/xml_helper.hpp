#pragma once

#include <map>
#include <vector>
#include <algorithm>
#include <string>
#include <tinyxml2.h>

#include <fabric_base/structs.hpp>

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
 * @param success Output boolean indicating whether extraction was successful
 * @return tinyxml2::XMLElement* Pointer to the first child of <Plan> if successful, nullptr otherwise
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

}  // namespace fabric