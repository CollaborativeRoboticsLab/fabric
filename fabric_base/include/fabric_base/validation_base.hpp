#pragma once
#include <any>
#include <string>
#include <vector>
#include <rclcpp/rclcpp.hpp>
#include <fabric_base/utils/structs.hpp>
#include <fabric_base/utils/xml_helper.hpp>
#include <fabric_base/utils/exception.hpp>

namespace fabric
{

/**
 * @brief Abstract base class for XML plan validation plugins.
 *
 * Implementations should provide a validate() method that checks the XML plan for errors.
 */
class ValidationBase
{
public:
  virtual ~ValidationBase() = default;

  /**
   * @brief Initialize the validation plugin with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "ValidationBasePlugin");
  }

  /**
   * @brief Validate the given XML plan.
   *
   * @param plan The fabric::Plan to validate.
   * @param capabilities Support data for evaluation as a vector of fabric::CapabilityInfo.
   */
  virtual void validate(fabric::Plan& plan, std::vector<CapabilityInfo>& capabilities) = 0;

protected:
  /**
   * @brief Base initialization method for derived classes.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name of the validation plugin.
   */
  void initialize_base(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name)
  {
    node_ = node;
    plugin_name_ = plugin_name;
  }

  /**
   * @brief Shared pointer to the ROS2 node.
   */
  rclcpp::Node::SharedPtr node_;

  /**
   * @brief Name of the validation plugin.
   */
  std::string plugin_name_;
};

}  // namespace fabric
