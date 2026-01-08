#pragma once

#include <string>
#include <tinyxml2.h>

namespace Fabric
{

/**
 * @brief Add a system runner to the XML document
 *
 * This function creates a new <Runner> element with the specified interface, provider,
 * input count, and ID, and adds it to the system XML document.
 *
 * @param system_doc The XML document to which the Runner element will be added
 * @param interface The interface attribute for the Runner element
 * @param provider The provider attribute for the Runner element
 * @param input_count The input_count attribute for the Runner element (default is 0)
 * @param id The id attribute for the Runner element (default is 0)
 * @return tinyxml2::XMLElement* Pointer to the newly created Runner element
 */
tinyxml2::XMLElement* add_system_runner_xml(tinyxml2::XMLDocument& system_doc, const std::string& interface, const std::string& provider,
                                            int input_count = 0, int id = 0)
{
  // Create the <Runner .../> element
  tinyxml2::XMLElement* runner = system_doc.NewElement("Runner");
  runner->SetAttribute("interface", interface.c_str());
  runner->SetAttribute("provider", provider.c_str());
  runner->SetAttribute("input_count", input_count);
  runner->SetAttribute("id", id);

  // Add the runner to the system document
  system_doc.InsertEndChild(runner);

  // Return the created <Runner> element
  return runner;
}

}  // namespace Fabric
