#pragma once

#include <string>

#include <rclcpp/rclcpp.hpp>

namespace fabric
{

/**
 * @brief Base class for data accumulation plugins.
 *
 * Implementations should provide a push() method that pushes new data to the storage.
 */
class StorageBase
{
public:
  virtual ~StorageBase() = default;

  /**
   * @brief Initialize the storage plugin with the given ROS2 node.
   *
   * @param node Shared pointer to the ROS2 node.
   * @return true if initialization was successful, false otherwise.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "StorageBasePlugin");
  }

  /**
   * @brief Push new data to the storage.
   *
   */
  virtual void push() = 0;

protected:
  /**
   * @brief Base initialization method for derived classes.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name of the storage plugin.
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
   * @brief Name of the storage plugin.
   */
  std::string plugin_name_;
};

}  // namespace fabric