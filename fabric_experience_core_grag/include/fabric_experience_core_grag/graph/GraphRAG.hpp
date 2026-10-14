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
#include <fabric_experience_core_grag/graph/clustering.hpp>
#include <fabric_experience_core_grag/graph/Graph.hpp>

namespace experience
{

class GraphRAG : public Graph
{
public:
  /**
   * @brief Construct a graph-RAG helper around the shared ROS node.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  GraphRAG(rclcpp::Node::SharedPtr node) : Graph(node)
  {
    // Initialize GraphRAG level parameters
    node_->declare_parameter("graph_rag.clustering_algorithm", "KMEANS");
    node_->declare_parameter("graph_rag.num_clusters", 5);

    node_->get_parameter("graph_rag.clustering_algorithm", algorithm_);
    node_->get_parameter("graph_rag.num_clusters", clusters_);
  }

  /**
   * @brief Perform clustering and annotate nodes.
   *
   * @param algorithm The clustering algorithm to use. Available options are BFS, KMEANS, and SPECTRAL.
   * @param k Number of clusters (used for KMEANS and SPECTRAL).
   */
  void clustering(const experience::clustering::Algorithm& algorithm, int k = 3)
  {
    RCLCPP_INFO(node_->get_logger(), "Starting clustering with algorithm %d and k=%d.", static_cast<int>(algorithm), k);

    switch (algorithm)
    {
      case experience::clustering::Algorithm::BFS:
        experience::clustering::bfs_clustering(graph_);
        break;
      case experience::clustering::Algorithm::KMEANS:
        experience::clustering::kmeans_clustering(graph_, k);
        break;
      case experience::clustering::Algorithm::SPECTRAL:
        experience::clustering::spectral_clustering(graph_, k);
        break;
      default:
        throw fabric::fabric_exception("Unsupported clustering algorithm.");
    }
  }

  /**
   * @brief Construct a simple community schema by grouping nodes by cluster id.
   *
   * @param clustering_needed Whether to perform clustering before community creation.
   * @return std::unordered_map<std::string, experience::graph::CommunityInfo> The constructed communities.
   *
   * Edges are canonicalized and uniqued; `occurrence` is derived from chunk_ids
   * count if present.
   */

  std::unordered_map<std::string, experience::graph::CommunityInfo> create_communities(bool clustering_needed = true)
  {
    if (clustering_needed)
    {
      experience::clustering::Algorithm algorithm;

      if (algorithm_ == "BFS")
        algorithm = experience::clustering::Algorithm::BFS;
      else if (algorithm_ == "KMEANS")
        algorithm = experience::clustering::Algorithm::KMEANS;
      else if (algorithm_ == "SPECTRAL")
        algorithm = experience::clustering::Algorithm::SPECTRAL;
      else
      {
        RCLCPP_WARN(node_->get_logger(), "[GraphRAG] Unsupported '%s', defaulting to KMEANS.", algorithm_.c_str());
        algorithm = experience::clustering::Algorithm::KMEANS;
      }

      clustering(algorithm, clusters_);
    }

    // Build communities from node cluster_id
    std::unordered_map<std::string, experience::graph::CommunityInfo> out;

    for (const auto& n : graph_.nodes)
    {
      std::string key = std::to_string(n.second.cluster_id);
      auto& comm = out[key];
      comm.id = key;
      comm.level = 0;
      comm.node_ids.push_back(n.first);

      // edges accumulation
      auto eds = get_node_edges(n.first);
      for (auto& e : eds)
      {
        auto a = e.source, b = e.target;
        if (a > b)
          std::swap(a, b);
        comm.edge_ids.emplace_back(a + "|" + b);
      }
    }

    // Finalize: unique edges and compute occurrence (here, just count nodes for demo)
    for (auto& kv : out)
    {
      auto& edges = kv.second.edge_ids;
      std::sort(edges.begin(), edges.end());
      edges.erase(std::unique(edges.begin(), edges.end()), edges.end());
      kv.second.occurrence = static_cast<double>(kv.second.node_ids.size());
    }
    return out;
  }

  /**
   * @brief Upsert community information into the graph.
   *
   * @param community_info The CommunityInfo object to upsert.
   */
  void upsert_community(const experience::graph::CommunityInfo& community_info)
  {
    graph_.communities[community_info.id] = community_info;
  }

  /**
   * @brief Get the internal graph structure.
   *
   * @return experience::graph::Graph The internal graph.
   */
  experience::graph::Graph get_graph()
  {
    return graph_;
  }

  /**
   * @brief Set the internal graph structure.
   *
   * @param graph The graph to set.
   */
  void set_graph(experience::graph::Graph&& graph)
  {
    graph_ = std::move(graph);
  }

private:
  /**
   * @brief Internal graph structure.
   */
  experience::graph::Graph graph_;

  /**
   * @brief Clustering algorithm parameter.
   */
  std::string algorithm_;

  /**
   * @brief Number of clusters parameter.
   */
  int clusters_;
};

}  // namespace experience