#pragma once

#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <fabric_base/utils/metadata_structs.hpp>

namespace fabric
{

/**
 * @brief Base class for capability metadata ingestion plugins.
 */
class MetadataBase
{
public:
  virtual ~MetadataBase() = default;

  /**
   * @brief Initialize the metadata plugin with the owning ROS node.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node, "MetadataBasePlugin");
  }

  /**
   * @brief Collect metadata records from the backing system.
   * 
   * Implementations should populate the provided vector with capability descriptors.
   * 
   * @param capabilities Vector to be populated with capability descriptors.
   */
  virtual void collect(std::vector<fabric::capability::Descriptor>& capabilities) = 0;

  /**
   * @brief Wait for the backing system to become ready.
   * 
   * Implementations should block until the backing system is ready or the timeout is reached.
   * 
   * @param timeout_sec Maximum time to wait in seconds. If negative, wait indefinitely.
   */
  virtual void wait_until_ready(int timeout_sec = -1)
  {
    (void)timeout_sec;
  }

protected:
  /**
   * @brief Store the shared node handle and plugin name for derived metadata plugins.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name registered for the metadata plugin.
   */
  void initialize_base(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name)
  {
    node_ = node;
    plugin_name_ = plugin_name;
  }

  rclcpp::Node::SharedPtr node_;
  std::string plugin_name_;
};

}  // namespace fabric