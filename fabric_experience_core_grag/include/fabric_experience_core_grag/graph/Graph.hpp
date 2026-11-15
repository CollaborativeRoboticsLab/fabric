#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <tuple>
#include <optional>
#include <algorithm>
#include <rclcpp/rclcpp.hpp>
#include <fabric_base/utils/exception.hpp>
#include <fabric_experience_core_grag/graph/structs.hpp>

namespace fabric::experience
{

class Graph
{
public:

  /**
   * @brief Construct a new Graph object.
   * 
   * @param node Shared pointer to the ROS2 node.
   */
  Graph(rclcpp::Node::SharedPtr node)
  {
    node_ = node;
  }

  /**
   * @brief Check if a node exists in the graph.
   *
   * @param node_id The ID of the node to check.
   * @return true if the node exists, false otherwise.
   */
  bool has_node(const std::string& node_id) const
  {
    return graph_.nodes.count(node_id) > 0;
  }

  /**
   * @brief Check if an undirected edge exists between two nodes.
   *
   * @param source The source node ID.
   * @param target The target node ID.
   * @return true if the edge exists, false otherwise.
   */
  bool has_edge(const std::string& source, const std::string& target) const
  {
    experience::graph::EdgeInfo edge;
    edge.source = source;
    edge.target = target;
    auto k = edge.generate_key();
    return graph_.edges.count(k) > 0;
  }

  /**
   * @brief Get the degree (number of neighbors) of a node.
   *
   * @param node_id The ID of the node.
   * @return The degree of the node.
   */
  int node_degree(const std::string& node_id) const
  {
    auto it = graph_.adjacency.find(node_id);
    if (it == graph_.adjacency.end())
      return 0;
    return static_cast<int>(it->second.size());
  }

  /**
   * @brief Get the combined degree of an undirected edge (sum of degrees of both nodes).
   *
   * @param source The source node ID.
   * @param target The target node ID.
   * @return The combined degree of the edge.
   */
  int edge_degree(const std::string& source, const std::string& target) const
  {
    return node_degree(source) + node_degree(target);
  }

  /**
   * @brief Retrieve node properties if present.
   *
   * @param node_id The ID of the node.
   * @return Optional map of node properties if the node exists. else std::nullopt.
   */
  std::optional<experience::graph::NodeInfo> get_node(const std::string& node_id) const
  {
    auto it = graph_.nodes.find(node_id);
    if (it == graph_.nodes.end())
      return std::nullopt;
    return it->second;
  }

  /**
   * @brief Retrieve edge properties if present.
   *
   * @param source The source node ID.
   * @param target The target node ID.
   * @return Optional map of edge properties if the edge exists. else std::nullopt.
   */
  std::optional<experience::graph::EdgeInfo> get_edge(const std::string& source, const std::string& target) const
  {
    experience::graph::EdgeInfo edge;
    edge.source = source;
    edge.target = target;
    auto k = edge.generate_key();
    auto it = graph_.edges.find(k);
    if (it == graph_.edges.end())
      return std::nullopt;
    return it->second;
  }

  /**
   * @brief Get all edges connected to a given node.
   *
   * @param node_id The ID of the node.
   * @return Vector of pairs representing edges (source, target).
   */
  std::vector<experience::graph::EdgeInfo> get_node_edges(const std::string& node_id) const
  {
    std::vector<experience::graph::EdgeInfo> out;
    auto it = graph_.adjacency.find(node_id);
    if (it == graph_.adjacency.end())
      return out;
    for (const auto& tgt : it->second)
    {
      experience::graph::EdgeInfo edge;
      edge.source = node_id;
      edge.target = tgt;
      out.emplace_back(edge);
    }
    return out;
  }

  /**
   * @brief Upsert (Update or insert) a node and its properties.
   *
   * @param node_id The ID of the node.
   * @param node_data Map of node properties.
   *
   */
  void upsert_node(const std::string& node_id, const experience::graph::NodeInfo& node_data)
  {
    // Update or insert node
    graph_.nodes[node_id] = node_data;

    // If the node is new, initialize its adjacency set
    if (graph_.adjacency.find(node_id) == graph_.adjacency.end())
      graph_.adjacency.emplace(node_id, std::unordered_set<std::string>{});
  }

  /**
   * * @brief Batch upsert nodes.
   *
   * @param nodes_data Vector of pairs (node_id, node_data) to upsert.
   */
  void upsert_nodes_batch(const std::vector<std::pair<std::string, experience::graph::NodeInfo>>& nodes_data)
  {
    for (const auto& kv : nodes_data)
      upsert_node(kv.first, kv.second);
  }

  /**
   * @brief Upsert (Update or insert) an undirected edge and its properties.
   *
   * @param edge_data Map of edge properties.
   */
  void upsert_edge(experience::graph::EdgeInfo& edge_data)
  {
    // Ensure both nodes exist
    if (!has_node(edge_data.source) || !has_node(edge_data.target))
      throw fabric::fabric_exception("Both nodes must exist before adding an edge.");

    // create edge name
    auto k = edge_data.generate_key();

    // Update or insert edge
    graph_.edges[k] = edge_data;
    graph_.adjacency[edge_data.source].insert(edge_data.target);
    graph_.adjacency[edge_data.target].insert(edge_data.source);
  }

  /**
   * @brief Batch upsert edges.
   *
   * @param edges_data Vector of tuples (source, target, edge_data) to upsert.
   */
  void upsert_edges_batch(std::vector<experience::graph::EdgeInfo>& edges_data)
  {
    for (auto& e : edges_data)
      upsert_edge(e);
  }


protected:
  /**
   * @brief Internal graph representation.
   */
  experience::graph::Graph graph_;

  /**
   * @brief ROS2 node for logging and other operations.
   */
  rclcpp::Node::SharedPtr node_;
};

}  // namespace experience