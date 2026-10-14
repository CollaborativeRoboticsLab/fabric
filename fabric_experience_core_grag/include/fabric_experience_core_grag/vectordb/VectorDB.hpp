#pragma once

#include <memory>
#include <string>
#include <Eigen/Dense>

#include <rclcpp/rclcpp.hpp>

#include "fabric_experience_core_grag/vectordb/metric.hpp"
#include "fabric_experience_core_grag/vectordb/structs.hpp"

#include "fabric_experience_core_grag/llm/llm_client.hpp"

namespace experience
{

/**
 * @brief In-memory vector database.
 */
class VectorDB
{
public:
  /**
   * @brief Constructor for the VectorDB.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Name of the embedding plugin to use for parameter retrieval.
   * @param metric_type Metric type for similarity calculations.
   */
  VectorDB(rclcpp::Node::SharedPtr node, std::string plugin_name, experience::vectordb::MetricType metric_type)
  {
    node_ = node;
    metric_type_ = metric_type;

    // Get embedding dimension from parameters
    node_->declare_parameter(plugin_name + ".embedding_dim", 1536);

    node_->get_parameter(plugin_name + ".embedding_dim", embedding_dim_);

    // Initialize embedding client
    embedding_client_ = std::make_shared<experience::llm::PromptServiceClient>(node_, "embedding_plugin");

    RCLCPP_INFO(node_->get_logger(), "VectorDB initialized with embedding_dim=%d", embedding_dim_);
  }

  /**
   * @brief Upsert a data record into the vector database.
   *
   * @param id Unique identifier of the data record.
   * @param content Content to be embedded and stored.
   */
  void upsert(std::string& id, std::string& content)
  {
    auto embeddings = embedding_client_->compute_embedding(content, embedding_dim_);

    for (const auto& embed_result : embeddings)
    {
      // create the embedding vector
      experience::vectordb::Embedding embedding;
      embedding.id = id;
      embedding.vector = Eigen::VectorXf(static_cast<int>(embed_result.embedding.size()));

      // copy embedding values
      for (int j = 0; j < embedding.vector.size(); ++j)
        embedding.vector[j] = embed_result.embedding[j];

      // search the id in data_ to check if it exists
      bool found = false;
      for (int i = 0; i < data_.size(); ++i)
      {
        // if id exists, update the embedding
        if (data_[i].id == id)
        {
          // update existing entry
          data_[i] = embedding;

          // update the matrix row
          matrix_.row(i) = Eigen::Map<const Eigen::RowVectorXf>(embedding.vector.data(), embedding.vector.size());

          found = true;
          return;
        }
      }
      // if id not found, insert new entry
      if (!found)
      {
        // insert new entry for keeping track between ids and vectors
        data_.push_back(embedding);

        // expand the matrix to accommodate the new embedding
        matrix_.conservativeResize(matrix_.rows() + 1, embedding_dim_);

        // add the new embedding to the last row of the matrix for future queries
        matrix_.row(matrix_.rows() - 1) =
            Eigen::Map<const Eigen::RowVectorXf>(embedding.vector.data(), embedding.vector.size());
      }
    }
  }

  /**
   * @brief Query the vector database with a text input.
   *
   * @param query Input text to query.
   * @param top_k Number of top similar results to return.
   * @return std::vector<std::string> JSON strings of the top_k similar records with their metadata and similarity
   * scores.
   */
  std::vector<std::string> query(const std::string& query, int top_k, experience::vectordb::MetricType metric_type)
  {
    auto embeddings = embedding_client_->compute_embedding(query, embedding_dim_);

    // if the query results in multiple embeddings, we will aggregate the results
    std::vector<experience::vectordb::QueryResult> all_vectors;

    for (const auto& embed_result : embeddings)
    {
      // create the query vector
      Eigen::VectorXf query_vector(static_cast<int>(embed_result.embedding.size()));
      for (int i = 0; i < query_vector.size(); ++i)
        query_vector[i] = embed_result.embedding[i];

      if (metric_type == experience::vectordb::MetricType::COSINE)
      {
        auto vectors = experience::vectordb::cosine_query(matrix_, query_vector, top_k);
        all_vectors.insert(all_vectors.end(), vectors.begin(), vectors.end());
      }
      else if (metric_type == experience::vectordb::MetricType::EUCLIDEAN)
      {
        auto vectors = experience::vectordb::euclidean_query(matrix_, query_vector, top_k);
        all_vectors.insert(all_vectors.end(), vectors.begin(), vectors.end());
      }
      else
      {
        RCLCPP_WARN(node_->get_logger(), "Unsupported metric type. Falling back to COSINE.");
        auto vectors = experience::vectordb::cosine_query(matrix_, query_vector, top_k);
        all_vectors.insert(all_vectors.end(), vectors.begin(), vectors.end());
      }
    }

    // sort all_vectors based on score
    std::sort(all_vectors.begin(), all_vectors.end(), [](const auto& a, const auto& b) { return a.score > b.score; });

    // select top_k from all_vectors
    std::vector<experience::vectordb::QueryResult> selected_vectors;
    for (int i = 0; i < std::min(top_k, static_cast<int>(all_vectors.size())); ++i)
      selected_vectors.push_back(all_vectors[i]);

    std::vector<std::string> results;

    // extract the corresponding ids
    for (auto& vec : selected_vectors)
      for (const auto& emb : data_)
        if (vec.vector == emb.vector)
        {
          results.push_back(emb.id);
          break;
        }

    return results;
  }

  /**
   * @brief Retrieve all embeddings stored in the vector database. This is primarily for debugging or storage purposes.
   *
   * @return std::vector<experience::vectordb::Embedding> Vector of all stored embeddings.
   */
  std::vector<experience::vectordb::Embedding> get_embeddings()
  {
    return data_;
  }

  /**
   * @brief Set all embeddings in the vector database. This replaces any existing data. Can be used for restoring from storage.
   *
   * @param embeddings Vector of embeddings to set.
   * @return true if successful, false otherwise.
   */
  bool set_embeddings(std::vector<experience::vectordb::Embedding>& embeddings)
  {
    data_ = std::move(embeddings);

    // Rebuild the matrix
    try
    {
      matrix_.resize(data_.size(), embedding_dim_);

      for (size_t i = 0; i < data_.size(); ++i)
        matrix_.row(i) = Eigen::Map<const Eigen::RowVectorXf>(data_[i].vector.data(), data_[i].vector.size());
    }
    catch (const std::exception& e)
    {
      RCLCPP_ERROR(node_->get_logger(), "Failed to rebuild matrix: %s", e.what());
      return false;
    }

    return true;
  }

protected:
  /**
   * @brief Dimension of the embeddings.
   */
  int embedding_dim_;

  /**
   * @brief Shared pointer to the ROS2 node.
   */
  rclcpp::Node::SharedPtr node_;

  /**
   * @brief Embedding client.
   */
  std::shared_ptr<experience::llm::PromptServiceClient> embedding_client_;

  /**
   * @brief Embedding strategy to use.
   */
  experience::vectordb::MetricType metric_type_;

  /**
   * @brief Stored data entries.
   */
  std::vector<experience::vectordb::Embedding> data_;

  /**
   * @brief Eigen matrix to hold embeddings.
   */
  Eigen::MatrixXf matrix_;
};

}  // namespace experience