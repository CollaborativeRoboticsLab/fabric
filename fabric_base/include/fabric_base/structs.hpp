#pragma once

#include <map>
#include <string>
#include <tinyxml2.h>

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
  std::string runner = "";
  std::string provider = "";
  tinyxml2::XMLElement* parameters = nullptr;
};

/**
 * @brief Structure representing connections between nodes
 */
struct connection
{
  node source;
  node target_on_start;
  node target_on_stop;
  node target_on_success;
  node target_on_failure;
  std::string description;
  int trigger_id = -1;
};

struct Plan
{
  std::string bond_id;
  std::string plan;
  std::map<int, connection> connections;
  std::vector<std::string> rejected_list;
};

struct CapabilityInfo
{
  std::string interface;
  std::string provider;
  bool has_semantic;
  std::vector<std::string> semantic_interfaces;
  std::vector<std::string> alt_providers;
};

}  // namespace fabric