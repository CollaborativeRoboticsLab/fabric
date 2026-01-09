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
  std::map<int, connection> connections;
};

struct SyntaxValidationData
{
  std::vector<std::string> control_list;
  std::vector<std::string> interface_list;
  std::vector<std::string> provider_list;
};


}  // namespace fabric