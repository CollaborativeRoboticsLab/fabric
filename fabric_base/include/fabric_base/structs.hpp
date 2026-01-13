#pragma once

#include <map>
#include <string>
#include <vector>
#include <tinyxml2.h>
#include <fabric_base/xml_helper.hpp>

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
  tinyxml2::XMLElement* parameters = nullptr;

  bool exists()
  {
    return (interface != "" && provider != "" && parameters != nullptr);
  }

  std::string parameter_to_string()
  {
    std::string parameter_string;
    convert_to_string(parameters, parameter_string);
    return parameter_string;
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
  int trigger_id = -1;
};

struct Plan
{
  std::string bond_id;
  std::string plan;
  std::map<int, connection> connections;
  std::vector<std::string> rejected_list;
  bool completed;
};

struct CapabilityInfo
{
  std::string interface;
  std::string provider;
  bool is_semantic;
  std::vector<std::string> alt_providers;
};

}  // namespace fabric