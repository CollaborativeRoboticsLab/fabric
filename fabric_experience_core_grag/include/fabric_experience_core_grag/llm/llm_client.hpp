#pragma once
#include <string>
#include <vector>
#include <memory>
#include <future>
#include <utility>
#include <stdexcept>
#include <tuple>
#include <mutex>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>
#include "prompt_msgs/srv/embedding.hpp"
#include "prompt_msgs/srv/prompt.hpp"
#include "prompt_msgs/srv/tokenize.hpp"

#include "fabric_experience_core_grag/llm/prompts.hpp"
#include "fabric_experience_core_grag/llm/structs.hpp"

namespace fabric::experience::llm
{
class PromptServiceClient
{
public:
  /**
   * @brief Thin adapter over the prompt, embedding, and tokenize ROS services.
   *
   * @param node Shared pointer to the ROS2 node.
   * @param plugin_name Parameter namespace used for service and model configuration.
   */
  PromptServiceClient(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name)
  {
    // Get service name and default parameters for embedding service from parameters
    node_ = node;

    node_->declare_parameter(plugin_name + ".embed.service_name", "prompt/embedding");
    node_->declare_parameter(plugin_name + ".embed.model_name", "text-embedding-3-small");
    node_->declare_parameter(plugin_name + ".embed.model_family", "openai");

    node_->declare_parameter(plugin_name + ".prompt.service_name", "prompt/prompt");
    node_->declare_parameter(plugin_name + ".prompt.model_name", "gpt-4");
    node_->declare_parameter(plugin_name + ".prompt.model_family", "openai");

    node_->declare_parameter(plugin_name + ".tokenize.service_name", "prompt/tokenizer");
    node_->declare_parameter(plugin_name + ".tokenize.model_name", "O200K_BASE");
    node_->declare_parameter(plugin_name + ".tokenize.model_family", "openai");
    node_->declare_parameter(plugin_name + ".tokenize.tokens_per_document", 20000);
    node_->declare_parameter(plugin_name + ".tokenize.tokens_per_document_overlap", 2000);
    node_->declare_parameter(plugin_name + ".tokenize.tokens_per_embedding", 8191);
    node_->declare_parameter(plugin_name + ".tokenize.tokens_per_embedding_overlap", 100);

    node_->get_parameter(plugin_name + ".embed.service_name", embed_service_name_);
    node_->get_parameter(plugin_name + ".embed.model_name", embed_model_name_);
    node_->get_parameter(plugin_name + ".embed.model_family", embed_model_family_);

    node_->get_parameter(plugin_name + ".prompt.service_name", prompt_service_name_);
    node_->get_parameter(plugin_name + ".prompt.model_name", prompt_model_name_);
    node_->get_parameter(plugin_name + ".prompt.model_family", prompt_model_family_);

    node_->get_parameter(plugin_name + ".tokenize.service_name", tokenize_service_name_);
    node_->get_parameter(plugin_name + ".tokenize.model_name", tokenize_model_name_);
    node_->get_parameter(plugin_name + ".tokenize.model_family", tokenize_model_family_);
    node_->get_parameter(plugin_name + ".tokenize.tokens_per_document", tokens_per_document_);
    node_->get_parameter(plugin_name + ".tokenize.tokens_per_document_overlap", tokens_per_document_overlap_);
    node_->get_parameter(plugin_name + ".tokenize.tokens_per_embedding", tokens_per_embedding_);
    node_->get_parameter(plugin_name + ".tokenize.tokens_per_embedding_overlap", tokens_per_embedding_overlap_);

    embed_client = node_->create_client<prompt_msgs::srv::Embedding>(embed_service_name_);

    while (!embed_client->wait_for_service(std::chrono::seconds(1)))
      RCLCPP_INFO(node_->get_logger(), "Waiting for embedding service to be available...");

    RCLCPP_INFO(node_->get_logger(), "Initialized LLM Embedding Client with service: %s, model: %s, family: %s",
                embed_service_name_.c_str(), embed_model_name_.c_str(), embed_model_family_.c_str());

    prompt_client = node_->create_client<prompt_msgs::srv::Prompt>(prompt_service_name_);

    while (!prompt_client->wait_for_service(std::chrono::seconds(1)))
      RCLCPP_INFO(node_->get_logger(), "Waiting for prompt service to be available...");

    RCLCPP_INFO(node_->get_logger(), "Initialized LLM Prompt Client with service: %s, model: %s, family: %s",
                prompt_service_name_.c_str(), prompt_model_name_.c_str(), prompt_model_family_.c_str());

    tokenize_client_ = node_->create_client<prompt_msgs::srv::Tokenize>(tokenize_service_name_);

    while (!tokenize_client_->wait_for_service(std::chrono::seconds(1)))
      RCLCPP_INFO(node_->get_logger(), "Waiting for tokenize service to be available...");

    RCLCPP_INFO(node_->get_logger(), "Initialized LLM Tokenize Client with service: %s, model: %s, family: %s",
                tokenize_service_name_.c_str(), tokenize_model_name_.c_str(), tokenize_model_family_.c_str());
  }

  /**
    * @brief Destructor for the service adapter.
   */
    ~PromptServiceClient()
  {
  }

  /**
   * @brief Compute embedding for the given text.
   *
   * @param text Input text to embed.
   * @param embedding_dim Expected dimension of the embedding.
   * @return std::vector<float> Computed embedding vector.
   */
  std::vector<experience::llm::EmbeddingResult> compute_embedding(const std::string& text, int embedding_dim)
  {
    // check for empty text
    if (text.empty())
      throw std::invalid_argument("Input text for embedding is empty.");

    // check for token length
    auto tokens = encode_to_tokens(text);

    std::vector<std::vector<int32_t>> token_chunks;
    std::vector<std::string> text_chunks;
    if (tokens.size() > tokens_per_embedding_)
    {
      // chop tokens to fit limit and create chunks. consider overlap
      size_t start = 0;
      while (start < tokens.size())
      {
        size_t end = std::min(start + tokens_per_embedding_, tokens.size());
        std::vector<int32_t> chunk(tokens.begin() + start, tokens.begin() + end);
        token_chunks.push_back(chunk);

        if (end == tokens.size())
          break;

        start = end - tokens_per_embedding_overlap_;
      }

      // decode each chunk back to text
      for (const auto& chunk : token_chunks)
      {
        std::string chunk_text = decode_from_tokens(chunk);
        text_chunks.push_back(chunk_text);
      }
    }
    else
    {
      text_chunks.push_back(text);
    }

    // compute embedding per chunk and return as a pair of text and embedding
    std::vector<experience::llm::EmbeddingResult> results;

    for (const auto& chunk_text : text_chunks)
    {
      auto chunk_embedding = compute_embedding_per_chunk(chunk_text, embedding_dim);

      results.push_back(chunk_embedding);
    }

    return results;
  }

  /**
   * @brief Extract entities from the given text using LLM.
   *
   * @param name Name associated with the text (e.g., capability name).
   * @param text Input text to extract entities from.
   * @return std::vector<EntityInfo> Vector of extracted entities.
   */
  std::vector<EntityInfo> extract_entities(const std::string& name, const std::string& text)
  {
    // check for empty text
    if (text.empty())
      throw std::invalid_argument("Input text for entity extraction is empty.");

    std::string prompt_text = experience::llm::ENTITY_EXTRACTION_PROMPT + "Capability name: " + name + ". Text: ";

    auto text_documents = process_into_documents(prompt_text, text);

    // compute embedding per chunk and return as a pair of text and embedding
    std::vector<EntityInfo> results;

    for (const auto& chunk_text : text_documents)
    {
      auto entity = extract_entities_per_document(chunk_text);

      results.insert(results.end(), entity.begin(), entity.end());
    }

    return results;
  }

  /**
   * @brief Extract relationships from the given text using LLM.
   *
   * @param name Name associated with the text (e.g., capability name).
   * @param text Input text to extract relationships from.
   * @param entities List of entities to consider for relationship extraction.
   * @return std::vector<RelationshipInfo> Vector of extracted relationships.
   */
  std::vector<RelationshipInfo> extract_relationships(const std::string& name, const std::string& text,
                                                      const std::vector<EntityInfo>& entities)
  {
    // check for empty text
    if (text.empty())
      throw std::invalid_argument("Input text for relationship extraction is empty.");

    // prepare known entities string
    std::string known_entities = "List of Entities: [";
    for (const auto& entity : entities)
    {
      known_entities += "\"" + entity.name + "\", ";
    }
    known_entities += "] ";

    // prepare system prompt
    std::string system_prompt =
        experience::llm::RELATIONSHIP_EXTRACTION_PROMPT + "Capability name: " + name + ". " + known_entities + "Text: ";

    auto text_documents = process_into_documents(system_prompt, text);

    // extract relationships per document
    std::vector<RelationshipInfo> relationships;

    for (const auto& chunk_text : text_documents)
    {
      auto rels = extract_relationships_per_document(chunk_text);

      relationships.insert(relationships.end(), rels.begin(), rels.end());
    }

    return relationships;
  }

  /**
   * @brief Combine multiple descriptions into a single coherent description using LLM.
   *
   * @param descriptions Vector of descriptions to combine.
   * @return std::string Combined description.
   */
  std::string combine_descriptions(const std::vector<std::string>& descriptions)
  {
    std::string combined;
    for (const auto& desc : descriptions)
    {
      combined += desc + "\n";
    }

    std::string system_prompt = experience::llm::DESCRIPTION_COMBINATION_PROMPT + "Input Descriptions:";

    std::vector<std::string> text_documents = process_into_documents(system_prompt, combined);

    // combine descriptions per document
    std::string final_description = "";

    for (const auto& chunk_text : text_documents)
    {
      auto combined_desc = combine_descriptions_per_document(chunk_text);
      final_description += combined_desc + "\n";
    }

    return final_description;
  }

private:
  /**
   * @brief Compute embedding for a single chunk of text.
   *
   * @param text Input text chunk to embed.
   * @param embedding_dim Expected dimension of the embedding.
   * @return std::vector<float> Computed embedding vector.
   */
  experience::llm::EmbeddingResult compute_embedding_per_chunk(const std::string& text, int embedding_dim)
  {
    auto req = std::make_shared<prompt_msgs::srv::Embedding::Request>();
    req->input.text = text;
    req->input.model_family = embed_model_family_;

    prompt_msgs::msg::ModelOption option1;
    option1.key = "encoding_format";
    option1.value = "float";
    option1.type = prompt_msgs::msg::ModelOption::STRING_TYPE;
    req->input.options.push_back(option1);

    prompt_msgs::msg::ModelOption option2;
    option2.key = "model";
    option2.value = embed_model_name_;
    option2.type = prompt_msgs::msg::ModelOption::STRING_TYPE;
    req->input.options.push_back(option2);

    prompt_msgs::msg::ModelOption option3;
    option3.key = "dimensions";
    option3.value = std::to_string(embedding_dim);
    option3.type = prompt_msgs::msg::ModelOption::INT_TYPE;
    req->input.options.push_back(option3);

    std::mutex mutex;
    std::condition_variable cv;
    bool completed = false;
    std::unique_lock<std::mutex> lock(mutex);

    experience::llm::EmbeddingResult result;
    result.text = text;

    embed_client->async_send_request(
      req, [&mutex, &cv, &completed, &result, embedding_dim](
           rclcpp::Client<prompt_msgs::srv::Embedding>::SharedFuture future) {
          if (!future.valid())
          {
            throw std::runtime_error("Invalid future received in embedding response callback.");
          }

          auto response = future.get();

          // fill in embeddings based on index
          if (!response->output.embeddings[0].is_float)
            throw std::runtime_error("Received non-float embedding, which is unsupported.");

          if (response->output.embeddings[0].float_embedding.size() == 0)
            throw std::runtime_error("Received empty float embedding.");

          if (response->output.embeddings[0].float_embedding.size() != static_cast<size_t>(embedding_dim))
            throw std::runtime_error("Received float embedding of incorrect dimension.");

          result.embedding = response->output.embeddings[0].float_embedding;

          completed = true;
          cv.notify_one();
        });

    cv.wait(lock, [&completed]() { return completed; });

    return result;
  }

  /**
   * @brief Extract entities from the given text using LLM.
   *
   * @param text Input text to extract entities from.
   * @return std::vector<EntityInfo> Vector of extracted entities.
   */
  std::vector<EntityInfo> extract_entities_per_document(const std::string& text)
  {
    auto req = std::make_shared<prompt_msgs::srv::Prompt::Request>();
    req->uuid = "";
    req->prompt.prompt = text;
    req->prompt.use_cache = false;
    req->prompt.flush_cache = false;
    req->prompt.use_chat_mode = false;
    req->prompt.model_family = prompt_model_family_;

    prompt_msgs::msg::ModelOption option1;
    option1.key = "model";
    option1.value = prompt_model_name_;
    option1.type = prompt_msgs::msg::ModelOption::STRING_TYPE;
    req->prompt.options.push_back(option1);

    std::vector<EntityInfo> entities;
    std::mutex mutex;
    std::condition_variable cv;
    bool completed = false;

    std::unique_lock<std::mutex> lock(mutex);

    prompt_client->async_send_request(
        req, [&mutex, &cv, &completed, &entities, this](rclcpp::Client<prompt_msgs::srv::Prompt>::SharedFuture future) {
          if (!future.valid())
            throw std::runtime_error("Invalid future received in prompt response callback.");

          auto response = future.get();

          // Parse the response to extract entities
          try
          {
            // try to extract json portion from response assuming json is well formed but may be embedded in text
            auto json_response = extract_json(response->response.response);

            if (!json_response.contains("entities") || !json_response["entities"].is_array())
              throw std::runtime_error("JSON response does not contain valid 'entities' array.");

            for (const auto& item : json_response["entities"])
            {
              if (!item.contains("name") || !item.contains("type") || !item.contains("description"))
                continue;

              EntityInfo entity;

              entity.name = item["name"].get<std::string>();
              entity.type = item["type"].get<std::string>();
              entity.description = item["description"].get<std::string>();

              entities.push_back(entity);
            }
          }
          catch (const nlohmann::json::parse_error& e)
          {
            throw std::runtime_error("Failed to parse LLM response as JSON.");
          }

          completed = true;
          cv.notify_one();
        });

    cv.wait(lock, [&completed]() { return completed; });

    return entities;
  }

  /**
   * @brief Extract relationships from the given text using LLM.
   *
   * @param text Input text to extract relationships from.
   * @return std::vector<RelationshipInfo> Vector of extracted relationships.
   */
  std::vector<RelationshipInfo> extract_relationships_per_document(const std::string& text)
  {
    auto req = std::make_shared<prompt_msgs::srv::Prompt::Request>();
    req->uuid = "";
    req->prompt.prompt = text;
    req->prompt.use_cache = false;
    req->prompt.flush_cache = false;
    req->prompt.use_chat_mode = false;
    req->prompt.model_family = prompt_model_family_;

    prompt_msgs::msg::ModelOption option1;
    option1.key = "model";
    option1.value = prompt_model_name_;
    option1.type = prompt_msgs::msg::ModelOption::STRING_TYPE;
    req->prompt.options.push_back(option1);

    std::vector<RelationshipInfo> relationships;
    std::mutex mutex;
    std::condition_variable cv;
    bool completed = false;

    std::unique_lock<std::mutex> lock(mutex);

    prompt_client->async_send_request(req, [&mutex, &cv, &completed, &relationships,
                        this](rclcpp::Client<prompt_msgs::srv::Prompt>::SharedFuture future) {
          if (!future.valid())
            throw std::runtime_error("Invalid future received in prompt response callback.");

          auto response = future.get();

          // Parse the response to extract relationships
          try
          {
            // try to extract json portion from response assuming json is well formed but may be embedded in text
            auto json_response = extract_json(response->response.response);

            if (!json_response.contains("relationships") || !json_response["relationships"].is_array())
              throw std::runtime_error("JSON response does not contain valid 'relationships' array.");

            for (const auto& item : json_response["relationships"])
            {
              if (!item.contains("source") || !item.contains("target") || !item.contains("relationship"))
                continue;

              RelationshipInfo relationship;
              relationship.source = item["source"].get<std::string>();
              relationship.target = item["target"].get<std::string>();
              relationship.relationship = item["relationship"].get<std::string>();

              relationships.push_back(relationship);
            }
          }
          catch (const nlohmann::json::parse_error& e)
          {
            throw std::runtime_error("Failed to parse LLM response as JSON.");
          }

          completed = true;
          cv.notify_one();
        });

    cv.wait(lock, [&completed]() { return completed; });

    return relationships;
  }

  /**
   * @brief Combine multiple descriptions into a single coherent description using LLM.
   *
   * @param text Input text to combine.
   * @return std::string Combined description.
   */
  std::string combine_descriptions_per_document(const std::string& text)
  {
    auto req = std::make_shared<prompt_msgs::srv::Prompt::Request>();
    req->uuid = "";
    req->prompt.prompt = text;
    req->prompt.use_cache = false;
    req->prompt.flush_cache = false;
    req->prompt.use_chat_mode = false;
    req->prompt.model_family = prompt_model_family_;

    prompt_msgs::msg::ModelOption option1;
    option1.key = "model";
    option1.value = prompt_model_name_;
    option1.type = prompt_msgs::msg::ModelOption::STRING_TYPE;
    req->prompt.options.push_back(option1);

    std::string combined_description;
    std::mutex mutex;
    std::condition_variable cv;
    bool completed = false;

    std::unique_lock<std::mutex> lock(mutex);

    prompt_client->async_send_request(req, [&mutex, &cv, &completed, &combined_description,
                        this](rclcpp::Client<prompt_msgs::srv::Prompt>::SharedFuture future) {
          if (!future.valid())
            throw std::runtime_error("Invalid future received in prompt response callback.");

          auto response = future.get();

          // Parse the response to extract combined description
          try
          {
            // try to extract json portion from response assuming json is well formed but may be embedded in text
            auto json_response = extract_json(response->response.response);

            if (!json_response.contains("combined_description") || !json_response["combined_description"].is_string())
              throw std::runtime_error("JSON response does not contain valid 'combined_description' string.");

            combined_description = json_response["combined_description"].get<std::string>();
          }
          catch (const nlohmann::json::parse_error& e)
          {
            throw std::runtime_error("Failed to parse LLM response as JSON.");
          }

          completed = true;
          cv.notify_one();
        });

    cv.wait(lock, [&completed]() { return completed; });

    return combined_description;
  }

  /**
   * @brief Divide text into smaller documents based on token limits.
   *
   * @param pretext Preceding text to include before each document.
   * @param text Input text to divide.
   * @return std::vector<std::string> Vector of text documents.
   */
  std::vector<std::string> process_into_documents(const std::string& pretext, const std::string& text)
  {
    // calculate token size for pretext
    auto pretext_tokens = encode_to_tokens(pretext);
    int pretext_token_size = static_cast<int>(pretext_tokens.size());
    int remaining_tokens = tokens_per_document_ - pretext_token_size;

    std::vector<std::vector<int32_t>> token_chunks;
    std::vector<std::string> text_documents;

    // check for token length
    auto tokens = encode_to_tokens(text);
    if (tokens.size() > remaining_tokens)
    {
      // chop tokens to fit limit and create chunks. consider overlap
      size_t start = 0;
      while (start < tokens.size())
      {
        size_t end = std::min(start + remaining_tokens, tokens.size());
        std::vector<int32_t> chunk(tokens.begin() + start, tokens.begin() + end);
        token_chunks.push_back(chunk);

        if (end == tokens.size())
          break;

        start = end - tokens_per_document_overlap_;
      }

      // decode each chunk back to text
      for (const auto& chunk : token_chunks)
      {
        std::string chunk_text = decode_from_tokens(chunk);
        text_documents.push_back(pretext + chunk_text);
      }
    }
    else
    {
      text_documents.push_back(pretext + text);
    }

    return text_documents;
  }

  /**
   * @brief Encode text to tokens using the tokenize service.
   *
   * @param text Input text to encode.
   * @return std::vector<int32_t> Encoded tokens.
   */
  std::vector<int32_t> encode_to_tokens(const std::string& text)
  {
    auto req = std::make_shared<prompt_msgs::srv::Tokenize::Request>();

    req->input.model_family = tokenize_model_family_;
    req->input.text = text;
    req->input.encode = true;

    prompt_msgs::msg::ModelOption option;
    option.key = "model";
    option.value = tokenize_model_name_;
    option.type = prompt_msgs::msg::ModelOption::STRING_TYPE;
    req->input.options.push_back(option);

    std::mutex mutex;
    std::condition_variable cv;
    bool completed = false;
    std::unique_lock<std::mutex> lock(mutex);

    std::vector<int32_t> tokens;

    tokenize_client_->async_send_request(
        req, [&mutex, &cv, &completed, &tokens](rclcpp::Client<prompt_msgs::srv::Tokenize>::SharedFuture future) {
          if (!future.valid())
            throw std::runtime_error("Invalid future received in tokenize response callback.");

          auto response = future.get();

          if (!response->output.success)
            throw std::runtime_error("Tokenize service reported failure :" + response->output.error);

          tokens = response->output.tokens;

          completed = true;
          cv.notify_one();
        });

    cv.wait(lock, [&completed]() { return completed; });

    return tokens;
  }

  /**
   * @brief Decode tokens back to text using the tokenize service.
   *
   * @param tokens Input tokens to decode.
   * @return std::string Decoded text.
   */
  std::string decode_from_tokens(const std::vector<int32_t>& tokens)
  {
    auto req = std::make_shared<prompt_msgs::srv::Tokenize::Request>();

    req->input.model_family = tokenize_model_family_;
    req->input.tokens = tokens;
    req->input.encode = false;

    prompt_msgs::msg::ModelOption option;
    option.key = "model";
    option.value = tokenize_model_name_;
    option.type = prompt_msgs::msg::ModelOption::STRING_TYPE;
    req->input.options.push_back(option);

    std::mutex mutex;
    std::condition_variable cv;
    bool completed = false;
    std::unique_lock<std::mutex> lock(mutex);

    std::string text;

    tokenize_client_->async_send_request(
        req, [&mutex, &cv, &completed, &text](rclcpp::Client<prompt_msgs::srv::Tokenize>::SharedFuture future) {
          if (!future.valid())
            throw std::runtime_error("Invalid future received in tokenize response callback.");

          auto response = future.get();

          if (!response->output.success)
            throw std::runtime_error("Tokenize service reported failure :" + response->output.error);

          text = response->output.text;

          completed = true;
          cv.notify_one();
        });

    cv.wait(lock, [&completed]() { return completed; });

    return text;
  }

  /**
   * @brief Extract a fenced JSON payload from an LLM response.
   *
   * @param response_text Raw LLM response text.
   * @return nlohmann::json Parsed JSON document.
   */
  nlohmann::json extract_json(const std::string& response_text)
  {
    const auto fenced_start = response_text.find("```json");
    if (fenced_start != std::string::npos)
    {
      const auto fenced_end = response_text.find("```", fenced_start + 7);
      if (fenced_end == std::string::npos || fenced_end <= fenced_start + 7)
        throw std::runtime_error("No valid JSON payload found in fenced LLM response.");

      const std::string json_str = response_text.substr(fenced_start + 7, fenced_end - (fenced_start + 7));
      return nlohmann::json::parse(json_str);
    }

    const auto object_start = response_text.find('{');
    const auto object_end = response_text.rfind('}');
    if (object_start == std::string::npos || object_end == std::string::npos || object_end < object_start)
      throw std::runtime_error("No valid JSON object found in LLM response.");

    return nlohmann::json::parse(response_text.substr(object_start, object_end - object_start + 1));
  }

  /**
   * @brief Client for the embedding service.
   */
  rclcpp::Client<prompt_msgs::srv::Embedding>::SharedPtr embed_client;

  /**
   * @brief Client for the prompt service.
   */
  rclcpp::Client<prompt_msgs::srv::Prompt>::SharedPtr prompt_client;

  /**
   * @brief Client for the tokenize service.
   */
  rclcpp::Client<prompt_msgs::srv::Tokenize>::SharedPtr tokenize_client_;

  /**
   * @brief Service name for the embedding service.
   */
  std::string embed_service_name_;

  /**
   * @brief embedding model name.
   */
  std::string embed_model_name_;

  /**
   * @brief Embedding model version.
   */
  std::string embed_model_family_;

  /**
   * @brief Service name for the prompt service.
   */
  std::string prompt_service_name_;

  /**
   * @brief Prompt model name.
   */
  std::string prompt_model_name_;

  /**
   * @brief Prompt model version.
   */
  std::string prompt_model_family_;

  /**
   * @brief Service name for the tokenize service.
   */
  std::string tokenize_service_name_;

  /**
   * @brief Tokenize model name.
   */
  std::string tokenize_model_name_;

  /**
   * @brief Tokenize model version.
   */
  std::string tokenize_model_family_;

  /**
   * @brief Number of tokens per document for tokenization.
   */
  int tokens_per_document_;

  /**
   * @brief Number of overlapping tokens for tokenization.
   */
  int tokens_per_document_overlap_;

  /**
   * @brief Number of tokens per embedding chunk for tokenization.
   */
  int tokens_per_embedding_;

  /**
   * @brief Number of overlapping tokens per embedding chunk for tokenization.
   */
  int tokens_per_embedding_overlap_;

  /**
   * @brief Node handle.
   */
  rclcpp::Node::SharedPtr node_;
};

}  // namespace fabric::experience::llm