#pragma once
#include <string>
#include <vector>
#include <rclcpp/rclcpp.hpp>
#include <fabric_base/utils/structs.hpp>
#include <fabric_base/utils/xml_helper.hpp>

namespace fabric
{

/**
 * @brief Abstract base class for plan generation.
 *
 * Implementations should provide a parse() method that extracts information from the XML
 * plan and return a fabric::Plan object.
 */
class GenerationBase
{
public:
  virtual ~GenerationBase() = default;

  /**
   * @brief Initialize the generation plugin with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   * @return true if initialization was successful, false otherwise.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "GenerationBasePlugin");
  }

  /**
   * @brief Check the compatibility of the given plan with the given parser.
   *
   * @param plan The plan to check for compatibility.
   * @return true if the plan is compatible, false otherwise.
   */
  virtual fabric::Plan generate(const std::string& task, const std::string& uuid, bool flush) = 0;

protected:
  /**
   * @brief Base initialization method for derived classes.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name of the generation plugin.
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
   * @brief Name of the generation plugin.
   */
  std::string plugin_name_;
};

}  // namespace fabric
