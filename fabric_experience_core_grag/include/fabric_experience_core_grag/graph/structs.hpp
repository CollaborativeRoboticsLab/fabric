#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace fabric::experience::graph
{

/**
 * @brief Node information structure.
 * 
 * @param id Unique identifier of the node that bind it with vectordb entry.
 * @param level Level of the node in the graph.
 * @param cluster_id Cluster ID the node belongs to.
 */
struct NodeInfo
{
  std::string id;
  std::string name;
  int level;
  int cluster_id;
  std::vector<double> feature_vector;
};

/**
 * @brief Edge information structure.
 *
 * @param id Unique identifier of the edge that bind it with edge information.
 * @param source Source node ID.
 * @param target Target node ID.
 */
struct EdgeInfo
{
  std::string id;
  std::string source;
  std::string target;

  /**
   * @brief Generate a unique key for the edge based on source and target.
   *
   * @return std::string The generated key.
   */
  std::string generate_key() const
  {
    return source < target ? source + "->" + target : target + "->" + source;
  } 
};

/**
 * @brief Single community structure.
 *
 * @param id Unique identifier of the community that bind it with community information.
 * @param level Level of the community.
 * @param node_ids Vector of node IDs in the community.
 * @param edge_ids Vector of edge IDs in the community.
 * @param occurrence Occurrence metric for the community.
 */
struct CommunityInfo
{
  std::string id;
  int level;
  std::vector<std::string> node_ids;
  std::vector<std::string> edge_ids;
  double occurrence;
};

/**
 * @brief Graph structure containing nodes and edges.
 *
 * @param nodes Map of node IDs to NodeInfo.
 * @param edges Map of edge IDs to EdgeInfo.
 * @param adjacency Adjacency list mapping node IDs to sets of neighboring node IDs.
 * @param communities Map of community levels to CommunityInfo.
 */
struct Graph
{
  std::unordered_map<std::string, NodeInfo> nodes;
  std::unordered_map<std::string, EdgeInfo> edges;
  std::unordered_map<std::string, std::unordered_set<std::string>> adjacency;
  std::unordered_map<std::string, CommunityInfo> communities;
};

}  // namespace fabric::experience::graph