#pragma once
#include <string>
#include <vector>
#include <tinyxml2.h>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/validation_base.hpp>
#include <fabric_base/parser_base.hpp>
#include <fabric_base/xml_helper.hpp>
#include <fabric_base/structs.hpp>

namespace fabric
{
/**
 * @brief Syntax validation plugin for XML plans.
 *
 * This plugin checks the XML plan for basic syntax errors (well-formedness, root tag, etc).
 */
class CompatibilityValidation : public ValidationBase
{
public:
  CompatibilityValidation() = default;
  virtual ~CompatibilityValidation() = default;

  /**
   * @brief Initialize the syntax validation plugin.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "CompatibilityValidationPlugin");
  }

  /**
   * @brief Validate the XML plan for syntax errors.
   *
   * @param document The XMLDocument representing the plan.
   * @param error_msg Output string for error messages, if any.
   * @return true if the plan is syntactically valid, false otherwise.
   */
  bool validate(tinyxml2::XMLDocument& document, std::any eval_data) override
  {
    // extract the components within the 'plan' tags
    bool success = false;
    plan = extract_plan(document, success);

    if (!success)
    {
      error_msg = "XML document does not contain a valid <Plan> element.";
      RCLCPP_ERROR(node_->get_logger(), "%s", error_msg.c_str());
    }

    // If we reach here, basic syntax is valid
    return success;
  }

protected:
  /**
   * @brief check the plan for invalid/unsupported control and event tags
   * uses recursive approach to go through the plan
   *
   * @param element XML Element to be evaluated
   * @param rejected list containing invalid tags
   * @param error output string for error messages
   *
   * @return `true` if element valid and supported and `false` otherwise
   */
  bool check_tags(tinyxml2::XMLElement* element, std::vector<std::string>& rejected, std::string& error)
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

      bool foundInControl = search(data_.control_list, typetag);

      if (!foundInControl)
      {
        error = "Control tag '" + typetag + "' not available in the valid list";
        rejected.push_back(parameter_string);
        return false;
      }

      if (hasChildren)
        returnValue &= this->check_tags(element->FirstChildElement(), rejected, error);

      if (hasSiblings)
        returnValue &= this->check_tags(element->NextSiblingElement(), rejected, error);
    }
    else if (elementTag == "Runner")
    {
      element->QueryStringAttribute("interface", &interface);
      element->QueryStringAttribute("provider", &provider);

      if (interface)
        interfacetag = interface;
      if (provider)
        providertag = provider;

      bool foundInRunners = search(data_.interface_list, interfacetag);
      bool foundInProviders = search(data_.provider_list, providertag);

      if (!foundInRunners || !foundInProviders)
      {
        error = "Runner tag interface '" + interfacetag + "' or provider '" + providertag + "' not available in the valid list";
        rejected.push_back(parameter_string);
        return false;
      }

      if (hasSiblings)
        returnValue &= this->check_tags(element->NextSiblingElement(), rejected, error);
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
   * @brief Syntax validation data containing valid tags
   */
  std::vector<CapabilityInfo> data_;

  /**
   * @brief extracted plan element from the XML document
   */
  tinyxml2::XMLElement* plan = nullptr;
};
}  // namespace fabric
