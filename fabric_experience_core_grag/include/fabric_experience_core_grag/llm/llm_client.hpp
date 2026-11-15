#pragma once

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <rclcpp/rclcpp.hpp>

#include <fabric_base/llm_analysis_base.hpp>

#include "fabric_experience_core_grag/llm/prompts.hpp"
#include "fabric_experience_core_grag/llm/structs.hpp"

namespace fabric::experience::llm
{

class PromptServiceClient
{
public:
  PromptServiceClient(const rclcpp::Node::SharedPtr& node, const std::string& plugin_name,
                      const std::shared_ptr<fabric::LlmAnalysisBase>& analysis_plugin)
  {
    node_ = node;
    analysis_plugin_ = analysis_plugin;

    if (!analysis_plugin_)
      throw std::invalid_argument("PromptServiceClient requires a configured LLM analysis plugin.");

    node_->declare_parameter(plugin_name + ".embed.model_name", "text-embedding-3-small");
    node_->declare_parameter(plugin_name + ".embed.model_family", "openai");
    node_->declare_parameter(plugin_name + ".prompt.model_name", "gpt-4");
    node_->declare_parameter(plugin_name + ".prompt.model_family", "openai");
    node_->declare_parameter(plugin_name + ".tokenize.model_name", "O200K_BASE");
    node_->declare_parameter(plugin_name + ".tokenize.model_family", "openai");
    node_->declare_parameter(plugin_name + ".tokenize.tokens_per_document", 20000);
    node_->declare_parameter(plugin_name + ".tokenize.tokens_per_document_overlap", 2000);
    node_->declare_parameter(plugin_name + ".tokenize.tokens_per_embedding", 8191);
    node_->declare_parameter(plugin_name + ".tokenize.tokens_per_embedding_overlap", 100);

    node_->get_parameter(plugin_name + ".embed.model_name", embed_model_name_);
    node_->get_parameter(plugin_name + ".embed.model_family", embed_model_family_);
    node_->get_parameter(plugin_name + ".prompt.model_name", prompt_model_name_);
    node_->get_parameter(plugin_name + ".prompt.model_family", prompt_model_family_);
    node_->get_parameter(plugin_name + ".tokenize.model_name", tokenize_model_name_);
    node_->get_parameter(plugin_name + ".tokenize.model_family", tokenize_model_family_);
    node_->get_parameter(plugin_name + ".tokenize.tokens_per_document", tokens_per_document_);
    node_->get_parameter(plugin_name + ".tokenize.tokens_per_document_overlap", tokens_per_document_overlap_);
    node_->get_parameter(plugin_name + ".tokenize.tokens_per_embedding", tokens_per_embedding_);
    node_->get_parameter(plugin_name + ".tokenize.tokens_per_embedding_overlap", tokens_per_embedding_overlap_);
  }

  ~PromptServiceClient() = default;

  std::vector<EmbeddingResult> compute_embedding(const std::string& text, int embedding_dim)
  {
    if (text.empty())
      throw std::invalid_argument("Input text for embedding is empty.");

    const auto tokens = encode_to_tokens(text);

    std::vector<std::vector<int32_t>> token_chunks;
    std::vector<std::string> text_chunks;
    if (tokens.size() > static_cast<size_t>(tokens_per_embedding_))
    {
      size_t start = 0;
      while (start < tokens.size())
      {
        const size_t end = std::min(start + static_cast<size_t>(tokens_per_embedding_), tokens.size());
        token_chunks.emplace_back(tokens.begin() + static_cast<std::ptrdiff_t>(start),
                                  tokens.begin() + static_cast<std::ptrdiff_t>(end));

        if (end == tokens.size())
          break;

        start = end - static_cast<size_t>(tokens_per_embedding_overlap_);
      }

      for (const auto& chunk : token_chunks)
        text_chunks.push_back(decode_from_tokens(chunk));
    }
    else
    {
      text_chunks.push_back(text);
    }

    std::vector<EmbeddingResult> results;
    for (const auto& chunk_text : text_chunks)
      results.push_back(compute_embedding_per_chunk(chunk_text, embedding_dim));

    return results;
  }

  std::vector<EntityInfo> extract_entities(const std::string& name, const std::string& text)
  {
    if (text.empty())
      throw std::invalid_argument("Input text for entity extraction is empty.");

    const std::string prompt_text = ENTITY_EXTRACTION_PROMPT + "Capability name: " + name + ". Text: ";
    const auto text_documents = process_into_documents(prompt_text, text);

    std::vector<EntityInfo> results;
    for (const auto& chunk_text : text_documents)
    {
      auto entities = extract_entities_per_document(chunk_text);
      results.insert(results.end(), entities.begin(), entities.end());
    }

    return results;
  }

  std::vector<RelationshipInfo> extract_relationships(const std::string& name, const std::string& text,
                                                      const std::vector<EntityInfo>& entities)
  {
    if (text.empty())
      throw std::invalid_argument("Input text for relationship extraction is empty.");

    std::string known_entities = "List of Entities: [";
    for (const auto& entity : entities)
      known_entities += "\"" + entity.name + "\", ";
    known_entities += "] ";

    const std::string system_prompt = RELATIONSHIP_EXTRACTION_PROMPT +
        "Capability name: " + name + ". " + known_entities + "Text: ";
    const auto text_documents = process_into_documents(system_prompt, text);

    std::vector<RelationshipInfo> relationships;
    for (const auto& chunk_text : text_documents)
    {
      auto rels = extract_relationships_per_document(chunk_text);
      relationships.insert(relationships.end(), rels.begin(), rels.end());
    }

    return relationships;
  }

  std::string combine_descriptions(const std::vector<std::string>& descriptions)
  {
    std::string combined;
    for (const auto& desc : descriptions)
      combined += desc + "\n";

    const std::string system_prompt = DESCRIPTION_COMBINATION_PROMPT + "Input Descriptions:";
    const auto text_documents = process_into_documents(system_prompt, combined);

    std::string final_description;
    for (const auto& chunk_text : text_documents)
      final_description += combine_descriptions_per_document(chunk_text) + "\n";

    return final_description;
  }

private:
  EmbeddingResult compute_embedding_per_chunk(const std::string& text, int embedding_dim)
  {
    EmbeddingResult result;
    result.text = text;

    const auto response = analysis_plugin_->embed(fabric::LlmEmbeddingRequest{
      text,
      embed_model_family_,
      embed_model_name_,
      embedding_dim,
    });

    if (!response.success)
      throw std::runtime_error(response.error_message.empty() ? "Embedding request failed." : response.error_message);
    if (response.embedding.empty())
      throw std::runtime_error("Received empty float embedding.");
    if (response.embedding.size() != static_cast<size_t>(embedding_dim))
      throw std::runtime_error("Received float embedding of incorrect dimension.");

    result.embedding = response.embedding;
    return result;
  }

  std::vector<EntityInfo> extract_entities_per_document(const std::string& text)
  {
    const auto response = analysis_plugin_->prompt(fabric::LlmPromptRequest{
      "",
      text,
      "",
      prompt_model_family_,
      prompt_model_name_,
      false,
      false,
      false,
      false,
    });

    if (!response.success)
      throw std::runtime_error(response.error_message.empty() ? "Prompt request failed." : response.error_message);

    auto json_response = extract_json(response.response);
    if (!json_response.contains("entities") || !json_response["entities"].is_array())
      throw std::runtime_error("JSON response does not contain valid 'entities' array.");

    std::vector<EntityInfo> entities;
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

    return entities;
  }

  std::vector<RelationshipInfo> extract_relationships_per_document(const std::string& text)
  {
    const auto response = analysis_plugin_->prompt(fabric::LlmPromptRequest{
      "",
      text,
      "",
      prompt_model_family_,
      prompt_model_name_,
      false,
      false,
      false,
      false,
    });

    if (!response.success)
      throw std::runtime_error(response.error_message.empty() ? "Prompt request failed." : response.error_message);

    auto json_response = extract_json(response.response);
    if (!json_response.contains("relationships") || !json_response["relationships"].is_array())
      throw std::runtime_error("JSON response does not contain valid 'relationships' array.");

    std::vector<RelationshipInfo> relationships;
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

    return relationships;
  }

  std::string combine_descriptions_per_document(const std::string& text)
  {
    const auto response = analysis_plugin_->prompt(fabric::LlmPromptRequest{
      "",
      text,
      "",
      prompt_model_family_,
      prompt_model_name_,
      false,
      false,
      false,
      false,
    });

    if (!response.success)
      throw std::runtime_error(response.error_message.empty() ? "Prompt request failed." : response.error_message);

    auto json_response = extract_json(response.response);
    if (!json_response.contains("combined_description") || !json_response["combined_description"].is_string())
      throw std::runtime_error("JSON response does not contain valid 'combined_description' string.");

    return json_response["combined_description"].get<std::string>();
  }

  std::vector<std::string> process_into_documents(const std::string& pretext, const std::string& text)
  {
    const auto pretext_tokens = encode_to_tokens(pretext);
    const int remaining_tokens = tokens_per_document_ - static_cast<int>(pretext_tokens.size());

    const auto tokens = encode_to_tokens(text);
    std::vector<std::vector<int32_t>> token_chunks;
    std::vector<std::string> text_documents;
    if (tokens.size() > static_cast<size_t>(remaining_tokens))
    {
      size_t start = 0;
      while (start < tokens.size())
      {
        const size_t end = std::min(start + static_cast<size_t>(remaining_tokens), tokens.size());
        token_chunks.emplace_back(tokens.begin() + static_cast<std::ptrdiff_t>(start),
                                  tokens.begin() + static_cast<std::ptrdiff_t>(end));

        if (end == tokens.size())
          break;

        start = end - static_cast<size_t>(tokens_per_document_overlap_);
      }

      for (const auto& chunk : token_chunks)
        text_documents.push_back(pretext + decode_from_tokens(chunk));
    }
    else
    {
      text_documents.push_back(pretext + text);
    }

    return text_documents;
  }

  std::vector<int32_t> encode_to_tokens(const std::string& text)
  {
    const auto response = analysis_plugin_->tokenize(fabric::LlmTokenizationRequest{
      fabric::LlmTokenizationRequest::Mode::ENCODE,
      text,
      {},
      tokenize_model_family_,
      tokenize_model_name_,
    });

    if (!response.success)
      throw std::runtime_error(response.error_message.empty() ? "Tokenize service reported failure." : response.error_message);

    return response.tokens;
  }

  std::string decode_from_tokens(const std::vector<int32_t>& tokens)
  {
    const auto response = analysis_plugin_->tokenize(fabric::LlmTokenizationRequest{
      fabric::LlmTokenizationRequest::Mode::DECODE,
      "",
      tokens,
      tokenize_model_family_,
      tokenize_model_name_,
    });

    if (!response.success)
      throw std::runtime_error(response.error_message.empty() ? "Tokenize service reported failure." : response.error_message);

    return response.text;
  }

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

  std::string embed_model_name_;
  std::string embed_model_family_;
  std::string prompt_model_name_;
  std::string prompt_model_family_;
  std::string tokenize_model_name_;
  std::string tokenize_model_family_;
  int tokens_per_document_{ 0 };
  int tokens_per_document_overlap_{ 0 };
  int tokens_per_embedding_{ 0 };
  int tokens_per_embedding_overlap_{ 0 };
  rclcpp::Node::SharedPtr node_;
  std::shared_ptr<fabric::LlmAnalysisBase> analysis_plugin_;
};

}  // namespace fabric::experience::llm
