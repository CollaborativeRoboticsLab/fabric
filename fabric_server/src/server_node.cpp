#include <fabric_server/server.hpp>

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);

  // Create the node instance
  auto node = std::make_shared<fabric::Fabric>();

  // Initialize the node components after construction
  node->initialize();
  
  rclcpp::spin(node);
  
  rclcpp::shutdown();
  return 0;
}