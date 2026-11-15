#include <fabric_server/fabric_server.hpp>
#include <atomic>
#include <chrono>
#include <csignal>

namespace
{
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> shutdown_requested{false};

void requestShutdown(int)
{
  shutdown_requested.store(true, std::memory_order_relaxed);
}
}  // namespace

int main(int argc, char ** argv)
{
  rclcpp::init(argc, argv, rclcpp::InitOptions(), rclcpp::SignalHandlerOptions::None);
  std::signal(SIGINT, requestShutdown);
  std::signal(SIGTERM, requestShutdown);

  // Create the node instance
  auto node = std::make_shared<fabric::Fabric>();

  // Initialize the node components after construction
  node->initialize();

  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);
  auto shutdown_timer = node->create_wall_timer(std::chrono::milliseconds(100), [&executor]() {
        if (shutdown_requested.load(std::memory_order_relaxed)) {
          executor.cancel();
        }
    });
  executor.spin();
  shutdown_timer->cancel();
  shutdown_timer.reset();
  executor.remove_node(node);

  node->shutdownRuntime();
  node.reset();
  rclcpp::shutdown();
  return 0;
}
