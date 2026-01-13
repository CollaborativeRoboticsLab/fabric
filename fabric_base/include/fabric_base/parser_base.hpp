#pragma once
#include <string>
#include <vector>
#include <tinyxml2.h>
#include <rclcpp/rclcpp.hpp>
#include <fabric_base/structs.hpp>
#include <fabric_base/xml_helper.hpp>

namespace fabric
{

/**
 * @brief Abstract base class for XML plan parsing plugins.
 *
 * Implementations should provide a parse() method that extracts information from the XML 
 * plan and return a fabric::Plan object.
 */
class ParserBase
{
public:
  virtual ~ParserBase() = default;

  /**
   * @brief Initialize the validation plugin with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   * @return true if initialization was successful, false otherwise.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "ParserBasePlugin");
  }

  /**
   * @brief Parse the given XML plan.
   *
   * @param document The XMLDocument representing the plan.
   * @param plan The parsed plan.
   */
  virtual void parse(tinyxml2::XMLDocument& document, fabric::Plan& plan) = 0;

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
