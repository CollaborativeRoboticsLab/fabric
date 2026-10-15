#pragma once
#include <Eigen/Dense>
#include <vector>
#include <algorithm>
#include "fabric_experience_core_grag/vectordb/structs.hpp"

namespace fabric::experience::vectordb
{
enum class MetricType
{
  COSINE,
  EUCLIDEAN,
  MANHATTAN
};

/**
 * @brief Calculates the top-k most similar vectors to a target using cosine similarity.
 *
 * This function computes the cosine similarity between each row vector in the source matrix and the target vector.
 * It normalizes both the source vectors and the target, computes the dot product (cosine similarity), and returns
 * the top_k vectors from the source that are most similar to the target (highest cosine similarity).
 *
 * @param source   Matrix where each row is a vector to compare against the target.
 * @param target   The vector to which similarity is measured.
 * @param top_k    The number of most similar vectors to return.
 * @return std::vector<Eigen::VectorXf> The top_k most similar vectors from the source, ordered by similarity.
 */
std::vector<experience::vectordb::QueryResult> cosine_query(const Eigen::MatrixXf& source, const Eigen::VectorXf& target, int top_k)
{
  std::vector<experience::vectordb::QueryResult> results;

  Eigen::VectorXf normalized_target = target.normalized();
  Eigen::MatrixXf normalized_source = source.rowwise().normalized();

  // store scores and indices
  std::vector<std::pair<float, int>> scores;

  for (int i = 0; i < normalized_source.rows(); ++i)
  {
    float denominator = (normalized_source.row(i).norm() * normalized_target.norm());
    if (denominator == 0.0f)
    {
      continue;  // skip this iteration to avoid division by zero
    }

    float score = normalized_source.row(i).dot(normalized_target) / denominator;
    scores.emplace_back(score, i);
  }

  // sort scores in descending order as we want the largest scores
  std::sort(scores.begin(), scores.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

  // retrieve top_k results
  for (int i = 0; i < std::min(top_k, static_cast<int>(scores.size())); ++i)
  {
    experience::vectordb::QueryResult result;
    result.vector = source.row(scores[i].second);
    result.score = scores[i].first;
    results.push_back(result);
  }

  return results;
}

/**
 * @brief Calculates the top-k most similar vectors to a target using Euclidean distance.
 *
 * This function computes the Euclidean distance between each row vector in the source matrix and the target vector.
 * It calculates the squared norm of the difference between each source vector and the target, sorts them by distance,
 * and returns the top_k vectors from the source that are closest to the target (smallest distance).
 *
 * @param source   Matrix where each row is a vector to compare against the target.
 * @param target   The vector to which similarity is measured.
 * @param top_k    The number of most similar vectors to return.
 * @return std::vector<Eigen::VectorXf> The top_k most similar vectors from the source, ordered by similarity.
 */
std::vector<experience::vectordb::QueryResult> euclidean_query(const Eigen::MatrixXf& source, const Eigen::VectorXf& target, int top_k)
{
  std::vector<experience::vectordb::QueryResult> results;

  // store distances and indices
  std::vector<std::pair<float, int>> distances;

  for (int i = 0; i < source.rows(); ++i)
  {
    float distance = (source.row(i) - target).squaredNorm();
    distances.emplace_back(distance, i);
  }

  // sort distances in ascending order as we want the smallest distances
  std::sort(distances.begin(), distances.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

  // retrieve top_k results
  for (int i = 0; i < std::min(top_k, static_cast<int>(distances.size())); ++i)
  {
    experience::vectordb::QueryResult result;
    result.vector = source.row(distances[i].second);
    result.score = distances[i].first;
    results.push_back(result);
  }

  return results;
}

}  // namespace fabric::experience::vectordb
