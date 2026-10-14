#pragma once

#include <string>
#include <Eigen/Dense>

namespace experience::vectordb
{
/**
 * @brief Data record structure
 *
 * @param id Unique identifier of the data record
 * @param vector Embedding vector associated with the data record
 */
struct Embedding
{
  std::string id;
  Eigen::VectorXf vector;
};

/**
 * @brief Query result structure
 *
 * @param id Unique identifier of the data record
 * @param vector Embedding vector associated with the data record
 * @param score Similarity score of the query result
 */
struct QueryResult
{
  std::string id;
  Eigen::VectorXf vector;
  float score;
};

}  // namespace experience::vectordb
