#pragma once

#include <atomic>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <utility>

#include <builtin_interfaces/msg/time.hpp>
#include <fabric_base/utils/plan_structs.hpp>
#include <fabric_msgs/srv/get_fabric_plan_snapshot.hpp>
#include <fabric_server/utils/status.hpp>

namespace fabric
{
/** @brief Immutable copy of the current dequeued plan for read-only inspection. */
struct CurrentPlanSnapshot
{
  fabric::plan::Plan plan;
  bool connections_complete{false};
  builtin_interfaces::msg::Time stamp;
};

class CurrentPlanSnapshotStore
{
public:
  std::shared_ptr<const CurrentPlanSnapshot> load() const
  {
    return std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
  }

  void publish(
    const fabric::plan::Plan & plan, bool connections_complete,
    const builtin_interfaces::msg::Time & stamp)
  {
    auto snapshot = std::make_shared<CurrentPlanSnapshot>();
    snapshot->plan = plan;
    snapshot->connections_complete = connections_complete;
    snapshot->stamp = stamp;
    std::atomic_store_explicit(&snapshot_,
        std::shared_ptr<const CurrentPlanSnapshot>(std::move(snapshot)),
                               std::memory_order_release);
  }

private:
  std::shared_ptr<const CurrentPlanSnapshot> snapshot_;
};

namespace server::plan_snapshot
{
inline fabric_msgs::msg::FabricConnection toFabricConnection(
  const fabric::plan::connection & connection)
{
  fabric_msgs::msg::FabricConnection message;
  const auto convert = [](const fabric::plan::node & source, auto & target) {
      target.interface = source.interface;
      target.provider = source.provider;
      target.instance_id = source.instance_id;
    };
  convert(connection.source, message.source);
  convert(connection.on_start, message.on_start);
  convert(connection.on_stop, message.on_stop);
  convert(connection.on_success, message.on_success);
  convert(connection.on_failure, message.on_failure);
  message.description = connection.description;
  return message;
}

inline void populateResponse(
  const fabric::CurrentPlanSnapshot * snapshot,
  fabric_msgs::srv::GetFabricPlanSnapshot::Response & response)
{
  response.has_plan = false;
  response.connections_complete = false;
  response.plan_id.clear();
  response.bond_id.clear();
  response.instances.clear();
  response.connections.clear();
  response.status = fabric_msgs::msg::FabricStatus();
  response.status.code = fabric_msgs::msg::FabricStatus::UNKNOWN;
  response.header.stamp = snapshot ? snapshot->stamp : builtin_interfaces::msg::Time();

  if (!snapshot || snapshot->plan.plan_id.empty()) {
    return;
  }

  response.has_plan = true;
  response.connections_complete = snapshot->connections_complete;
  response.plan_id = snapshot->plan.plan_id;
  response.bond_id = snapshot->plan.bond_id;
  response.status = fabric::server::status::toFabricStatusMsg(snapshot->plan);
  std::set<std::tuple<std::string, std::string, int>> seen_instances;
  const auto append_instance = [&](const fabric::plan::node & node) {
      if (!node.exists()) {
        return;
      }
      const auto identity = std::make_tuple(node.interface, node.provider, node.instance_id);
      if (seen_instances.insert(identity).second) {
        fabric_msgs::msg::FabricNode instance;
        instance.interface = node.interface;
        instance.provider = node.provider;
        instance.instance_id = node.instance_id;
        response.instances.push_back(std::move(instance));
      }
    };
  response.connections.reserve(snapshot->plan.connections.size());
  for (const auto & entry : snapshot->plan.connections) {
    const auto & connection = entry.second;
    append_instance(connection.source);
    append_instance(connection.on_start);
    append_instance(connection.on_stop);
    append_instance(connection.on_success);
    append_instance(connection.on_failure);
    response.connections.push_back(toFabricConnection(connection));
  }
}
}  // namespace server::plan_snapshot
}  // namespace fabric
