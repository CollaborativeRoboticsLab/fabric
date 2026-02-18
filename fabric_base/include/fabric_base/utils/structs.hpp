#pragma once

#include <map>
#include <string>
#include <vector>
#include <capabilities2_events/event_parameters.hpp>

namespace fabric
{

/**
 * @brief Enumeration for different event types in connections
 */
enum event
{
  ON_START,
  ON_SUCCESS,
  ON_FAILURE,
  ON_STOP
};

/**
 * @brief Structure representing a node in the fabric
 */
struct node
{
  std::string interface = "";
  std::string provider = "";
  capabilities2_events::EventParameters parameters;
  int instance_id = -1;

  bool exists() const
  {
    return (!interface.empty() && !provider.empty());
  }
};

/**
 * @brief Structure representing connections between nodes
 */
struct connection
{
  node source;
  node on_start;
  node on_stop;
  node on_success;
  node on_failure;
  std::string description;
};

enum class PlanStatus {
  UNKNOWN,
  QUEUED,
  PARSING,
  PARSE_FAILED,
  VALIDATING,
  VALIDATION_FAILED,
  BONDING,
  BOND_FAILED,
  CAPABILITY_STARTING,
  CAPABILITY_START_FAILED,
  CAPABILITY_CONNECTING,
  CAPABILITY_CONNECT_FAILED,
  RUNNING,
  COMPLETED,
  CANCELLED
};

struct Plan
{
  std::string plan_id;
  std::string plan;
  std::string bond_id;
  std::map<int, connection> connections;
  std::vector<std::string> rejected_list;
  PlanStatus status = PlanStatus::UNKNOWN;
};

struct CapabilityInfo
{
  std::string interface;
  std::string provider;
  bool is_semantic;
  std::vector<std::string> alt_providers;
};

}  // namespace fabric