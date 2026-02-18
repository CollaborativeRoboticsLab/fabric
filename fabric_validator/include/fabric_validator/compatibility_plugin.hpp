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
   * @brief Validate the given XML plan for compatibility.
   *
   * @param plan The fabric::Plan to validate.
   * @param capabilities Support data for evaluation as a vector of fabric::CapabilityInfo.
   */
  void validate(fabric::Plan& plan, std::vector<CapabilityInfo>& capabilities) override
  {
    // Iterate through the plan and check whether each connection's interface and provider are matched in
    // the provided CapabilityInfo data. If a provider is not matched, it could be in alternative providers.

    for (const auto& [id, connection] : plan.connections)
    {
      bool source_matched = false;

      // Check source node
      for (const auto& cap : capabilities)
      {
        if (connection.source.interface == cap.interface)
        {
          if (connection.source.provider == cap.provider)
          {
            source_matched = true;
            break;
          }
          else
          {
            // Check alternative providers
            if (std::find(cap.alt_providers.begin(), cap.alt_providers.end(), connection.source.provider) != cap.alt_providers.end())
            {
              source_matched = true;
              break;
            }
          }
        }
      }

      if (!source_matched)
      {
        throw fabric::fabric_exception("Unmatched interface: " + connection.source.interface + " provider: " + connection.source.provider +
                                       " combination");
      }
    }
  }
};
}  // namespace fabric
