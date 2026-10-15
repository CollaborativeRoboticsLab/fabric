#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <limits>

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>

#include <fabric_base/utils/exception.hpp>
#include <fabric_experience_core_grag/graph/structs.hpp>

namespace fabric::experience::clustering
{
/**
 * @brief Enumeration of supported clustering algorithms.
 *
 * @param BFS Breadth-First Search clustering.
 * @param KMEANS K-Means clustering.
 * @param SPECTRAL Spectral clustering.
 */
enum class Algorithm
{
  BFS,
  KMEANS,
  SPECTRAL
};

namespace detail
{

inline Eigen::MatrixXf build_feature_matrix(const experience::graph::Graph& graph,
                                            std::vector<std::string>& node_ids)
{
  node_ids.clear();
  node_ids.reserve(graph.nodes.size());

  size_t max_dim = 0;
  for (const auto& [node_id, node] : graph.nodes)
  {
    node_ids.push_back(node_id);
    max_dim = std::max(max_dim, node.feature_vector.size());
  }

  if (max_dim == 0)
    max_dim = 1;

  Eigen::MatrixXf features = Eigen::MatrixXf::Zero(static_cast<int>(node_ids.size()), static_cast<int>(max_dim));
  for (size_t row = 0; row < node_ids.size(); ++row)
  {
    const auto& node = graph.nodes.at(node_ids[row]);
    if (node.feature_vector.empty())
    {
      features(static_cast<int>(row), 0) = static_cast<float>(graph.adjacency.at(node_ids[row]).size());
      continue;
    }

    for (size_t col = 0; col < node.feature_vector.size(); ++col)
      features(static_cast<int>(row), static_cast<int>(col)) = static_cast<float>(node.feature_vector[col]);
  }

  return features;
}

inline std::vector<int> run_kmeans(const Eigen::MatrixXf& samples, int k, int max_iterations = 100)
{
  const int sample_count = static_cast<int>(samples.rows());
  if (sample_count == 0)
    return {};

  k = std::max(1, std::min(k, sample_count));

  std::vector<int> centroid_indices;
  centroid_indices.reserve(static_cast<size_t>(k));
  centroid_indices.push_back(0);

  while (static_cast<int>(centroid_indices.size()) < k)
  {
    float best_distance = -1.0f;
    int best_index = 0;
    for (int row = 0; row < sample_count; ++row)
    {
      float nearest_distance = std::numeric_limits<float>::max();
      for (const int centroid_index : centroid_indices)
      {
        const float distance = (samples.row(row) - samples.row(centroid_index)).squaredNorm();
        nearest_distance = std::min(nearest_distance, distance);
      }
      if (nearest_distance > best_distance)
      {
        best_distance = nearest_distance;
        best_index = row;
      }
    }
    centroid_indices.push_back(best_index);
  }

  Eigen::MatrixXf centroids(k, samples.cols());
  for (int cluster = 0; cluster < k; ++cluster)
    centroids.row(cluster) = samples.row(centroid_indices[cluster]);

  std::vector<int> assignments(static_cast<size_t>(sample_count), 0);
  for (int iteration = 0; iteration < max_iterations; ++iteration)
  {
    bool changed = false;
    for (int row = 0; row < sample_count; ++row)
    {
      float best_distance = (samples.row(row) - centroids.row(0)).squaredNorm();
      int best_cluster = 0;
      for (int cluster = 1; cluster < k; ++cluster)
      {
        const float distance = (samples.row(row) - centroids.row(cluster)).squaredNorm();
        if (distance < best_distance)
        {
          best_distance = distance;
          best_cluster = cluster;
        }
      }

      if (assignments[static_cast<size_t>(row)] != best_cluster)
      {
        assignments[static_cast<size_t>(row)] = best_cluster;
        changed = true;
      }
    }

    Eigen::MatrixXf new_centroids = Eigen::MatrixXf::Zero(k, samples.cols());
    std::vector<int> counts(static_cast<size_t>(k), 0);
    for (int row = 0; row < sample_count; ++row)
    {
      const int cluster = assignments[static_cast<size_t>(row)];
      new_centroids.row(cluster) += samples.row(row);
      counts[static_cast<size_t>(cluster)]++;
    }

    for (int cluster = 0; cluster < k; ++cluster)
    {
      if (counts[static_cast<size_t>(cluster)] > 0)
      {
        centroids.row(cluster) = new_centroids.row(cluster) /
                                 static_cast<float>(counts[static_cast<size_t>(cluster)]);
      }
      else
      {
        float farthest_distance = -1.0f;
        int farthest_index = 0;
        for (int row = 0; row < sample_count; ++row)
        {
          const float distance = (samples.row(row) - centroids.row(assignments[static_cast<size_t>(row)])).squaredNorm();
          if (distance > farthest_distance)
          {
            farthest_distance = distance;
            farthest_index = row;
          }
        }
        centroids.row(cluster) = samples.row(farthest_index);
      }
    }

    if (!changed)
      break;
  }

  return assignments;
}

inline void apply_assignments(experience::graph::Graph& graph,
                              const std::vector<std::string>& node_ids,
                              const std::vector<int>& assignments)
{
  for (size_t index = 0; index < node_ids.size(); ++index)
  {
    auto& node = graph.nodes[node_ids[index]];
    node.cluster_id = assignments[index];
    node.level = 0;
  }
}

}  // namespace detail

/**
 * @brief Perform BFS clustering and annotate nodes.
 */
void bfs_clustering(experience::graph::Graph& graph_)
{
  int cluster_id = 0;
  std::unordered_set<std::string> visited;

  for (const auto& n : graph_.nodes)
  {
    if (visited.count(n.first))
      continue;

    // BFS
    std::vector<std::string> queue{ n.first };
    visited.insert(n.first);
    while (!queue.empty())
    {
      auto cur = queue.back();
      queue.pop_back();

      auto& node_info = graph_.nodes[cur];
      node_info.cluster_id = cluster_id;
      node_info.level = 0;

      for (const auto& nb : graph_.adjacency[cur])
        if (!visited.count(nb))
        {
          visited.insert(nb);
          queue.push_back(nb);
        }
    }
    cluster_id++;
  }
}

/**
 * @brief Perform KMEANS clustering and annotate nodes.
 */
void kmeans_clustering(experience::graph::Graph& graph_, int k)
{
  if (k <= 0)
    throw fabric::fabric_exception("Number of clusters must be positive.");
  if (graph_.nodes.empty())
    return;

  std::vector<std::string> node_ids;
  const Eigen::MatrixXf features = detail::build_feature_matrix(graph_, node_ids);
  const std::vector<int> assignments = detail::run_kmeans(features, k);
  detail::apply_assignments(graph_, node_ids, assignments);
}

/**
 * @brief Perform SPECTRAL clustering and annotate nodes.
 */
void spectral_clustering(experience::graph::Graph& graph_, int k)
{
  if (k <= 0)
    throw fabric::fabric_exception("Number of clusters must be positive.");
  if (graph_.nodes.empty())
    return;

  std::vector<std::string> node_ids;
  node_ids.reserve(graph_.nodes.size());
  for (const auto& [node_id, _] : graph_.nodes)
    node_ids.push_back(node_id);

  const int node_count = static_cast<int>(node_ids.size());
  if (node_count == 1)
  {
    graph_.nodes[node_ids.front()].cluster_id = 0;
    graph_.nodes[node_ids.front()].level = 0;
    return;
  }

  k = std::max(1, std::min(k, node_count));

  std::unordered_map<std::string, int> index_by_id;
  index_by_id.reserve(node_ids.size());
  for (int index = 0; index < node_count; ++index)
    index_by_id[node_ids[static_cast<size_t>(index)]] = index;

  Eigen::MatrixXf adjacency = Eigen::MatrixXf::Zero(node_count, node_count);
  for (int row = 0; row < node_count; ++row)
  {
    const auto& source_id = node_ids[static_cast<size_t>(row)];
    const auto adjacency_it = graph_.adjacency.find(source_id);
    if (adjacency_it == graph_.adjacency.end())
      continue;

    for (const auto& target_id : adjacency_it->second)
    {
      const auto target_it = index_by_id.find(target_id);
      if (target_it != index_by_id.end())
        adjacency(row, target_it->second) = 1.0f;
    }
  }

  Eigen::VectorXf degrees = adjacency.rowwise().sum();
  Eigen::MatrixXf laplacian = Eigen::MatrixXf::Identity(node_count, node_count);
  for (int row = 0; row < node_count; ++row)
  {
    if (degrees(row) <= 0.0f)
      continue;

    const float inv_sqrt_degree_row = 1.0f / std::sqrt(degrees(row));
    for (int col = 0; col < node_count; ++col)
    {
      if (degrees(col) <= 0.0f || adjacency(row, col) == 0.0f)
        continue;
      laplacian(row, col) -= adjacency(row, col) * inv_sqrt_degree_row * (1.0f / std::sqrt(degrees(col)));
    }
  }

  Eigen::SelfAdjointEigenSolver<Eigen::MatrixXf> solver(laplacian);
  if (solver.info() != Eigen::Success)
    throw fabric::fabric_exception("Spectral clustering failed to decompose graph Laplacian.");

  Eigen::MatrixXf embedding = solver.eigenvectors().leftCols(k);
  for (int row = 0; row < embedding.rows(); ++row)
  {
    const float norm = embedding.row(row).norm();
    if (norm > 0.0f)
      embedding.row(row) /= norm;
  }

  const std::vector<int> assignments = detail::run_kmeans(embedding, k);
  detail::apply_assignments(graph_, node_ids, assignments);
}

}  // namespace fabric::experience::clustering