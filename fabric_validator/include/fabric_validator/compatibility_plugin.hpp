#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <rclcpp/rclcpp.hpp>
#include <fabric_base/validation_base.hpp>

namespace fabric
{
/**
 * @brief Compatibility validation plugin for parsed plans.
 *
 * Checks plan references against resolved runnable interface/provider pairs.
 */
class CompatibilityValidation : public ValidationBase
{
public:
  CompatibilityValidation() = default;
  virtual ~CompatibilityValidation() = default;

  /**
   * @brief Initialize the compatibility validation plugin.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "CompatibilityValidationPlugin");
  }

  /**
   * @brief Validate the given XML plan for compatibility.
   *
   * @param plan The fabric::plan::Plan to validate.
   * @param capabilities Resolved runnable capability descriptors.
   */
  void validate(fabric::plan::Plan& plan, const std::vector<fabric::capability::Descriptor>& capabilities) override
  {
    for (const auto& [id, connection] : plan.connections)
    {
      (void)id;
      const bool source_matched = std::any_of(capabilities.begin(), capabilities.end(), [&](const auto& capability) {
        return connection.source.interface == capability.interface_name && connection.source.provider == capability.provider_name;
      });

      if (!source_matched)
      {
        throw fabric::fabric_exception("Unmatched interface: " + connection.source.interface + " provider: " + connection.source.provider +
                                       " combination");
      }
    }
  }
};
}  // namespace fabric
