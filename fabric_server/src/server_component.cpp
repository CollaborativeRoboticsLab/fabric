#include <fabric_server/server.hpp>
#include <rclcpp_components/register_node_macro.hpp>

namespace rclcpp_components
{
template<>
class NodeFactoryTemplate<fabric::Fabric>: public NodeFactory
{
  struct Runtime
  {
    explicit Runtime(std::shared_ptr<fabric::Fabric> instance)
    : node(std::move(instance))
    {
      const std::weak_ptr<fabric::Fabric> weak_node = node;
      shutdown_callback = node->get_node_base_interface()->get_context()->add_pre_shutdown_callback(
        [weak_node]() {
          if (const auto instance = weak_node.lock()) {
            instance->shutdownRuntime();
          }
                                });
    }

    ~Runtime()
    {
      node->shutdownRuntime();
      node->get_node_base_interface()->get_context()->remove_pre_shutdown_callback(
          shutdown_callback);
    }

    std::shared_ptr<fabric::Fabric> node;
    rclcpp::PreShutdownCallbackHandle shutdown_callback;
  };

public:
  NodeInstanceWrapper create_node_instance(const rclcpp::NodeOptions & options) override
  {
    auto node = std::make_shared<fabric::Fabric>(options);
    auto runtime = std::make_shared<Runtime>(node);
    node->initialize();
    return NodeInstanceWrapper(runtime, [](const std::shared_ptr<void> & instance) {
               return std::static_pointer_cast<Runtime>(instance)->node->get_node_base_interface();
                        });
  }
};
}  // namespace rclcpp_components

RCLCPP_COMPONENTS_REGISTER_NODE(fabric::Fabric)
