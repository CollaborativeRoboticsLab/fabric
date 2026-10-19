#pragma once

#include <string>
#include <any>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <filesystem>
#include <atomic>

#include <fabric_base/llm_analysis_base.hpp>
#include <fabric_base/utils/exception.hpp>
#include <rclcpp/rclcpp.hpp>

namespace fabric
{

class MonitorBase
{
public:
  virtual ~MonitorBase() = default;

  /**
   * @brief Initialize the monitoring plugin
   *
   * This function should be overridden in derived classes to provide specific initialization.
   *
   * @param node Shared pointer to the ROS node
   */
  virtual void initialize(const rclcpp::Node::SharedPtr& node)
  {
    initialize_base(node);
  }

  /**
   * @brief Start the monitoring plugin
   *
   */
  virtual void start() = 0;

  /**
   * @brief Stop the monitoring plugin
   *
   */
  virtual void stop() = 0;

  /**
   * @brief Get latest data from the plugin
   *
   * This function should be overridden in derived classes to provide specific data.
   *
   * @return std::any The latest data from the plugin.
   * @throws fabric_exception if not implemented in derived classes
   */
  virtual std::any getData()
  {
    throw fabric_exception("getData() not implemented for this plugin.");
  }

  /**
   * @brief Get the name of the driver
   *
   * @return std::string The name of the driver
   */
  virtual std::string getName() const
  {
    return plugin_name_;
  }

  virtual void configureAnalysis(const std::shared_ptr<LlmAnalysisBase>& analysis_plugin)
  {
    analysis_plugin_ = analysis_plugin;
  }

protected:
  /**
   * @brief Monitoring function. need to be overridden in derived classes if used
   */
  virtual void monitoring_function()
  {
    // This is a placeholder for the monitor thread logic.
  }

  /**
   * @brief Initializer base driver in place of constructor due to plugin semantics
   *
   * @param node shared pointer to the ROS node.
   */
  void initialize_base(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name = "Base")
  {
    node_ = node;
    plugin_name_ = plugin_name;

    node->declare_parameter(plugin_name_ + ".topic", "");
    node->declare_parameter(plugin_name_ + ".frequency", 1.0);

    monitor_topic_ = node->get_parameter(plugin_name_ + ".topic").as_string();
    monitor_frequency_ = node->get_parameter(plugin_name_ + ".frequency").as_double();

    RCLCPP_INFO(node_->get_logger(), "Monitor plugin name: %s", plugin_name_.c_str());
    RCLCPP_INFO(node_->get_logger(), "Monitoring topic: %s", monitor_topic_.c_str());
    RCLCPP_INFO(node_->get_logger(), "Monitoring frequency: %.2f Hz", monitor_frequency_);
  }

  /**
   * @brief Name of the plugin
   */
  std::string plugin_name_;

  /**
   * @brief Topic name for monitoring data
   */
  std::string monitor_topic_;

  /**
   * @brief ROS node for the driver
   */
  rclcpp::Node::SharedPtr node_;

  /**
   * @brief Optional shared LLM-analysis plugin available to monitors.
   */
  std::shared_ptr<LlmAnalysisBase> analysis_plugin_;

  /**
   * @brief Mutex to protect access to the camera data
   */
  std::mutex buffer_mutex_;

  /**
   * @brief Condition variable to notify when new data is available
   */
  std::condition_variable buffer_cv_;

  /**
   * @brief Monitoring frequency in Hz
   */
  double monitor_frequency_{ 1.0 };

  /**
   * @brief Atomic flag to control the monitoring thread
   */
  std::atomic<bool> monitoring_active_{ false };

  /**
   * @brief Thread for monitoring
   */
  std::thread monitor_thread_;
};

}  // namespace fabric