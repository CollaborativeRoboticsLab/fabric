#pragma once

#include <string>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/utils/metadata_structs.hpp>

namespace fabric
{

/**
 * @brief Base class for interface plugins.
 * 
 * Implementations should provide a gather() method that extracts information from the XML
 */
class InterfaceBase
{
public:
  virtual ~InterfaceBase() = default;

  /**
   * @brief Initialize the interface plugin with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   * @return true if initialization was successful, false otherwise.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "InterfaceBasePlugin");
  }

  /**
   * @brief Return the received data since the last call to gather().
   *
   * Implementations should populate the provided vector with capability descriptors.
   */
  virtual void gather() = 0;

protected:
  /**
   * @brief Base initialization method for derived classes.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name of the interface plugin.
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
   * @brief Name of the interface plugin.
   */
  std::string plugin_name_;
};

}  // namespace fabric