#pragma once

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <fabric_base/utils/experience_structs.hpp>
#include <fabric_base/utils/metadata_structs.hpp>
#include <fabric_experience_core_grag/database/core_graphrag_utils.hpp>
#include <fabric_experience_core_grag/database/sqlite.hpp>
#include <fabric_experience_core_grag/database/structs.hpp>
#include <fabric_experience_core_grag/graph/GraphRAG.hpp>

namespace fabric::experience::graph::core_graphrag
{
using Parameter = ::fabric::capability::ParameterInfo;
using BehaviourInfo = ::fabric::capability::Descriptor;
using SemanticContextItem = ::fabric::experience::SemanticContextItem;

inline bool is_weak_parameter_edge(const SemanticContextItem& item)
{
  if (item.edge_type != "parameter_flow")
    return false;

  return item.edge_strength_class == "weak";
}

inline std::string classify_parameter_edge_strength(const double structural_score,
                                                    const size_t matched_required_inputs,
                                                    const size_t total_required_inputs,
                                                    const double edge_parameter_min_score,
                                                    const double edge_parameter_strong_score)
{
  const bool full_required_coverage = total_required_inputs == 0 || matched_required_inputs >= total_required_inputs;
  if (!full_required_coverage)
    return "weak";
  if (structural_score >= edge_parameter_strong_score)
    return "strong";
  if (structural_score >= edge_parameter_min_score)
    return "medium";
  return "weak";
}

inline std::vector<std::string> parameter_keys(const Parameter& parameter)
{
  std::vector<std::string> keys;

  if (!parameter.semantic_key.empty())
    keys.push_back(parameter.semantic_key);

  if (!parameter.name.empty() && std::find(keys.begin(), keys.end(), parameter.name) == keys.end())
    keys.push_back(parameter.name);

  for (const auto& alias : parameter.aliases)
  {
    if (!alias.empty() && std::find(keys.begin(), keys.end(), alias) == keys.end())
      keys.push_back(alias);
  }

  return keys;
}

inline bool parameter_types_compatible(const Parameter& output, const Parameter& input)
{
  return output.type.empty() || input.type.empty() || output.type == input.type;
}

inline bool parameters_match(const Parameter& output, const Parameter& input)
{
  if (!parameter_types_compatible(output, input))
    return false;

  const auto output_keys = parameter_keys(output);
  const auto input_keys = parameter_keys(input);

  for (const auto& output_key : output_keys)
  {
    if (std::find(input_keys.begin(), input_keys.end(), output_key) != input_keys.end())
      return true;
  }

  return false;
}

inline std::string preferred_parameter_key(const Parameter& parameter)
{
  if (!parameter.semantic_key.empty())
    return parameter.semantic_key;
  if (!parameter.name.empty())
    return parameter.name;
  if (!parameter.aliases.empty())
    return parameter.aliases.front();
  return "";
}

inline std::string build_parameter_relationship(const BehaviourInfo& source, const BehaviourInfo& target,
                                                const std::vector<std::string>& matched_keys,
                                                const size_t matched_required_inputs,
                                                const size_t total_required_inputs,
                                                const std::string& edge_strength_class)
{
  std::ostringstream relationship;
  relationship << source.interface_name << " provides runtime parameter flow to " << target.interface_name;

  if (!matched_keys.empty())
  {
    relationship << " via ";
    for (size_t index = 0; index < matched_keys.size(); ++index)
    {
      if (index > 0)
        relationship << ", ";
      relationship << matched_keys[index];
    }
  }

  if (total_required_inputs > 0)
  {
    relationship << ". matched " << matched_required_inputs << " of " << total_required_inputs
                 << " required runtime inputs";
  }

  if (!edge_strength_class.empty())
    relationship << ". strength class: " << edge_strength_class;

  return relationship.str();
}

inline double parameter_key_match_score(const Parameter& output, const Parameter& input)
{
  if (!output.semantic_key.empty() && output.semantic_key == input.semantic_key)
    return 1.0;
  if (!output.name.empty() && output.name == input.name)
    return 0.7;

  for (const auto& output_alias : output.aliases)
  {
    if (output_alias.empty())
      continue;

    if ((!input.semantic_key.empty() && output_alias == input.semantic_key) ||
        (!input.name.empty() && output_alias == input.name) ||
        std::find(input.aliases.begin(), input.aliases.end(), output_alias) != input.aliases.end())
      return 0.4;
  }

  return 0.0;
}

inline double parameter_type_score(const Parameter& output, const Parameter& input)
{
  if (output.type.empty() || input.type.empty())
    return 0.5;
  return output.type == input.type ? 1.0 : 0.0;
}

inline double parameter_edge_score(const double key_score, const double type_score,
                                   const size_t matched_required_inputs, const size_t total_required_inputs)
{
  const double coverage_score = total_required_inputs == 0 ? 1.0 :
                                                             static_cast<double>(matched_required_inputs) /
                                                                 static_cast<double>(total_required_inputs);
  const double role_score = 1.0;
  return (0.4 * key_score) + (0.2 * type_score) + (0.3 * coverage_score) + (0.1 * role_score);
}

inline void append_parameter_edges(const std::vector<BehaviourInfo>& info,
                                   std::vector<experience::database::EdgeInformation>& edge_infos,
                                   const double edge_parameter_min_score,
                                   const double edge_parameter_strong_score)
{
  for (size_t source_index = 0; source_index < info.size(); ++source_index)
  {
    const auto& source = info[source_index];
    if (source.runtime_output_parameters.empty())
      continue;

    for (size_t target_index = 0; target_index < info.size(); ++target_index)
    {
      if (source_index == target_index)
        continue;

      const auto& target = info[target_index];
      if (target.runtime_input_parameters.empty())
        continue;

      std::vector<std::string> matched_keys;
      std::unordered_set<std::string> matched_key_set;
      size_t matched_required_inputs = 0;
      size_t total_required_inputs = 0;
      double strongest_key_score = 0.0;
      double strongest_type_score = 0.0;

      for (const auto& input : target.runtime_input_parameters)
      {
        if (input.required)
          ++total_required_inputs;

        bool input_matched = false;
        std::string matched_key;

        for (const auto& output : source.runtime_output_parameters)
        {
          if (!parameters_match(output, input))
            continue;

          input_matched = true;
          matched_key = preferred_parameter_key(input);
          if (matched_key.empty())
            matched_key = preferred_parameter_key(output);
          strongest_key_score = std::max(strongest_key_score, parameter_key_match_score(output, input));
          strongest_type_score = std::max(strongest_type_score, parameter_type_score(output, input));
          break;
        }

        if (!input_matched)
          continue;

        if (input.required)
          ++matched_required_inputs;

        if (!matched_key.empty() && matched_key_set.insert(matched_key).second)
          matched_keys.push_back(matched_key);
      }

      if (matched_keys.empty())
        continue;

      experience::database::EdgeInformation edge_info;
      edge_info.source = source.interface_name;
      edge_info.target = target.interface_name;
      edge_info.edge_type = "parameter_flow";
      edge_info.matched_runtime_inputs = static_cast<int32_t>(matched_required_inputs);
      edge_info.total_runtime_inputs = static_cast<int32_t>(total_required_inputs);
      edge_info.matched_parameter_keys = matched_keys;
      edge_info.structural_score = parameter_edge_score(strongest_key_score, strongest_type_score,
                                                        matched_required_inputs, total_required_inputs);
      edge_info.edge_strength_class = classify_parameter_edge_strength(edge_info.structural_score,
                                                                       matched_required_inputs,
                                                                       total_required_inputs,
                                                                       edge_parameter_min_score,
                                                                       edge_parameter_strong_score);
      edge_info.relationship = build_parameter_relationship(source, target, matched_keys, matched_required_inputs,
                                                            total_required_inputs, edge_info.edge_strength_class);
      edge_infos.push_back(edge_info);
    }
  }
}

inline std::string nodeTypeToString(experience::database::NodeType type)
{
  switch (type)
  {
    case experience::database::NodeType::CAPABILITY:
      return "CAPABILITY";
    case experience::database::NodeType::TASK_ENTITY:
      return "TASK_ENTITY";
    case experience::database::NodeType::CONTEXT_ENTITY:
      return "CONTEXT_ENTITY";
    case experience::database::NodeType::PLAN_PATTERN:
      return "PLAN_PATTERN";
    case experience::database::NodeType::FAULT_CONCEPT:
      return "FAULT_CONCEPT";
    default:
      return "CONTEXT_ENTITY";
  }
}

inline SemanticContextItem semanticItemFromNode(const experience::database::NodeInformation& node,
                                                              const double relevance)
{
  SemanticContextItem item;
  item.id = node.id;
  item.name = node.name;
  item.provider = node.provider;
  item.description = node.descriptions;
  item.type = nodeTypeToString(node.type);
  item.relevance = relevance;
  return item;
}

inline SemanticContextItem semanticItemFromEdge(const experience::database::EdgeInformation& edge,
                                                              const double relevance)
{
  SemanticContextItem item;
  item.id = edge.id;
  item.name = edge.source + " -> " + edge.target;
  item.description = edge.relationship;
  item.type = "RELATIONSHIP";
  item.relationship = edge.relationship;
  item.source = edge.source;
  item.target = edge.target;
  item.edge_type = edge.edge_type;
  item.edge_strength_class = edge.edge_strength_class;
  item.structural_score = edge.structural_score;
  item.matched_runtime_inputs = edge.matched_runtime_inputs;
  item.total_runtime_inputs = edge.total_runtime_inputs;
  item.matched_parameter_keys = edge.matched_parameter_keys;
  item.relevance =
      edge.edge_type == "parameter_flow" ? ((0.6 * relevance) + (0.4 * edge.structural_score)) : relevance;
  item.feature_vector = {
    edge.edge_type == "parameter_flow" ? 1.0 : 0.0,
    edge.structural_score,
    static_cast<double>(edge.matched_runtime_inputs),
    static_cast<double>(edge.total_runtime_inputs),
  };
  return item;
}

inline std::string declaredRelationEdgeType(const std::string& predicate)
{
  const std::string normalized = experience::database::core_graphrag::normalize_id_segment(predicate);
  if (normalized == "depends_on")
    return "declared_dependency";
  return "declared_relation";
}

inline std::string classifyRelationship(const std::string& relationship)
{
  const std::string normalized = experience::database::core_graphrag::normalize_id_segment(relationship);
  if (normalized.find("fault") != std::string::npos || normalized.find("error") != std::string::npos ||
      normalized.find("warn") != std::string::npos || normalized.find("failure") != std::string::npos ||
      normalized.find("timeout") != std::string::npos || normalized.find("abort") != std::string::npos)
    return "fault";
  if (normalized.find("parallel") != std::string::npos || normalized.find("sequential") != std::string::npos ||
      normalized.find("recovery") != std::string::npos || normalized.find("retry") != std::string::npos ||
      normalized.find("fallback") != std::string::npos)
    return "pattern";
  return "dependency";
}

inline std::string classifySemanticEdge(const SemanticContextItem& item)
{
  if (item.edge_type == "declared_dependency" || item.edge_type == "parameter_flow")
    return "dependency";
  if (item.edge_type == "declared_relation")
  {
    const std::string relationship_category = classifyRelationship(item.relationship);
    if (relationship_category == "fault" || relationship_category == "pattern")
      return relationship_category;
    return "related";
  }
  return classifyRelationship(item.relationship);
}

inline std::vector<double> initialGraphNodeFeatures(const experience::database::NodeInformation& node)
{
  const double description_density = std::min(1.0, static_cast<double>(node.descriptions.size()) / 512.0);
  return {
    node.type == experience::database::NodeType::CAPABILITY ? 1.0 : 0.0,
    node.type == experience::database::NodeType::TASK_ENTITY ? 1.0 : 0.0,
    node.type == experience::database::NodeType::CONTEXT_ENTITY ? 1.0 : 0.0,
    node.type == experience::database::NodeType::PLAN_PATTERN ? 1.0 : 0.0,
    node.type == experience::database::NodeType::FAULT_CONCEPT ? 1.0 : 0.0,
    node.provider.empty() ? 0.0 : 1.0,
    description_density,
  };
}

inline void refreshGraphNodeFeatures(experience::GraphRAG& graph_rag, experience::database::SQLiteDB& inform_db)
{
  auto graph = graph_rag.get_graph();
  for (auto& [node_id, graph_node] : graph.nodes)
  {
    const auto node_info = inform_db.readNodeInformation(node_id);
    if (!node_info.has_value())
      continue;

    const double degree = static_cast<double>(graph.adjacency[node_id].size());
    const double description_density = std::min(1.0, static_cast<double>(node_info->descriptions.size()) / 512.0);
    graph_node.name = node_info->name;
    graph_node.feature_vector = {
      node_info->type == experience::database::NodeType::CAPABILITY ? 1.0 : 0.0,
      node_info->type == experience::database::NodeType::TASK_ENTITY ? 1.0 : 0.0,
      node_info->type == experience::database::NodeType::CONTEXT_ENTITY ? 1.0 : 0.0,
      node_info->type == experience::database::NodeType::PLAN_PATTERN ? 1.0 : 0.0,
      node_info->type == experience::database::NodeType::FAULT_CONCEPT ? 1.0 : 0.0,
      node_info->provider.empty() ? 0.0 : 1.0,
      std::min(1.0, degree / 8.0),
      description_density,
    };
  }
  graph_rag.set_graph(std::move(graph));
}
}  // namespace fabric::experience::graph::core_graphrag
