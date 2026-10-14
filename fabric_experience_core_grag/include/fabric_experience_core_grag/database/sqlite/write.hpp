#pragma once

namespace experience::database
{
inline bool SQLiteDB::writeNodeInformation(const NodeInformation& node)
{
  const char* sql = "REPLACE INTO node_information (id, name, provider, descriptions, type) VALUES (?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, node.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, node.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, node.provider.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, node.descriptions.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 5, static_cast<int>(node.type));
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeEdgeInformation(const EdgeInformation& edge)
{
  const char* sql = "REPLACE INTO edge_information (id, source, target, relationship, edge_type, edge_strength_class, "
                    "structural_score, matched_runtime_inputs, total_runtime_inputs, matched_parameter_keys) VALUES "
                    "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, edge.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, edge.source.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, edge.target.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, edge.relationship.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, edge.edge_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, edge.edge_strength_class.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 7, edge.structural_score);
  sqlite3_bind_int(stmt, 8, edge.matched_runtime_inputs);
  sqlite3_bind_int(stmt, 9, edge.total_runtime_inputs);
  const std::string matched_keys = detail::join(edge.matched_parameter_keys);
  sqlite3_bind_text(stmt, 10, matched_keys.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeCommunityInformation(const CommunityInformation& community)
{
  const char* sql = "REPLACE INTO community_information (id, description) VALUES (?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, community.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, community.description.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeEmbedding(const experience::vectordb::Embedding& embedding)
{
  const char* sql = "REPLACE INTO embedding (id, vector) VALUES (?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, embedding.id.c_str(), -1, SQLITE_TRANSIENT);
  const std::string vec = detail::serialize_vector(embedding.vector);
  sqlite3_bind_text(stmt, 2, vec.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeVectorDB(const std::vector<experience::vectordb::Embedding>& embeddings)
{
  for (const auto& embedding : embeddings)
    if (!writeEmbedding(embedding))
      return false;
  return true;
}

inline bool SQLiteDB::writeGraphNode(const experience::graph::NodeInfo& node)
{
  const char* sql = "REPLACE INTO graph_node (id, level, cluster_id, feature_vector) VALUES (?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, node.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, node.level);
  sqlite3_bind_int(stmt, 3, node.cluster_id);
  const std::string feature_vector = detail::joinDoubles(node.feature_vector);
  sqlite3_bind_text(stmt, 4, feature_vector.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeGraphEdge(const experience::graph::EdgeInfo& edge)
{
  const char* sql = "REPLACE INTO graph_edge (id, source, target) VALUES (?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, edge.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, edge.source.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, edge.target.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeGraphCommunity(const experience::graph::CommunityInfo& community)
{
  const char* sql = "REPLACE INTO graph_community (id, level, node_ids, edge_ids, occurrence) VALUES (?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, community.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, community.level);
  const std::string nodes = detail::join(community.node_ids);
  const std::string edges = detail::join(community.edge_ids);
  sqlite3_bind_text(stmt, 3, nodes.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, edges.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 5, community.occurrence);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeGraph(const experience::graph::Graph& graph)
{
  for (const auto& [id, node] : graph.nodes)
    if (!writeGraphNode(node))
      return false;
  for (const auto& [id, edge] : graph.edges)
    if (!writeGraphEdge(edge))
      return false;
  for (const auto& [id, community] : graph.communities)
    if (!writeGraphCommunity(community))
      return false;
  return true;
}

inline bool SQLiteDB::writeSkillRunStatistics(const SkillRunStatistics& data)
{
  const char* sql = "INSERT INTO skill_run_statistics (execution_window_id, plan_id, planning_request_id, "
                    "candidate_plan_id, graph_hash, bond_id, instance_id, skill_id, "
                    "skill_name, fabric_status, finalized, is_active, start_sec, start_nanosec, stop_sec, "
                    "stop_nanosec, runs_started, runs_completed, runs_succeeded, runs_failed, runs_aborted, "
                    "runs_timed_out, runtime_sample_count, runtime_sum_seconds, runtime_sum_squares_seconds, "
                    "energy_sample_count, energy_sum_delta_percentage, energy_sum_squares_delta_percentage, "
                    "warning_count, error_count, fault_exposure_count, resource_missing_dimensions, observed_at_sec, "
                    "observed_at_nanosec, last_outcome_sec, last_outcome_nanosec, last_outcome_label, "
                    "estimator_window, selected_context_key, missing_field_flags) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, "
                    "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?) ON "
                    "CONFLICT(execution_window_id) DO UPDATE SET plan_id = excluded.plan_id, "
                    "planning_request_id = excluded.planning_request_id, candidate_plan_id = excluded.candidate_plan_id, "
                    "graph_hash = excluded.graph_hash, bond_id = "
                    "excluded.bond_id, instance_id = excluded.instance_id, skill_id = excluded.skill_id, skill_name = "
                    "excluded.skill_name, fabric_status = excluded.fabric_status, finalized = CASE WHEN "
                    "skill_run_statistics.finalized = 1 THEN 1 ELSE excluded.finalized END, is_active = CASE WHEN "
                    "skill_run_statistics.finalized = 1 OR excluded.finalized = 1 THEN 0 ELSE excluded.is_active END, "
                    "start_sec = excluded.start_sec, start_nanosec = excluded.start_nanosec, stop_sec = "
                    "excluded.stop_sec, stop_nanosec = excluded.stop_nanosec, runs_started = excluded.runs_started, "
                    "runs_completed = excluded.runs_completed, runs_succeeded = excluded.runs_succeeded, runs_failed = "
                    "excluded.runs_failed, runs_aborted = excluded.runs_aborted, runs_timed_out = "
                    "excluded.runs_timed_out, runtime_sample_count = excluded.runtime_sample_count, "
                    "runtime_sum_seconds = excluded.runtime_sum_seconds, runtime_sum_squares_seconds = "
                    "excluded.runtime_sum_squares_seconds, energy_sample_count = excluded.energy_sample_count, "
                    "energy_sum_delta_percentage = excluded.energy_sum_delta_percentage, "
                    "energy_sum_squares_delta_percentage = excluded.energy_sum_squares_delta_percentage, warning_count "
                    "= excluded.warning_count, error_count = excluded.error_count, fault_exposure_count = "
                    "excluded.fault_exposure_count, resource_missing_dimensions = "
                    "excluded.resource_missing_dimensions, observed_at_sec = excluded.observed_at_sec, "
                    "observed_at_nanosec = excluded.observed_at_nanosec, last_outcome_sec = excluded.last_outcome_sec, "
                    "last_outcome_nanosec = excluded.last_outcome_nanosec, last_outcome_label = "
                    "excluded.last_outcome_label, estimator_window = excluded.estimator_window, selected_context_key = "
                    "excluded.selected_context_key, missing_field_flags = excluded.missing_field_flags;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, data.execution_window_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, data.plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, data.planning_request_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, data.candidate_plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, data.graph_hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, data.bond_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, data.instance_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, data.skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, data.skill_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 10, data.fabric_status);
  sqlite3_bind_int(stmt, 11, data.finalized ? 1 : 0);
  sqlite3_bind_int(stmt, 12, data.is_active ? 1 : 0);
  sqlite3_bind_int(stmt, 13, data.start_sec);
  sqlite3_bind_int(stmt, 14, static_cast<int>(data.start_nanosec));
  sqlite3_bind_int(stmt, 15, data.stop_sec);
  sqlite3_bind_int(stmt, 16, static_cast<int>(data.stop_nanosec));
  sqlite3_bind_int(stmt, 17, data.runs_started);
  sqlite3_bind_int(stmt, 18, data.runs_completed);
  sqlite3_bind_int(stmt, 19, data.runs_succeeded);
  sqlite3_bind_int(stmt, 20, data.runs_failed);
  sqlite3_bind_int(stmt, 21, data.runs_aborted);
  sqlite3_bind_int(stmt, 22, data.runs_timed_out);
  sqlite3_bind_int(stmt, 23, data.runtime_sample_count);
  sqlite3_bind_double(stmt, 24, data.runtime_sum_seconds);
  sqlite3_bind_double(stmt, 25, data.runtime_sum_squares_seconds);
  sqlite3_bind_int(stmt, 26, data.energy_sample_count);
  sqlite3_bind_double(stmt, 27, data.energy_sum_delta_percentage);
  sqlite3_bind_double(stmt, 28, data.energy_sum_squares_delta_percentage);
  sqlite3_bind_int(stmt, 29, data.warning_count);
  sqlite3_bind_int(stmt, 30, data.error_count);
  sqlite3_bind_int(stmt, 31, data.fault_exposure_count);
  sqlite3_bind_text(stmt, 32, data.resource_missing_dimensions.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 33, data.observed_at_sec);
  sqlite3_bind_int(stmt, 34, static_cast<int>(data.observed_at_nanosec));
  sqlite3_bind_int(stmt, 35, data.last_outcome_sec);
  sqlite3_bind_int(stmt, 36, static_cast<int>(data.last_outcome_nanosec));
  sqlite3_bind_text(stmt, 37, data.last_outcome_label.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 38, data.estimator_window.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 39, data.selected_context_key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 40, data.missing_field_flags.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeSkillContextStatistics(const SkillContextStatistics& data)
{
  const char* sql = "REPLACE INTO skill_context_statistics (skill_id, context_key, context_class, runs_started, "
                    "runs_completed, runs_succeeded, runs_failed, runs_aborted, runs_timed_out, runtime_sample_count, "
                    "runtime_sum_seconds, runtime_sum_squares_seconds, energy_sample_count, "
                    "energy_sum_delta_percentage, energy_sum_squares_delta_percentage, warning_count, error_count, "
                    "fault_exposure_count, resource_missing_dimensions, estimator_window, updated_at_sec, "
                    "updated_at_nanosec, provenance) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, "
                    "?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, data.skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, data.context_key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, data.context_class.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, data.runs_started);
  sqlite3_bind_int(stmt, 5, data.runs_completed);
  sqlite3_bind_int(stmt, 6, data.runs_succeeded);
  sqlite3_bind_int(stmt, 7, data.runs_failed);
  sqlite3_bind_int(stmt, 8, data.runs_aborted);
  sqlite3_bind_int(stmt, 9, data.runs_timed_out);
  sqlite3_bind_int(stmt, 10, data.runtime_sample_count);
  sqlite3_bind_double(stmt, 11, data.runtime_sum_seconds);
  sqlite3_bind_double(stmt, 12, data.runtime_sum_squares_seconds);
  sqlite3_bind_int(stmt, 13, data.energy_sample_count);
  sqlite3_bind_double(stmt, 14, data.energy_sum_delta_percentage);
  sqlite3_bind_double(stmt, 15, data.energy_sum_squares_delta_percentage);
  sqlite3_bind_int(stmt, 16, data.warning_count);
  sqlite3_bind_int(stmt, 17, data.error_count);
  sqlite3_bind_int(stmt, 18, data.fault_exposure_count);
  sqlite3_bind_text(stmt, 19, data.resource_missing_dimensions.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 20, data.estimator_window.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 21, data.updated_at_sec);
  sqlite3_bind_int(stmt, 22, static_cast<int>(data.updated_at_nanosec));
  sqlite3_bind_text(stmt, 23, data.provenance.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeSkillEdgeStatistics(const SkillEdgeStatistics& data)
{
  const char* sql = "REPLACE INTO skill_edge_statistics (source_skill_id, target_skill_id, edge_label, context_key, "
                    "transition_count, success_count, failure_count, recovery_count, runtime_sample_count, "
                    "runtime_sum_seconds, runtime_sum_squares_seconds, energy_sample_count, "
                    "energy_sum_delta_percentage, energy_sum_squares_delta_percentage, warning_count, error_count, "
                    "fault_exposure_count, estimator_window, updated_at_sec, updated_at_nanosec, provenance) VALUES "
                    "(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, data.source_skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, data.target_skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, data.edge_label.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, data.context_key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 5, data.transition_count);
  sqlite3_bind_int(stmt, 6, data.success_count);
  sqlite3_bind_int(stmt, 7, data.failure_count);
  sqlite3_bind_int(stmt, 8, data.recovery_count);
  sqlite3_bind_int(stmt, 9, data.runtime_sample_count);
  sqlite3_bind_double(stmt, 10, data.runtime_sum_seconds);
  sqlite3_bind_double(stmt, 11, data.runtime_sum_squares_seconds);
  sqlite3_bind_int(stmt, 12, data.energy_sample_count);
  sqlite3_bind_double(stmt, 13, data.energy_sum_delta_percentage);
  sqlite3_bind_double(stmt, 14, data.energy_sum_squares_delta_percentage);
  sqlite3_bind_int(stmt, 15, data.warning_count);
  sqlite3_bind_int(stmt, 16, data.error_count);
  sqlite3_bind_int(stmt, 17, data.fault_exposure_count);
  sqlite3_bind_text(stmt, 18, data.estimator_window.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 19, data.updated_at_sec);
  sqlite3_bind_int(stmt, 20, static_cast<int>(data.updated_at_nanosec));
  sqlite3_bind_text(stmt, 21, data.provenance.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writePlanFeedbackStatistics(const PlanFeedbackStatistics& data)
{
  const char* sql = "REPLACE INTO plan_feedback_statistics (planning_request_id, candidate_plan_id, graph_hash, "
                    "selected, scoring_supported, feasible, sequential_graph, feasibility_reason, "
                    "terminal_success_estimate, expected_runtime_seconds, expected_energy_delta_percentage, "
                    "expected_fault_burden, expected_resource_demand, expected_uncertainty, final_rapfs, "
                    "terminal_outcome, terminal_success, runtime_seconds, energy_delta_percentage, warning_count, "
                    "error_count, human_judgement, score_components, calibration_version, normalizer_version, "
                    "estimator_version, weight_version, recorded_at_sec, recorded_at_nanosec, provenance) VALUES (?, "
                    "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, data.planning_request_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, data.candidate_plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, data.graph_hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 4, data.selected ? 1 : 0);
  sqlite3_bind_int(stmt, 5, data.scoring_supported ? 1 : 0);
  sqlite3_bind_int(stmt, 6, data.feasible ? 1 : 0);
  sqlite3_bind_int(stmt, 7, data.sequential_graph ? 1 : 0);
  sqlite3_bind_text(stmt, 8, data.feasibility_reason.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 9, data.terminal_success_estimate);
  sqlite3_bind_double(stmt, 10, data.expected_runtime_seconds);
  sqlite3_bind_double(stmt, 11, data.expected_energy_delta_percentage);
  sqlite3_bind_double(stmt, 12, data.expected_fault_burden);
  sqlite3_bind_double(stmt, 13, data.expected_resource_demand);
  sqlite3_bind_double(stmt, 14, data.expected_uncertainty);
  sqlite3_bind_double(stmt, 15, data.final_rapfs);
  sqlite3_bind_text(stmt, 16, data.terminal_outcome.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 17, data.terminal_success ? 1 : 0);
  sqlite3_bind_double(stmt, 18, data.runtime_seconds);
  sqlite3_bind_double(stmt, 19, data.energy_delta_percentage);
  sqlite3_bind_int(stmt, 20, data.warning_count);
  sqlite3_bind_int(stmt, 21, data.error_count);
  sqlite3_bind_text(stmt, 22, data.human_judgement.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 23, data.score_components.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 24, data.calibration_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 25, data.normalizer_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 26, data.estimator_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 27, data.weight_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 28, data.recorded_at_sec);
  sqlite3_bind_int(stmt, 29, static_cast<int>(data.recorded_at_nanosec));
  sqlite3_bind_text(stmt, 30, data.provenance.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writeScoringEpochStatistics(const ScoringEpochStatistics& data)
{
  const char* sql = "REPLACE INTO scoring_epoch_statistics (epoch_id, strategy_name, calibration_version, "
                    "normalizer_version, estimator_version, weight_version, uncertainty_weight_locked, "
                    "calibration_summary, selection_bias_summary, active, start_sec, start_nanosec, end_sec, "
                    "end_nanosec, recorded_at_sec, recorded_at_nanosec, provenance) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, "
                    "?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, data.epoch_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, data.strategy_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, data.calibration_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, data.normalizer_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, data.estimator_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, data.weight_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 7, data.uncertainty_weight_locked ? 1 : 0);
  sqlite3_bind_text(stmt, 8, data.calibration_summary.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, data.selection_bias_summary.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 10, data.active ? 1 : 0);
  sqlite3_bind_int(stmt, 11, data.start_sec);
  sqlite3_bind_int(stmt, 12, static_cast<int>(data.start_nanosec));
  sqlite3_bind_int(stmt, 13, data.end_sec);
  sqlite3_bind_int(stmt, 14, static_cast<int>(data.end_nanosec));
  sqlite3_bind_int(stmt, 15, data.recorded_at_sec);
  sqlite3_bind_int(stmt, 16, static_cast<int>(data.recorded_at_nanosec));
  sqlite3_bind_text(stmt, 17, data.provenance.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writePlanSkillAttribution(const PlanSkillAttribution& data)
{
  const char* sql = "REPLACE INTO plan_skill_attribution (planning_request_id, candidate_plan_id, graph_hash, "
                    "candidate_node_id, runtime_plan_id, runtime_instance_id, skill_name, skill_id, context_key, "
                    "context_class, selected_skill_id, "
                    "selected_key, estimator_window, fallback_level, proxy_source, selected, terminal_success, "
                    "terminal_outcome, recorded_at_sec, recorded_at_nanosec, provenance) VALUES (?, ?, ?, ?, ?, ?, ?, "
                    "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, data.planning_request_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, data.candidate_plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, data.graph_hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, data.candidate_node_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, data.runtime_plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, data.runtime_instance_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, data.skill_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, data.skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, data.context_key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10, data.context_class.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 11, data.selected_skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 12, data.selected_key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 13, data.estimator_window.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 14, data.fallback_level.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 15, data.proxy_source.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 16, data.selected ? 1 : 0);
  sqlite3_bind_int(stmt, 17, data.terminal_success ? 1 : 0);
  sqlite3_bind_text(stmt, 18, data.terminal_outcome.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 19, data.recorded_at_sec);
  sqlite3_bind_int(stmt, 20, static_cast<int>(data.recorded_at_nanosec));
  sqlite3_bind_text(stmt, 21, data.provenance.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writePlanSkillEdgeAttribution(const PlanSkillEdgeAttribution& data)
{
  const char* sql = "REPLACE INTO plan_skill_edge_attribution (planning_request_id, candidate_plan_id, graph_hash, "
                    "source_node_id, target_node_id, source_skill_id, target_skill_id, edge_label, selected, "
                    "terminal_success, terminal_outcome, recorded_at_sec, recorded_at_nanosec, provenance) VALUES (?, "
                    "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, data.planning_request_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, data.candidate_plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, data.graph_hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, data.source_node_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, data.target_node_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, data.source_skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, data.target_skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, data.edge_label.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 9, data.selected ? 1 : 0);
  sqlite3_bind_int(stmt, 10, data.terminal_success ? 1 : 0);
  sqlite3_bind_text(stmt, 11, data.terminal_outcome.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 12, data.recorded_at_sec);
  sqlite3_bind_int(stmt, 13, static_cast<int>(data.recorded_at_nanosec));
  sqlite3_bind_text(stmt, 14, data.provenance.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}

inline bool SQLiteDB::writePlanNodeScoreExplanation(const PlanNodeScoreExplanation& data)
{
  const char* sql = "REPLACE INTO plan_node_score_explanation (planning_request_id, candidate_plan_id, graph_hash, "
                    "candidate_node_id, skill_name, skill_id, requested_context_key, requested_context_class, "
                    "selected_skill_id, selected_key, estimator_window, fallback_level, proxy_source, reason, "
                    "semantic_familiarity, semantic_relevant_capabilities, semantic_related_entities, "
                    "semantic_dependencies, semantic_plan_patterns, semantic_fault_concepts, "
                    "semantic_community_summaries, estimator_names, estimator_values, estimator_available, "
                    "estimator_reasons, recorded_at_sec, recorded_at_nanosec, provenance) VALUES (?, ?, ?, ?, ?, ?, ?, "
                    "?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return false;
  sqlite3_bind_text(stmt, 1, data.planning_request_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, data.candidate_plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, data.graph_hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, data.candidate_node_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, data.skill_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, data.skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, data.requested_context_key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, data.requested_context_class.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 9, data.selected_skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 10, data.selected_key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 11, data.estimator_window.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 12, data.fallback_level.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 13, data.proxy_source.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 14, data.reason.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 15, data.semantic_familiarity);
  const auto semantic_relevant_capabilities = detail::join(data.semantic_relevant_capabilities);
  const auto semantic_related_entities = detail::join(data.semantic_related_entities);
  const auto semantic_dependencies = detail::join(data.semantic_dependencies);
  const auto semantic_plan_patterns = detail::join(data.semantic_plan_patterns);
  const auto semantic_fault_concepts = detail::join(data.semantic_fault_concepts);
  const auto semantic_community_summaries = detail::join(data.semantic_community_summaries);
  const auto estimator_names = detail::join(data.estimator_names);
  const auto estimator_values = detail::joinDoubles(data.estimator_values);
  const auto estimator_available = detail::joinBools(data.estimator_available);
  const auto estimator_reasons = detail::join(data.estimator_reasons);
  sqlite3_bind_text(stmt, 16, semantic_relevant_capabilities.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 17, semantic_related_entities.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 18, semantic_dependencies.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 19, semantic_plan_patterns.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 20, semantic_fault_concepts.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 21, semantic_community_summaries.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 22, estimator_names.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 23, estimator_values.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 24, estimator_available.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 25, estimator_reasons.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 26, data.recorded_at_sec);
  sqlite3_bind_int(stmt, 27, static_cast<int>(data.recorded_at_nanosec));
  sqlite3_bind_text(stmt, 28, data.provenance.c_str(), -1, SQLITE_TRANSIENT);
  return finishWriteStatement(stmt);
}
}  // namespace experience::database
