#pragma once
#include <string>
#include <vector>
#include <tinyxml2.h>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/validation_base.hpp>

namespace fabric
{
/**
 * @brief Syntax validation plugin for XML plans.
 *
 * This plugin checks the XML plan for basic syntax errors (well-formedness, root tag, etc).
 */
class SyntaxValidation : public ValidationBase
{
public:
  SyntaxValidation() = default;
  virtual ~SyntaxValidation() = default;

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "SyntaxValidationPlugin");

		// Initialize list of valid control tags
		control_list.push_back("sequential");
		control_list.push_back("parallel_any");
		control_list.push_back("parallel_all");
		control_list.push_back("recovery");
  }

  /**
   * @brief Validate the XML plan for syntax errors.
   *
   * @param document The XMLDocument representing the plan.
   * @param error_msg Output string for error messages, if any.
   * @return true if the plan is syntactically valid, false otherwise.
   */
  bool validate(const tinyxml2::XMLDocument& document, std::string& error_msg) override
  {
    // Check for root element
    const tinyxml2::XMLElement* root = document.FirstChildElement();
    if (!root)
    {
      error_msg = "XML document has no root element.";
      return false;
    }
    if (std::string(root->Name()) != "Plan")
    {
      error_msg = "Root element is not <Plan>.";
      return false;
    }
    // Optionally, check for at least one child under <Plan>
    if (!root->FirstChildElement())
    {
      error_msg = "<Plan> element has no child elements.";
      return false;
    }
    // If we reach here, basic syntax is valid
    return true;
  }

protected:
  /**
	 * @brief List of valid control tags
  */
  std::vector<std::string> control_list;
};
}  // namespace fabric
