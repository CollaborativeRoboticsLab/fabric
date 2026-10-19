#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <uuid/uuid.h>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/core_base.hpp>
#include <fabric_base/utils/exception.hpp>
#include <fabric_base/utils/experience_structs.hpp>
#include <fabric_base/utils/metadata_structs.hpp>
#include <fabric_base/utils/status_structs.hpp>
#include <fabric_experience_core_grag/database/core_graphrag_utils.hpp>
#include <fabric_experience_core_grag/database/sqlite.hpp>
#include <fabric_experience_core_grag/database/structs.hpp>
#include <fabric_experience_core_grag/graph/GraphRAG.hpp>
#include <fabric_experience_core_grag/graph/core_graphrag_utils.hpp>
#include <fabric_experience_core_grag/graph/structs.hpp>
#include <fabric_experience_core_grag/llm/core_graphrag_utils.hpp>
#include <fabric_experience_core_grag/llm/llm_client.hpp>
#include <fabric_experience_core_grag/llm/structs.hpp>
#include <fabric_experience_core_grag/vectordb/VectorDB.hpp>
#include <fabric_experience_core_grag/vectordb/structs.hpp>

namespace fabric
{

using CoreBase = ::fabric::CoreBase;
using BehaviourInfo = ::fabric::capability::Descriptor;
using SystemStatus = ::fabric::status::SystemStatus;
using CandidateSkillRequest = ::fabric::experience::CandidateSkillRequest;
using CandidateSkillResolution = ::fabric::experience::CandidateSkillResolution;
using CandidatePlanSkillAttribution = ::fabric::experience::CandidatePlanSkillAttribution;
using CandidatePlanEdgeAttribution = ::fabric::experience::CandidatePlanEdgeAttribution;
using PlanNodeScoreExplanationRecord = ::fabric::experience::PlanNodeScoreExplanationRecord;
using PlanNodeScoreExplanationQuery = ::fabric::experience::PlanNodeScoreExplanationQuery;
using PlanNodeScoreExplanationNodeQuery = ::fabric::experience::PlanNodeScoreExplanationNodeQuery;
using SemanticContextBundle = ::fabric::experience::SemanticContextBundle;
using SemanticContextItem = ::fabric::experience::SemanticContextItem;
using SelectedPlanScoreExplanationBundle = ::fabric::experience::SelectedPlanScoreExplanationBundle;
using PlanFeedbackRecord = ::fabric::experience::PlanFeedbackRecord;
using ScoringEpochRecord = ::fabric::experience::ScoringEpochRecord;
using PlanFeedbackCalibrationSummary = ::fabric::experience::PlanFeedbackCalibrationSummary;
using PlanFeedbackSelectionBiasSummary = ::fabric::experience::PlanFeedbackSelectionBiasSummary;
using EstimateResult = ::fabric::experience::EstimateResult;
using exception = ::fabric::fabric_exception;

class GraphKnowledgeCore;

/**
 * @brief Vector database plugin for XML plans.
 *
 * This plugin manages the vector database for XML plans.
 */
class CoreGraphRag : public CoreBase
{
public:
  CoreGraphRag() = default;
  virtual ~CoreGraphRag() override
  {
    shutdown();
  }
  /**
   * @brief Initialize the XML vector database plugin.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "CoreGraphRag");

    node_->declare_parameter<double>(plugin_name_ + ".edge.parameter.min_score", edge_parameter_min_score_);
    node_->declare_parameter<double>(plugin_name_ + ".edge.parameter.strong_score", edge_parameter_strong_score_);
    node_->declare_parameter<int>(plugin_name_ + ".edge.parameter.max_weak_dependencies",
                                  edge_parameter_max_weak_dependencies_);

    edge_parameter_min_score_ = node_->get_parameter(plugin_name_ + ".edge.parameter.min_score").as_double();
    edge_parameter_strong_score_ = node_->get_parameter(plugin_name_ + ".edge.parameter.strong_score").as_double();
    edge_parameter_max_weak_dependencies_ =
        node_->get_parameter(plugin_name_ + ".edge.parameter.max_weak_dependencies").as_int();

    if (!analysis_plugin_)
    {
      throw exception("CoreGraphRag requires a configured LLM analysis plugin.");
    }

    llmclient_ = std::make_shared<experience::llm::PromptServiceClient>(node_, "core_graphrag", analysis_plugin_);
    RCLCPP_INFO(node_->get_logger(), "Initialized shared analysis client for CoreGraphRag.");

    vector_db_ =
      std::make_shared<experience::VectorDB>(node_, "core_graphrag", experience::vectordb::MetricType::COSINE, llmclient_);
    RCLCPP_INFO(node_->get_logger(), "Initialized VectorDB for CoreGraphRag.");

    graph_rag_ = std::make_shared<experience::GraphRAG>(node_);
    RCLCPP_INFO(node_->get_logger(), "Initialized GraphRAG for CoreGraphRag.");

    inform_db_ = std::make_shared<experience::database::SQLiteDB>(node_, "core_graphrag.sqlite3");
    inform_db_->open();
    inform_db_->createNodeInformationTable();
    inform_db_->createEdgeInformationTable();
    inform_db_->createCommunityInformationTable();
    inform_db_->createEmbeddingTable();
    inform_db_->createGraphNodeTable();
    inform_db_->createGraphEdgeTable();
    inform_db_->createGraphCommunityTable();
    inform_db_->createSkillRunStatisticsTable();
    inform_db_->createSkillContextStatisticsTable();
    inform_db_->createSkillEdgeStatisticsTable();
    inform_db_->createPlanFeedbackStatisticsTable();
    inform_db_->createScoringEpochStatisticsTable();
    inform_db_->createPlanSkillAttributionTable();
    inform_db_->createPlanSkillEdgeAttributionTable();
    inform_db_->createPlanNodeScoreExplanationTable();
    RCLCPP_INFO(node_->get_logger(), "Initialized SQLiteDB for CoreGraphRag.");
  }

  /**
   * @brief Build the knowledge core from capability information
   *
   * @param info Vector containing capability information.
   */
  void build(std::vector<BehaviourInfo>& info) override
  {
    RCLCPP_INFO(node_->get_logger(), "Building CoreGraphRag with %zu capabilities.", info.size());

    // Temporary storage for node and edge information until processing is complete
    std::vector<experience::database::NodeInformation> node_infos;
    std::vector<experience::database::EdgeInformation> edge_infos;

    // Step 1: Populate node_infos and edge_infos from capabilities and their relations
    for (const auto& cap : info)
    {
      // Add the main capability node for this /provider combination.
      experience::database::NodeInformation node_info;
      node_info.name = cap.interface_name;
      node_info.provider = cap.provider_name;
      node_info.descriptions = cap.interface_description;
      if (!cap.provider_description.empty())
      {
        if (!node_info.descriptions.empty())
          node_info.descriptions += "\n";
        node_info.descriptions += cap.provider_description;
      }
      node_info.type = experience::database::NodeType::CAPABILITY;
      node_infos.push_back(node_info);
      RCLCPP_DEBUG(node_->get_logger(), "[CoreGraphRag] Added capability node: %s", cap.interface_name.c_str());

      // Add edges for relations (related nodes may be added as placeholders, will be merged later)
      for (const auto& relation : cap.relations)
      {
        // Add a placeholder node for the related capability (if not already present, will be merged by name)
        experience::database::NodeInformation related_node_info;
        related_node_info.name = relation.object;
        related_node_info.type = experience::database::NodeType::CAPABILITY;
        node_infos.push_back(related_node_info);
        RCLCPP_DEBUG(node_->get_logger(), "[CoreGraphRag] Added related capability placeholder node: %s",
                     relation.object.c_str());

        // Add the edge representing the relationship between capabilities
        experience::database::EdgeInformation edge_info;
        edge_info.source = cap.interface_name;
        edge_info.target = relation.object;
        edge_info.relationship = relation.predicate;
        edge_info.edge_type = experience::graph::core_graphrag::declaredRelationEdgeType(relation.predicate);
        edge_infos.push_back(edge_info);
        RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Added edge: %s --(%s)--> %s", edge_info.source.c_str(),
                    edge_info.relationship.c_str(), edge_info.target.c_str());
      }
    }

    experience::graph::core_graphrag::append_parameter_edges(info, edge_infos, edge_parameter_min_score_,
                                  edge_parameter_strong_score_);

    // Step 2: For each capability, extract entities and relationships using the LLM
    for (const auto& cap : info)
    {
      // Use LLM to extract entities from the capability's description
      auto entities = llmclient_->extract_entities(cap.interface_name,
                       experience::llm::core_graphrag::node_description(cap));

      // Add extracted entities to node_infos
      for (const auto& entity : entities)
      {
        RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Extracted entity: %s (type: %s)", entity.name.c_str(),
                    entity.type.c_str());

        experience::database::NodeInformation entity_node_info;
        entity_node_info.name = entity.name;
        entity_node_info.descriptions = entity.description;

        entity_node_info.type = experience::llm::core_graphrag::nodeTypeFromExtractionLabel(entity.type);

        node_infos.push_back(entity_node_info);
      }

      // Use LLM to extract relationships between entities for this capability
      auto relationships = llmclient_->extract_relationships(cap.interface_name,
                         experience::llm::core_graphrag::node_description(cap),
                         entities);

      // Add each relationship as an edge in the graph
      for (const auto& rel : relationships)
      {
        RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Extracted relationship: %s --(%s)--> %s", rel.source.c_str(),
                    rel.relationship.c_str(), rel.target.c_str());

        experience::database::EdgeInformation edge_info;
        edge_info.source = rel.source;
        edge_info.target = rel.target;
        edge_info.relationship = rel.relationship;
        edge_infos.push_back(edge_info);
      }
    }

    // Step 3: Merge duplicate nodes (by name), combining descriptions and providers as needed
    // This is necessary because placeholder nodes for relations may have been added earlier
    std::vector<experience::database::NodeInformation> node_list;

    for (int i = 0; i < node_infos.size(); ++i)
    {
      // already merged so skip
      if (node_infos[i].is_valid() == false)
        continue;

      // Create new node rather than modifying in place to avoid issues with references
      experience::database::NodeInformation node;
      node.name = node_infos[i].name;
      node.provider = node_infos[i].provider;
      node.type = node_infos[i].type;

      // Temporary storage for descriptions to be combined
      std::vector<std::string> descriptions;
      descriptions.push_back(node_infos[i].descriptions);

      // Check the following nodes for duplicates by name
      for (int j = i + 1; j < node_infos.size(); ++j)
      {
        // A duplicate found
        if (node.name == node_infos[j].name)
        {
          RCLCPP_DEBUG(node_->get_logger(), "[CoreGraphRag] Found duplicate node: %s", node_infos[i].name.c_str());

          // If the newly created node's provider is empty, use the duplicate's provider
          if (node.provider.empty() && !node_infos[j].provider.empty())
            node.provider = node_infos[j].provider;

          // Prefer a more specific duplicate type over the default context classification.
          if (node.type == experience::database::NodeType::CONTEXT_ENTITY &&
              node_infos[j].type != experience::database::NodeType::CONTEXT_ENTITY)
            node.type = node_infos[j].type;

          // Merge description from the duplicate node
          descriptions.push_back(node_infos[j].descriptions);

          // Clear the duplicate node to mark it as merged and skip later
          node_infos[j].clear();

          RCLCPP_DEBUG(node_->get_logger(), "[CoreGraphRag] Merged duplicate node: %s", node.name.c_str());
        }
      }

      if (descriptions.size() > 1)
      {
        RCLCPP_DEBUG(node_->get_logger(), "[CoreGraphRag] Combining %zu descriptions for node: %s", descriptions.size(),
                     node.name.c_str());
        // get the combined description of individual descriptions to form a coherent whole
        node.descriptions = llmclient_->combine_descriptions(descriptions);
      }
      else
        node.descriptions = descriptions.front();

      node_list.push_back(node);
    }

    // Step 4: Merge duplicate edges (by source and target), combining relationship descriptions as needed
    // This ensures only one edge exists between any two nodes, with a combined relationship description

    std::vector<experience::database::EdgeInformation> edge_list;

    for (int i = 0; i < edge_infos.size(); ++i)
    {
      // already merged so skip
      if (edge_infos[i].is_valid() == false)
        continue;

      experience::database::EdgeInformation edge;
      edge.source = edge_infos[i].source;
      edge.target = edge_infos[i].target;

      std::vector<std::string> relationships;
      relationships.push_back(edge_infos[i].relationship);

      // Check for other edges with the same source and target to merge
      for (int j = i + 1; j < edge_infos.size(); ++j)
      {
        // If another edge has the same source and target but is a different object, merge it
        if (edge.source == edge_infos[j].source && edge.target == edge_infos[j].target)
        {
          relationships.push_back(edge_infos[j].relationship);
          // Clear the duplicate edge to mark it as merged and skip later
          edge_infos[j].clear();

          RCLCPP_DEBUG(node_->get_logger(), "[CoreGraphRag] Merged duplicate edge: %s --(%s)--> %s",
                       edge.source.c_str(), edge_infos[j].relationship.c_str(), edge.target.c_str());
        }
      }

      // If there are multiple relationships, combine them using the LLM
      if (relationships.size() > 1)
      {
        edge.relationship = llmclient_->combine_descriptions(relationships);

        RCLCPP_DEBUG(node_->get_logger(), "[CoreGraphRag] Combined relationships for edge: %s -> %s",
                     edge.source.c_str(), edge.target.c_str());
      }
      else
        edge.relationship = relationships.front();

      edge_list.push_back(edge);
    }

    // Ensure every edge endpoint has a semantic node before IDs are assigned.
    std::unordered_set<std::string> known_node_names;
    for (const auto& node_info : node_list)
    {
      if (node_info.is_valid())
        known_node_names.insert(node_info.name);
    }

    for (const auto& edge_info : edge_list)
    {
      for (const auto& endpoint : { edge_info.source, edge_info.target })
      {
        if (endpoint.empty() || known_node_names.count(endpoint) > 0)
          continue;

        experience::database::NodeInformation endpoint_node_info;
        endpoint_node_info.name = endpoint;
        endpoint_node_info.type = experience::database::NodeType::CONTEXT_ENTITY;
        node_list.push_back(endpoint_node_info);
        known_node_names.insert(endpoint);

        RCLCPP_WARN(node_->get_logger(),
                    "[CoreGraphRag] Synthesized missing node for edge endpoint before persistence: %s",
                    endpoint.c_str());
      }
    }

    // Step 5: Upsert nodes into vector DB, graph, and SQL DB
    std::unordered_map<std::string, std::string> node_ids_by_name;
    for (auto& node_info : node_list)
    {
      // Generate a unique ID for the node
      node_info.id = generate_uuid();
      node_ids_by_name[node_info.name] = node_info.id;

      // Store node information in the SQL database
      inform_db_->writeNodeInformation(node_info);

      RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Upserted node into database for node: %s (uid: %s)",
                  node_info.name.c_str(), node_info.id.c_str());

      // Upsert node embedding into the vector database
      if (!node_info.descriptions.empty())
      {
        vector_db_->upsert(node_info.id, node_info.descriptions);

        RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Upserted embedding for node: %s (uid: %s)",
                    node_info.name.c_str(), node_info.id.c_str());
      }

      // Add node to the graph structure
      experience::graph::NodeInfo graph_node_info;
      graph_node_info.id = node_info.id;
      graph_node_info.name = node_info.name;
      graph_node_info.level = 0;
      graph_node_info.cluster_id = 0;
      graph_node_info.feature_vector = experience::graph::core_graphrag::initialGraphNodeFeatures(node_info);
      graph_rag_->upsert_node(graph_node_info.id, graph_node_info);
      inform_db_->writeGraphNode(graph_node_info);

      RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Added graph node: %s (uid: %s)", node_info.name.c_str(),
                  node_info.id.c_str());
    }

    // Step 6: Upsert edges into vector DB, graph, and SQL DB
    for (auto& edge_info : edge_list)
    {
      edge_info.id = generate_uuid();

      const std::string source_name = edge_info.source;
      const std::string target_name = edge_info.target;

      const auto source_it = node_ids_by_name.find(source_name);
      const auto target_it = node_ids_by_name.find(target_name);
      if (source_it == node_ids_by_name.end() || target_it == node_ids_by_name.end())
      {
        RCLCPP_WARN(node_->get_logger(), "[CoreGraphRag] Skipping edge with unresolved node IDs: %s -> %s",
                    source_name.c_str(), target_name.c_str());
        continue;
      }

      edge_info.source = source_it->second;
      edge_info.target = target_it->second;

      // Store edge information in the SQL database with stable node ID endpoints.
      inform_db_->writeEdgeInformation(edge_info);
      RCLCPP_INFO(node_->get_logger(),
                  "[CoreGraphRag] Upserted edge information into database for edge: %s -> %s (uid: %s)",
                  source_name.c_str(), target_name.c_str(), edge_info.id.c_str());

      // Upsert edge embedding into the vector database
      if (!edge_info.relationship.empty())
      {
        vector_db_->upsert(edge_info.id, edge_info.relationship);

        RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Upserted embedding for edge: %s -> %s (uid: %s)",
                    edge_info.source.c_str(), edge_info.target.c_str(), edge_info.id.c_str());
      }

      // Add edge to the graph structure
      experience::graph::EdgeInfo graph_edge_info;
      graph_edge_info.id = edge_info.id;
      graph_edge_info.source = edge_info.source;
      graph_edge_info.target = edge_info.target;
      graph_rag_->upsert_edge(graph_edge_info);
      inform_db_->writeGraphEdge(graph_edge_info);

      RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Added graph edge: %s -> %s (uid: %s)", source_name.c_str(),
                  target_name.c_str(), edge_info.id.c_str());
    }

    experience::graph::core_graphrag::refreshGraphNodeFeatures(*graph_rag_, *inform_db_);

    // Identify communities in the graph using the specified clustering algorithm
    auto communities = graph_rag_->create_communities();

    RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Identified %zu communities in the graph.", communities.size());

    // Process communities
    for (auto& kv : communities)
    {
      // generate unique community id and overwrite temporary key for persistence
      kv.second.id = generate_uuid();
      RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Creating community %s with %zu nodes and %zu edges.",
                  kv.second.id.c_str(), kv.second.node_ids.size(), kv.second.edge_ids.size());

      // generate description for the community based in its nodes and edges
      std::vector<std::string> community_descriptions;

      // pull descriptions from nodes in the community
      for (const auto& node_id : kv.second.node_ids)
      {
        auto node_info = inform_db_->readNodeInformation(node_id);
        if (node_info.has_value() && !node_info->descriptions.empty())
        {
          community_descriptions.push_back(node_info->descriptions);
          RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Community %s includes node %s (%s).",
                      kv.second.id.c_str(), node_id.c_str(), node_info->name.c_str());
        }
      }

      RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Pulled descriptions from nodes for community: %s",
                  kv.second.id.c_str());

      // pull relationships from edges in the community
      for (const auto& edge_id : kv.second.edge_ids)
      {
        auto edge_info = inform_db_->readEdgeInformation(edge_id);
        if (edge_info.has_value() && !edge_info->relationship.empty())
        {
          std::string information = edge_info->source + " " + edge_info->relationship + " " + edge_info->target;
          community_descriptions.push_back(information);
          RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Community %s includes edge %s: %s --(%s)--> %s.",
                      kv.second.id.c_str(), edge_id.c_str(), edge_info->source.c_str(),
                      edge_info->relationship.c_str(), edge_info->target.c_str());
        }
      }

      RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Pulled relationships from edges for community: %s",
                  kv.second.id.c_str());

      // create community information struct for database insertion. create a combined description
      experience::database::CommunityInformation community_info;
      community_info.id = kv.second.id;
      community_info.description = llmclient_->combine_descriptions(community_descriptions);

      RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Generated description for community: %s", kv.second.id.c_str());

      // persist community information
      graph_rag_->upsert_community(kv.second);
      inform_db_->writeCommunityInformation(community_info);
      vector_db_->upsert(community_info.id, community_info.description);

      RCLCPP_INFO(node_->get_logger(), "[CoreGraphRag] Upserted community: %s", kv.second.id.c_str());
    }

    RCLCPP_INFO(node_->get_logger(), "Completed building CoreGraphRag.");

    inform_db_->writeGraph(graph_rag_->get_graph());
    RCLCPP_INFO(node_->get_logger(), "Persisted graph structure to database.");

    inform_db_->writeVectorDB(vector_db_->get_embeddings());
    RCLCPP_INFO(node_->get_logger(), "Persisted vector embeddings to database.");
  }

  void shutdown() override
  {
    if (inform_db_)
    {
      inform_db_->flush();
      inform_db_->close();
    }
  }

  /**
   * @brief Update the core with runtime supervisor evidence.
   *
   * @param status Internal system status record converted from supervisor telemetry.
   */
  void update(const SystemStatus& status) override
  {
    size_t run_evidence_count = 0;
    size_t persisted_run_evidence_count = 0;

    for (const auto& capability : status.capabilities)
    {
      if (!capability.has_run_evidence && !capability.has_lifecycle)
      {
        continue;
      }

      const std::string skill_id = experience::database::core_graphrag::normalize_id_segment(capability.capability_name);
      const std::string planning_scope = status.planning_request_id.empty() ? status.plan_id : status.planning_request_id;
      const std::string candidate_scope = status.candidate_plan_id.empty() ? "unknown_candidate" : status.candidate_plan_id;
      const std::string graph_scope = status.graph_hash.empty() ? "unknown_graph" : status.graph_hash;

      experience::database::SkillRunStatistics data;
      data.execution_window_id = experience::database::core_graphrag::normalize_id_segment(planning_scope) + "/" +
                                 experience::database::core_graphrag::normalize_id_segment(candidate_scope) + "/" +
                                 experience::database::core_graphrag::normalize_id_segment(graph_scope) + "/" +
                                 experience::database::core_graphrag::normalize_id_segment(status.plan_id) + "/" +
                                 experience::database::core_graphrag::normalize_id_segment(capability.instance_id) +
                                 "/" + skill_id;
      data.plan_id = status.plan_id;
      data.planning_request_id = status.planning_request_id;
      data.candidate_plan_id = status.candidate_plan_id;
      data.graph_hash = status.graph_hash;
      data.bond_id.clear();
      data.instance_id = capability.instance_id;
      data.skill_id = skill_id;
      data.skill_name = capability.capability_name;
      data.fabric_status = status.capability_plan_state;
      data.finalized = experience::database::core_graphrag::is_finalized(status, capability);
      data.is_active = !data.finalized && capability.has_lifecycle && capability.lifecycle.is_active;
      data.start_sec = capability.lifecycle.last_start_time.sec;
      data.start_nanosec = capability.lifecycle.last_start_time.nanosec;
      data.stop_sec = capability.lifecycle.last_stop_time.sec;
      data.stop_nanosec = capability.lifecycle.last_stop_time.nanosec;
      data.runs_started = capability.has_run_evidence ? capability.run_evidence.runs_started : 0;
      data.runs_completed = capability.has_run_evidence ? capability.run_evidence.runs_completed : 0;
      data.runs_succeeded = capability.has_run_evidence ? capability.run_evidence.runs_succeeded : 0;
      data.runs_failed = capability.has_run_evidence ? capability.run_evidence.runs_failed : 0;
      data.runs_aborted = capability.has_run_evidence ? capability.run_evidence.runs_aborted : 0;
      data.runs_timed_out = capability.has_run_evidence ? capability.run_evidence.runs_timed_out : 0;
      data.runtime_sample_count = 0;
      data.runtime_sum_seconds = 0.0;
      data.runtime_sum_squares_seconds = 0.0;
      data.energy_sample_count = 0;
      data.energy_sum_delta_percentage = 0.0;
      data.energy_sum_squares_delta_percentage = 0.0;
      experience::database::core_graphrag::accumulate_interval_evidence(capability, data);
      experience::database::core_graphrag::accumulate_fault_evidence(capability, data);
      data.resource_missing_dimensions = "cpu,memory,network,storage,actuator";
      data.observed_at_sec = status.header.stamp.sec;
      data.observed_at_nanosec = status.header.stamp.nanosec;
      data.last_outcome_sec = capability.has_run_evidence ? capability.run_evidence.last_outcome_time.sec : 0;
      data.last_outcome_nanosec = capability.has_run_evidence ? capability.run_evidence.last_outcome_time.nanosec : 0;
      data.last_outcome_label = capability.has_run_evidence ? capability.run_evidence.last_outcome_label : "";
      data.estimator_window = "all";
      data.selected_context_key = "skill:" + skill_id;
      data.missing_field_flags = experience::database::core_graphrag::missing_field_flags(status, capability);

      if (!inform_db_->writeSkillRunStatistics(data))
      {
        RCLCPP_WARN(node_->get_logger(), "[CoreGraphRag] Failed to persist run evidence for capability: %s",
                    capability.capability_name.c_str());
        continue;
      }

      ++run_evidence_count;
      ++persisted_run_evidence_count;
    }

    RCLCPP_INFO(node_->get_logger(),
                "[CoreGraphRag] Injected supervisor run evidence: plan_id=%s planning_request_id=%s candidate_plan_id=%s graph_hash=%s received=%zu persisted=%zu",
                status.plan_id.c_str(), status.planning_request_id.c_str(), status.candidate_plan_id.c_str(),
                status.graph_hash.c_str(), run_evidence_count, persisted_run_evidence_count);
  }

  /**
   * @brief Load the graph structure from the database and verify its integrity
   * against capability information.
   *
   * @param info Vector containing capability information.
   *
   * @return true if the graph was loaded successfully, false otherwise.
  * @throws experience::fabric_exception if integrity check of loaded graph fails.
   */
  void load(std::vector<BehaviourInfo>& info) override
  {
    // Load graph structure from the database
    auto graph = inform_db_->readGraph();
    std::unordered_map<std::string, std::string> node_ids_by_name;
    if (graph.has_value())
    {
      for (const auto& [node_id, graph_node] : graph->nodes)
      {
        auto node_info = inform_db_->readNodeInformation(node_id);
        if (node_info.has_value())
          node_ids_by_name[node_info->name] = node_id;
      }

      graph_rag_->set_graph(std::move(graph.value()));
    }
    RCLCPP_INFO(node_->get_logger(), "Loaded graph structure from database.");

    // Load vector embeddings from the database
    auto embeddings = inform_db_->readVectorDB();
    if (embeddings.has_value())
      vector_db_->set_embeddings(embeddings.value());
    RCLCPP_INFO(node_->get_logger(), "Loaded vector embeddings from database.");

    // Verify integrity of loaded graph against capability information
    for (const auto& cap : info)
    {
      // Check if the capability node exists in the graph
      const auto capability_node_it = node_ids_by_name.find(cap.interface_name);
      if (capability_node_it == node_ids_by_name.end() || !graph_rag_->has_node(capability_node_it->second))
      {
        RCLCPP_WARN(node_->get_logger(), "Graph is missing node for capability: %s", cap.interface_name.c_str());
        throw exception("Graph integrity check failed: missing capability node.");
      }

      // Check if all relations are represented as edges in the graph
      for (const auto& relation : cap.relations)
      {
        const auto relation_node_it = node_ids_by_name.find(relation.object);
        if (relation_node_it == node_ids_by_name.end() ||
            !graph_rag_->has_edge(capability_node_it->second, relation_node_it->second))
        {
          RCLCPP_WARN(node_->get_logger(), "Graph is missing edge for relation: %s --(%s)--> %s",
                      cap.interface_name.c_str(), relation.predicate.c_str(), relation.object.c_str());
          throw exception("Graph integrity check failed: missing capability edge.");
        }
      }
    }
  }

  /**
   * @brief Resolve the best evidence row for a candidate skill request.
   *
   * @param request Candidate skill identity and context request.
   * @return CandidateSkillResolution Resolved evidence summary and fallback trace.
   */
  CandidateSkillResolution resolveCandidateSkill(const CandidateSkillRequest& request) override
  {
    CandidateSkillResolution resolution;
    resolution.skill_name = request.skill_name;
    resolution.skill_id =
        request.skill_id.empty() ? experience::database::core_graphrag::normalize_id_segment(request.skill_name) :
                 experience::database::core_graphrag::normalize_id_segment(request.skill_id);
    resolution.selected_skill_id = resolution.skill_id;
    resolution.requested_context_key = request.context_key;
    resolution.requested_context_class = request.context_class;

    auto selection = inform_db_->selectSkillStatistics(resolution.skill_id, request.context_key, request.context_class);
    if (request.allow_proxy_fallback && selection.level == experience::database::StatisticsFallbackLevel::PRIOR_DEFAULT)
    {
      selection = inform_db_->selectSkillStatisticsWithSemanticFallback(resolution.skill_id, request.skill_name,
                                                                        request.context_key, request.context_class);
    }

    resolution.selected_skill_id =
        selection.selected_skill_id.empty() ? resolution.skill_id : selection.selected_skill_id;
    resolution.selected_key = selection.selected_key;
    resolution.estimator_window = selection.estimator_window;
    resolution.fallback_level = experience::database::core_graphrag::fallbackLevelToString(selection.level);
    resolution.proxy_source = selection.proxy_source.empty() ? "requested_skill" : selection.proxy_source;
    resolution.reason = selection.reason;
    resolution.has_empirical_evidence = selection.level != experience::database::StatisticsFallbackLevel::PRIOR_DEFAULT;
    resolution.evidence = experience::database::core_graphrag::estimatorEvidenceFromSummary(selection.statistics);
    return resolution;
  }

  /**
   * @brief Persist skill attribution for a candidate plan node.
   *
   * @param attribution Candidate plan skill attribution record.
   * @return bool True when the record was stored successfully.
   */
  bool recordCandidatePlanSkillAttribution(const CandidatePlanSkillAttribution& attribution) override
  {
    experience::database::PlanSkillAttribution data;
    data.planning_request_id = attribution.planning_request_id;
    data.candidate_plan_id = attribution.candidate_plan_id;
    data.graph_hash = attribution.graph_hash;
    data.candidate_node_id = attribution.candidate_node_id;
    data.runtime_plan_id = attribution.runtime_plan_id;
    data.runtime_instance_id = attribution.runtime_instance_id;
    data.skill_name = attribution.skill_name;
    data.skill_id = attribution.skill_id.empty() ?
              experience::database::core_graphrag::normalize_id_segment(attribution.skill_name) :
              experience::database::core_graphrag::normalize_id_segment(attribution.skill_id);
    data.context_key = attribution.context_key;
    data.context_class = attribution.context_class;
    data.selected_skill_id =
      attribution.selected_skill_id.empty() ? data.skill_id :
                          experience::database::core_graphrag::normalize_id_segment(
                            attribution.selected_skill_id);
    data.selected_key = attribution.selected_key;
    data.estimator_window = attribution.estimator_window;
    data.fallback_level = attribution.fallback_level;
    data.proxy_source = attribution.proxy_source;
    data.selected = attribution.selected;
    data.terminal_success = attribution.terminal_success;
    data.terminal_outcome = attribution.terminal_outcome;
    data.recorded_at_sec = attribution.recorded_at_sec;
    data.recorded_at_nanosec = attribution.recorded_at_nanosec;
    data.provenance = attribution.provenance;
    return inform_db_->writePlanSkillAttribution(data);
  }

  /**
   * @brief Persist edge attribution for a candidate plan edge.
   *
   * @param attribution Candidate plan edge attribution record.
   * @return bool True when the record was stored successfully.
   */
  bool recordCandidatePlanEdgeAttribution(const CandidatePlanEdgeAttribution& attribution) override
  {
    experience::database::PlanSkillEdgeAttribution data;
    data.planning_request_id = attribution.planning_request_id;
    data.candidate_plan_id = attribution.candidate_plan_id;
    data.graph_hash = attribution.graph_hash;
    data.source_node_id = attribution.source_node_id;
    data.target_node_id = attribution.target_node_id;
    data.source_skill_id = experience::database::core_graphrag::normalize_id_segment(attribution.source_skill_id);
    data.target_skill_id = experience::database::core_graphrag::normalize_id_segment(attribution.target_skill_id);
    data.edge_label = attribution.edge_label;
    data.selected = attribution.selected;
    data.terminal_success = attribution.terminal_success;
    data.terminal_outcome = attribution.terminal_outcome;
    data.recorded_at_sec = attribution.recorded_at_sec;
    data.recorded_at_nanosec = attribution.recorded_at_nanosec;
    data.provenance = attribution.provenance;
    return inform_db_->writePlanSkillEdgeAttribution(data);
  }

  /**
   * @brief Persist a node-level score explanation record.
   *
   * @param record Node score explanation to persist.
   * @return bool True when the record was stored successfully.
   */
  bool recordPlanNodeScoreExplanation(const PlanNodeScoreExplanationRecord& record) override
  {
    experience::database::PlanNodeScoreExplanation data;
    data.planning_request_id = record.planning_request_id;
    data.candidate_plan_id = record.candidate_plan_id;
    data.graph_hash = record.graph_hash;
    data.candidate_node_id = record.candidate_node_id;
    data.skill_name = record.skill_name;
    data.skill_id = record.skill_id.empty() ?
              experience::database::core_graphrag::normalize_id_segment(record.skill_name) :
              experience::database::core_graphrag::normalize_id_segment(record.skill_id);
    data.requested_context_key = record.requested_context_key;
    data.requested_context_class = record.requested_context_class;
    data.selected_skill_id =
      record.selected_skill_id.empty() ? data.skill_id :
                         experience::database::core_graphrag::normalize_id_segment(
                           record.selected_skill_id);
    data.selected_key = record.selected_key;
    data.estimator_window = record.estimator_window;
    data.fallback_level = record.fallback_level;
    data.proxy_source = record.proxy_source;
    data.reason = record.reason;
    data.semantic_familiarity = record.semantic_familiarity;
    data.semantic_relevant_capabilities = record.semantic_relevant_capabilities;
    data.semantic_related_entities = record.semantic_related_entities;
    data.semantic_dependencies = record.semantic_dependencies;
    data.semantic_plan_patterns = record.semantic_plan_patterns;
    data.semantic_fault_concepts = record.semantic_fault_concepts;
    data.semantic_community_summaries = record.semantic_community_summaries;
    // The DB layer stores estimator outputs as parallel primitive arrays so explanation rows remain easy to persist
    // and query without introducing a second child table for per-node estimator values.
    for (const auto& output : record.estimator_outputs)
    {
      data.estimator_names.push_back(output.name);
      data.estimator_values.push_back(output.value);
      data.estimator_available.push_back(output.available);
      data.estimator_reasons.push_back(output.reason);
    }
    data.recorded_at_sec = record.recorded_at_sec;
    data.recorded_at_nanosec = record.recorded_at_nanosec;
    data.provenance = record.provenance;
    return inform_db_->writePlanNodeScoreExplanation(data);
  }

  /**
   * @brief Read all node score explanations for a candidate plan.
   *
   * @param query Candidate plan explanation query.
   * @return std::vector<PlanNodeScoreExplanationRecord> Matching node explanation records.
   */
  std::vector<PlanNodeScoreExplanationRecord>
  readPlanNodeScoreExplanations(const PlanNodeScoreExplanationQuery& query) override
  {
    std::vector<PlanNodeScoreExplanationRecord> records;
    const auto rows =
        inform_db_->readPlanNodeScoreExplanations(query.planning_request_id, query.candidate_plan_id, query.graph_hash);
    records.reserve(rows.size());
    for (const auto& row : rows)
    {
      PlanNodeScoreExplanationRecord record;
      record.planning_request_id = row.planning_request_id;
      record.candidate_plan_id = row.candidate_plan_id;
      record.graph_hash = row.graph_hash;
      record.candidate_node_id = row.candidate_node_id;
      record.skill_name = row.skill_name;
      record.skill_id = row.skill_id;
      record.requested_context_key = row.requested_context_key;
      record.requested_context_class = row.requested_context_class;
      record.selected_skill_id = row.selected_skill_id;
      record.selected_key = row.selected_key;
      record.estimator_window = row.estimator_window;
      record.fallback_level = row.fallback_level;
      record.proxy_source = row.proxy_source;
      record.reason = row.reason;
      record.semantic_familiarity = row.semantic_familiarity;
      record.semantic_relevant_capabilities = row.semantic_relevant_capabilities;
      record.semantic_related_entities = row.semantic_related_entities;
      record.semantic_dependencies = row.semantic_dependencies;
      record.semantic_plan_patterns = row.semantic_plan_patterns;
      record.semantic_fault_concepts = row.semantic_fault_concepts;
      record.semantic_community_summaries = row.semantic_community_summaries;
      const size_t count = std::min(std::min(row.estimator_names.size(), row.estimator_values.size()),
                                    std::min(row.estimator_available.size(), row.estimator_reasons.size()));
      record.estimator_outputs.reserve(count);
      for (size_t index = 0; index < count; ++index)
      {
        EstimateResult output;
        output.name = row.estimator_names[index];
        output.value = row.estimator_values[index];
        output.available = row.estimator_available[index];
        output.reason = row.estimator_reasons[index];
        record.estimator_outputs.push_back(output);
      }
      record.recorded_at_sec = row.recorded_at_sec;
      record.recorded_at_nanosec = row.recorded_at_nanosec;
      record.provenance = row.provenance;
      records.push_back(record);
    }
    return records;
  }

  /**
   * @brief Estimate semantic familiarity between a task and a skill using retrieval rank.
   *
   * @param task_description Natural-language task description.
   * @param skill_name Skill name to score.
   * @param limit Maximum number of retrieval results to inspect.
   * @return double Familiarity score in the range [0, 1].
   */
  double semanticFamiliarity(const std::string& task_description, const std::string& skill_name,
                             size_t limit = 8) override
  {
    return retrieveSemanticContext(task_description, skill_name, limit).familiarity;
  }

  /**
   * @brief Retrieve semantic context for planning and scoring.
   *
   * This bundles the existing semantic stores into task-relevant capabilities,
   * related entities, dependency hints, plan-pattern hints, and fault concepts.
   */
  SemanticContextBundle retrieveSemanticContext(const std::string& task_description, const std::string& skill_name = "",
                                                size_t limit = 8) override
  {
    SemanticContextBundle bundle;
    if (!vector_db_ || task_description.empty())
      return bundle;

    const std::string requested_skill_id = experience::database::core_graphrag::normalize_id_segment(skill_name);
    const size_t bounded_limit = std::max<size_t>(1, limit);
    const auto query_results =
        vector_db_->query(task_description, static_cast<int>(bounded_limit), experience::vectordb::MetricType::COSINE);
    std::unordered_set<std::string> seen_capabilities;
    std::unordered_set<std::string> seen_entities;
    std::unordered_set<std::string> seen_dependencies;
    std::unordered_set<std::string> seen_patterns;
    std::unordered_set<std::string> seen_faults;
    std::vector<SemanticContextItem> weak_dependencies;

    for (size_t index = 0; index < query_results.size(); ++index)
    {
      const double relevance =
          std::clamp(1.0 - (static_cast<double>(index) / static_cast<double>(bounded_limit)), 0.0, 1.0);
      const std::string& id = query_results[index];

      if (const auto node_info = inform_db_->readNodeInformation(id); node_info.has_value())
      {
        SemanticContextItem item = experience::graph::core_graphrag::semanticItemFromNode(node_info.value(),
                               relevance);
        if (const auto graph_node = inform_db_->readGraphNode(id); graph_node.has_value())
        {
          item.level = graph_node->level;
          item.cluster_id = graph_node->cluster_id;
          item.feature_vector = graph_node->feature_vector;
        }

        const std::string normalized_name =
          experience::database::core_graphrag::normalize_id_segment(node_info->name);
        if (node_info->type == experience::database::NodeType::CAPABILITY)
        {
          if (seen_capabilities.insert(item.id).second)
            bundle.relevant_capabilities.push_back(item);
          if (!requested_skill_id.empty() && normalized_name == requested_skill_id)
            bundle.familiarity = std::max(bundle.familiarity, relevance);
        }
        else if (seen_entities.insert(item.id).second)
        {
          bundle.related_entities.push_back(item);
        }
        continue;
      }

      if (const auto edge_info = inform_db_->readEdgeInformation(id); edge_info.has_value())
      {
        SemanticContextItem item = experience::graph::core_graphrag::semanticItemFromEdge(edge_info.value(),
                               relevance);

        if (item.edge_type == "parameter_flow")
        {
          if (item.structural_score < edge_parameter_min_score_)
            continue;

          if (experience::graph::core_graphrag::is_weak_parameter_edge(item))
          {
            item.relevance *= 0.75;
            if (seen_dependencies.insert(item.id).second)
              weak_dependencies.push_back(item);
            continue;
          }
        }

        const std::string category = experience::graph::core_graphrag::classifySemanticEdge(item);
        if (category == "fault" && seen_faults.insert(item.id).second)
          bundle.fault_concepts.push_back(item);
        else if (category == "pattern" && seen_patterns.insert(item.id).second)
          bundle.plan_patterns.push_back(item);
        else if (category == "related" && seen_entities.insert(item.id).second)
          bundle.related_entities.push_back(item);
        else if (seen_dependencies.insert(item.id).second)
          bundle.dependencies.push_back(item);
        continue;
      }

      if (const auto community_info = inform_db_->readCommunityInformation(id); community_info.has_value())
      {
        SemanticContextItem item;
        item.id = community_info->id;
        item.name = "community";
        item.description = community_info->description;
        item.type = "COMMUNITY";
        item.relevance = relevance;
        if (seen_patterns.insert(item.id).second)
          bundle.community_summaries.push_back(item);
      }
    }

    const size_t weak_limit =
        edge_parameter_max_weak_dependencies_ < 0 ?
            weak_dependencies.size() :
            std::min(weak_dependencies.size(), static_cast<size_t>(edge_parameter_max_weak_dependencies_));
    for (size_t index = 0; index < weak_limit; ++index)
      bundle.dependencies.push_back(weak_dependencies[index]);

    return bundle;
  }

  /**
   * @brief Read one node score explanation by full candidate-node identity.
   *
   * @param query Candidate node explanation query.
   * @return std::optional<PlanNodeScoreExplanationRecord> Matching explanation when available.
   */
  std::optional<PlanNodeScoreExplanationRecord>
  readPlanNodeScoreExplanation(const PlanNodeScoreExplanationNodeQuery& query) override
  {
    const auto row = inform_db_->readPlanNodeScoreExplanation(query.planning_request_id, query.candidate_plan_id,
                                                              query.graph_hash, query.candidate_node_id);
    if (!row.has_value())
      return std::nullopt;

    PlanNodeScoreExplanationRecord record;
    record.planning_request_id = row->planning_request_id;
    record.candidate_plan_id = row->candidate_plan_id;
    record.graph_hash = row->graph_hash;
    record.candidate_node_id = row->candidate_node_id;
    record.skill_name = row->skill_name;
    record.skill_id = row->skill_id;
    record.requested_context_key = row->requested_context_key;
    record.requested_context_class = row->requested_context_class;
    record.selected_skill_id = row->selected_skill_id;
    record.selected_key = row->selected_key;
    record.estimator_window = row->estimator_window;
    record.fallback_level = row->fallback_level;
    record.proxy_source = row->proxy_source;
    record.reason = row->reason;
    record.semantic_familiarity = row->semantic_familiarity;
    record.semantic_relevant_capabilities = row->semantic_relevant_capabilities;
    record.semantic_related_entities = row->semantic_related_entities;
    record.semantic_dependencies = row->semantic_dependencies;
    record.semantic_plan_patterns = row->semantic_plan_patterns;
    record.semantic_fault_concepts = row->semantic_fault_concepts;
    record.semantic_community_summaries = row->semantic_community_summaries;
    const size_t count = std::min(std::min(row->estimator_names.size(), row->estimator_values.size()),
                                  std::min(row->estimator_available.size(), row->estimator_reasons.size()));
    record.estimator_outputs.reserve(count);
    for (size_t index = 0; index < count; ++index)
    {
      EstimateResult output;
      output.name = row->estimator_names[index];
      output.value = row->estimator_values[index];
      output.available = row->estimator_available[index];
      output.reason = row->estimator_reasons[index];
      record.estimator_outputs.push_back(output);
    }
    record.recorded_at_sec = row->recorded_at_sec;
    record.recorded_at_nanosec = row->recorded_at_nanosec;
    record.provenance = row->provenance;
    return record;
  }

  /**
   * @brief Read selected plan feedback together with all node explanations.
   *
   * @param query Candidate plan explanation query.
   * @return std::optional<SelectedPlanScoreExplanationBundle> Bundle when feedback exists.
   */
  std::optional<SelectedPlanScoreExplanationBundle>
  readSelectedPlanScoreExplanationBundle(const PlanNodeScoreExplanationQuery& query) override
  {
    const auto feedback = inform_db_->readSelectedPlanFeedbackStatistics(query.planning_request_id,
                                                                         query.candidate_plan_id, query.graph_hash);
    if (!feedback.has_value())
      return std::nullopt;

    SelectedPlanScoreExplanationBundle bundle;
    bundle.feedback.planning_request_id = feedback->planning_request_id;
    bundle.feedback.candidate_plan_id = feedback->candidate_plan_id;
    bundle.feedback.graph_hash = feedback->graph_hash;
    bundle.feedback.selected = feedback->selected;
    bundle.feedback.scoring_supported = feedback->scoring_supported;
    bundle.feedback.feasible = feedback->feasible;
    bundle.feedback.sequential_graph = feedback->sequential_graph;
    bundle.feedback.feasibility_reason = feedback->feasibility_reason;
    bundle.feedback.terminal_success_estimate = feedback->terminal_success_estimate;
    bundle.feedback.expected_runtime_seconds = feedback->expected_runtime_seconds;
    bundle.feedback.expected_energy_delta_percentage = feedback->expected_energy_delta_percentage;
    bundle.feedback.expected_fault_burden = feedback->expected_fault_burden;
    bundle.feedback.expected_resource_demand = feedback->expected_resource_demand;
    bundle.feedback.expected_uncertainty = feedback->expected_uncertainty;
    bundle.feedback.final_rapfs = feedback->final_rapfs;
    bundle.feedback.terminal_outcome = feedback->terminal_outcome;
    bundle.feedback.terminal_success = feedback->terminal_success;
    bundle.feedback.runtime_seconds = feedback->runtime_seconds;
    bundle.feedback.energy_delta_percentage = feedback->energy_delta_percentage;
    bundle.feedback.warning_count = feedback->warning_count;
    bundle.feedback.error_count = feedback->error_count;
    bundle.feedback.human_judgement = feedback->human_judgement;
    bundle.feedback.score_components = feedback->score_components;
    bundle.feedback.normalizer_version = feedback->normalizer_version;
    bundle.feedback.estimator_version = feedback->estimator_version;
    bundle.feedback.weight_version = feedback->weight_version;
    bundle.feedback.recorded_at_sec = feedback->recorded_at_sec;
    bundle.feedback.recorded_at_nanosec = feedback->recorded_at_nanosec;
    bundle.feedback.provenance = feedback->provenance;
    bundle.node_explanations = readPlanNodeScoreExplanations(query);
    return bundle;
  }

  /**
   * @brief Persist selected-plan feedback statistics.
   *
   * @param feedback Selected-plan feedback record.
   * @return bool True when the record was stored successfully.
   */
  bool recordPlanFeedback(const PlanFeedbackRecord& feedback) override
  {
    experience::database::PlanFeedbackStatistics data;
    data.planning_request_id = feedback.planning_request_id;
    data.candidate_plan_id = feedback.candidate_plan_id;
    data.graph_hash = feedback.graph_hash;
    data.selected = feedback.selected;
    data.scoring_supported = feedback.scoring_supported;
    data.feasible = feedback.feasible;
    data.sequential_graph = feedback.sequential_graph;
    data.feasibility_reason = feedback.feasibility_reason;
    data.terminal_success_estimate = feedback.terminal_success_estimate;
    data.expected_runtime_seconds = feedback.expected_runtime_seconds;
    data.expected_energy_delta_percentage = feedback.expected_energy_delta_percentage;
    data.expected_fault_burden = feedback.expected_fault_burden;
    data.expected_resource_demand = feedback.expected_resource_demand;
    data.expected_uncertainty = feedback.expected_uncertainty;
    data.final_rapfs = feedback.final_rapfs;
    data.terminal_outcome = feedback.terminal_outcome;
    data.terminal_success = feedback.terminal_success;
    data.runtime_seconds = feedback.runtime_seconds;
    data.energy_delta_percentage = feedback.energy_delta_percentage;
    data.warning_count = feedback.warning_count;
    data.error_count = feedback.error_count;
    data.human_judgement = feedback.human_judgement;
    data.score_components = feedback.score_components;
    data.calibration_version = feedback.calibration_version;
    data.normalizer_version = feedback.normalizer_version;
    data.estimator_version = feedback.estimator_version;
    data.weight_version = feedback.weight_version;
    data.recorded_at_sec = feedback.recorded_at_sec;
    data.recorded_at_nanosec = feedback.recorded_at_nanosec;
    data.provenance = feedback.provenance;
    return inform_db_->writePlanFeedbackStatistics(data);
  }

  /**
   * @brief Persist a versioned scoring epoch record.
   */
  bool recordScoringEpoch(const ScoringEpochRecord& record) override
  {
    experience::database::ScoringEpochStatistics data;
    data.epoch_id = record.epoch_id;
    data.strategy_name = record.strategy_name;
    data.calibration_version = record.calibration_version;
    data.normalizer_version = record.normalizer_version;
    data.estimator_version = record.estimator_version;
    data.weight_version = record.weight_version;
    data.uncertainty_weight_locked = record.uncertainty_weight_locked;
    data.calibration_summary = record.calibration_summary;
    data.selection_bias_summary = record.selection_bias_summary;
    data.active = record.active;
    data.start_sec = record.start_sec;
    data.start_nanosec = record.start_nanosec;
    data.end_sec = record.end_sec;
    data.end_nanosec = record.end_nanosec;
    data.recorded_at_sec = record.recorded_at_sec;
    data.recorded_at_nanosec = record.recorded_at_nanosec;
    data.provenance = record.provenance;
    return inform_db_->writeScoringEpochStatistics(data);
  }

  /**
   * @brief Read a versioned scoring epoch record by epoch ID.
   */
  std::optional<ScoringEpochRecord> readScoringEpoch(const std::string& epoch_id) override
  {
    const auto row = inform_db_->readScoringEpochStatistics(epoch_id);
    if (!row.has_value())
      return std::nullopt;

    ScoringEpochRecord record;
    record.epoch_id = row->epoch_id;
    record.strategy_name = row->strategy_name;
    record.calibration_version = row->calibration_version;
    record.normalizer_version = row->normalizer_version;
    record.estimator_version = row->estimator_version;
    record.weight_version = row->weight_version;
    record.uncertainty_weight_locked = row->uncertainty_weight_locked;
    record.calibration_summary = row->calibration_summary;
    record.selection_bias_summary = row->selection_bias_summary;
    record.active = row->active;
    record.start_sec = row->start_sec;
    record.start_nanosec = row->start_nanosec;
    record.end_sec = row->end_sec;
    record.end_nanosec = row->end_nanosec;
    record.recorded_at_sec = row->recorded_at_sec;
    record.recorded_at_nanosec = row->recorded_at_nanosec;
    record.provenance = row->provenance;
    return record;
  }

  /**
   * @brief Summarize selected-plan feedback rows for one scorer-version tuple.
   */
  PlanFeedbackCalibrationSummary summarizePlanFeedbackCalibration(const std::string& calibration_version,
                                                                  const std::string& normalizer_version,
                                                                  const std::string& estimator_version,
                                                                  const std::string& weight_version) override
  {
    const auto row = inform_db_->summarizePlanFeedbackCalibrationStatistics(calibration_version, normalizer_version,
                                                                            estimator_version, weight_version);
    PlanFeedbackCalibrationSummary summary;
    summary.calibration_version = row.calibration_version;
    summary.normalizer_version = row.normalizer_version;
    summary.estimator_version = row.estimator_version;
    summary.weight_version = row.weight_version;
    summary.sample_count = row.sample_count;
    summary.success_count = row.success_count;
    summary.failure_count = row.failure_count;
    summary.mean_predicted_success = row.mean_predicted_success;
    summary.observed_success_rate = row.observed_success_rate;
    summary.brier_score = row.brier_score;
    summary.mean_runtime_absolute_error_seconds = row.mean_runtime_absolute_error_seconds;
    summary.mean_energy_absolute_error_percentage = row.mean_energy_absolute_error_percentage;
    summary.mean_expected_fault_burden = row.mean_expected_fault_burden;
    summary.mean_warning_count = row.mean_warning_count;
    summary.mean_error_count = row.mean_error_count;
    summary.mean_uncertainty = row.mean_uncertainty;
    summary.mean_uncertainty_on_success = row.mean_uncertainty_on_success;
    summary.mean_uncertainty_on_failure = row.mean_uncertainty_on_failure;
    return summary;
  }

  /**
   * @brief Summarize candidate exposure and selection for one scorer-version tuple.
   */
  PlanFeedbackSelectionBiasSummary summarizePlanFeedbackSelectionBias(const std::string& calibration_version,
                                                                      const std::string& normalizer_version,
                                                                      const std::string& estimator_version,
                                                                      const std::string& weight_version) override
  {
    const auto row = inform_db_->summarizePlanFeedbackSelectionBiasStatistics(calibration_version, normalizer_version,
                                                                              estimator_version, weight_version);
    PlanFeedbackSelectionBiasSummary summary;
    summary.calibration_version = row.calibration_version;
    summary.normalizer_version = row.normalizer_version;
    summary.estimator_version = row.estimator_version;
    summary.weight_version = row.weight_version;
    summary.planning_request_count = row.planning_request_count;
    summary.total_candidate_count = row.total_candidate_count;
    summary.selected_candidate_count = row.selected_candidate_count;
    summary.unselected_candidate_count = row.unselected_candidate_count;
    summary.feasible_candidate_count = row.feasible_candidate_count;
    summary.selected_feasible_count = row.selected_feasible_count;
    summary.mean_candidates_per_request = row.mean_candidates_per_request;
    summary.mean_selected_rapfs = row.mean_selected_rapfs;
    summary.mean_unselected_rapfs = row.mean_unselected_rapfs;
    summary.mean_selected_success_estimate = row.mean_selected_success_estimate;
    summary.mean_unselected_success_estimate = row.mean_unselected_success_estimate;
    summary.selected_execution_rate = row.selected_execution_rate;
    return summary;
  }

protected:
  /**
   * @brief generate a unique bond id
   *
   * @return const std::string
   */
  static const std::string generate_uuid()
  {
    uuid_t uuid;
    uuid_generate_random(uuid);
    char uuid_str[40];
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);
  }

  /**
   * @brief Shared pointer to the vector database.
   */
  std::shared_ptr<experience::VectorDB> vector_db_;

  /**
   * @brief Shared pointer to the graph RAG.
   */
  std::shared_ptr<experience::GraphRAG> graph_rag_;

  /**
   * @brief Shared pointer to the information database.
   */
  std::shared_ptr<experience::database::SQLiteDB> inform_db_;

  /**
   * @brief Shared pointer to the prompt service client adapter.
   */
  std::shared_ptr<experience::llm::PromptServiceClient> llmclient_;

  double edge_parameter_min_score_ = 0.35;
  double edge_parameter_strong_score_ = 0.65;
  int edge_parameter_max_weak_dependencies_ = 2;
};

class GraphKnowledgeCore : public CoreGraphRag
{
};
}  // namespace fabric