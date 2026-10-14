#pragma once

namespace experience::database
{
inline bool SQLiteDB::createNodeInformationTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS node_information (id TEXT PRIMARY KEY, name TEXT, provider TEXT, "
                    "descriptions TEXT, type INTEGER);";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createEdgeInformationTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS edge_information (id TEXT PRIMARY KEY, source TEXT, target TEXT, "
                    "relationship TEXT, edge_type TEXT DEFAULT '', edge_strength_class TEXT DEFAULT '', "
                    "structural_score REAL DEFAULT 0.0, "
                    "matched_runtime_inputs INTEGER DEFAULT 0, total_runtime_inputs INTEGER DEFAULT 0, "
                    "matched_parameter_keys TEXT DEFAULT '');";
  const bool created = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
  if (!created)
    return false;

  return ensureEdgeInformationColumn("edge_type", "TEXT DEFAULT ''") &&
         ensureEdgeInformationColumn("edge_strength_class", "TEXT DEFAULT ''") &&
         ensureEdgeInformationColumn("structural_score", "REAL DEFAULT 0.0") &&
         ensureEdgeInformationColumn("matched_runtime_inputs", "INTEGER DEFAULT 0") &&
         ensureEdgeInformationColumn("total_runtime_inputs", "INTEGER DEFAULT 0") &&
         ensureEdgeInformationColumn("matched_parameter_keys", "TEXT DEFAULT ''");
}

inline bool SQLiteDB::createCommunityInformationTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS community_information (id TEXT PRIMARY KEY, description TEXT);";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createEmbeddingTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS embedding (id TEXT PRIMARY KEY, vector TEXT);";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createGraphNodeTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS graph_node (id TEXT PRIMARY KEY, level INTEGER, cluster_id INTEGER, "
                    "feature_vector TEXT DEFAULT '');";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createGraphEdgeTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS graph_edge (id TEXT PRIMARY KEY, source TEXT, target TEXT);";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createGraphCommunityTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS graph_community (id TEXT PRIMARY KEY, level INTEGER, node_ids TEXT, "
                    "edge_ids TEXT, occurrence REAL);";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createSkillRunStatisticsTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS skill_run_statistics (execution_window_id TEXT PRIMARY KEY, "
                    "plan_id TEXT, planning_request_id TEXT DEFAULT '', candidate_plan_id TEXT DEFAULT '', "
                    "graph_hash TEXT DEFAULT '', bond_id TEXT, instance_id TEXT, skill_id TEXT, skill_name TEXT, fabric_status "
                    "INTEGER, finalized INTEGER, is_active INTEGER, start_sec INTEGER, start_nanosec INTEGER, stop_sec "
                    "INTEGER, stop_nanosec INTEGER, runs_started INTEGER, runs_completed INTEGER, runs_succeeded "
                    "INTEGER, runs_failed INTEGER, "
                    "runs_aborted INTEGER, runs_timed_out INTEGER, runtime_sample_count INTEGER, runtime_sum_seconds "
                    "REAL, "
                    "runtime_sum_squares_seconds REAL, energy_sample_count INTEGER, energy_sum_delta_percentage REAL, "
                    "energy_sum_squares_delta_percentage REAL, warning_count INTEGER, error_count INTEGER, "
                    "fault_exposure_count INTEGER, "
                    "resource_missing_dimensions TEXT, observed_at_sec INTEGER, observed_at_nanosec INTEGER, "
                    "last_outcome_sec INTEGER, "
                    "last_outcome_nanosec INTEGER, last_outcome_label TEXT, estimator_window TEXT, "
                    "selected_context_key TEXT, missing_field_flags TEXT);";
  const bool created = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
  if (!created)
    return false;

  return ensureTextColumn("skill_run_statistics", "planning_request_id", "DEFAULT ''") &&
         ensureTextColumn("skill_run_statistics", "candidate_plan_id", "DEFAULT ''") &&
         ensureTextColumn("skill_run_statistics", "graph_hash", "DEFAULT ''");
}

inline bool SQLiteDB::createSkillContextStatisticsTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS skill_context_statistics (skill_id TEXT, context_key TEXT, "
                    "context_class TEXT, runs_started INTEGER, runs_completed INTEGER, runs_succeeded INTEGER, "
                    "runs_failed INTEGER, "
                    "runs_aborted INTEGER, runs_timed_out INTEGER, runtime_sample_count INTEGER, runtime_sum_seconds "
                    "REAL, "
                    "runtime_sum_squares_seconds REAL, energy_sample_count INTEGER, energy_sum_delta_percentage REAL, "
                    "energy_sum_squares_delta_percentage REAL, warning_count INTEGER, error_count INTEGER, "
                    "fault_exposure_count INTEGER, "
                    "resource_missing_dimensions TEXT, estimator_window TEXT, updated_at_sec INTEGER, "
                    "updated_at_nanosec INTEGER, "
                    "provenance TEXT, PRIMARY KEY (skill_id, context_key));";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createSkillEdgeStatisticsTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS skill_edge_statistics (source_skill_id TEXT, target_skill_id TEXT, "
                    "edge_label TEXT, context_key TEXT, transition_count INTEGER, success_count INTEGER, failure_count "
                    "INTEGER, "
                    "recovery_count INTEGER, runtime_sample_count INTEGER, runtime_sum_seconds REAL, "
                    "runtime_sum_squares_seconds REAL, "
                    "energy_sample_count INTEGER, energy_sum_delta_percentage REAL, "
                    "energy_sum_squares_delta_percentage REAL, "
                    "warning_count INTEGER, error_count INTEGER, fault_exposure_count INTEGER, estimator_window TEXT, "
                    "updated_at_sec INTEGER, updated_at_nanosec INTEGER, provenance TEXT, "
                    "PRIMARY KEY (source_skill_id, target_skill_id, edge_label, context_key));";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createPlanFeedbackStatisticsTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS plan_feedback_statistics (planning_request_id TEXT, candidate_plan_id "
                    "TEXT, "
                    "graph_hash TEXT, selected INTEGER, scoring_supported INTEGER DEFAULT 0, feasible INTEGER DEFAULT "
                    "0, "
                    "sequential_graph INTEGER DEFAULT 0, feasibility_reason TEXT DEFAULT '', terminal_success_estimate "
                    "REAL DEFAULT 0.0, "
                    "expected_runtime_seconds REAL DEFAULT 0.0, expected_energy_delta_percentage REAL DEFAULT 0.0, "
                    "expected_fault_burden REAL DEFAULT 0.0, "
                    "expected_resource_demand REAL DEFAULT 0.0, expected_uncertainty REAL DEFAULT 0.0, final_rapfs "
                    "REAL DEFAULT 0.0, terminal_outcome TEXT, "
                    "terminal_success INTEGER, runtime_seconds REAL, energy_delta_percentage REAL, warning_count "
                    "INTEGER, error_count INTEGER, human_judgement TEXT, "
                    "score_components TEXT, calibration_version TEXT DEFAULT '', normalizer_version TEXT, "
                    "estimator_version TEXT, weight_version TEXT, "
                    "recorded_at_sec INTEGER, recorded_at_nanosec INTEGER, provenance TEXT, PRIMARY KEY "
                    "(planning_request_id, candidate_plan_id, graph_hash));";
  if (sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
    return false;
  return ensureTextColumn("plan_feedback_statistics", "calibration_version", "DEFAULT ''");
}

inline bool SQLiteDB::createScoringEpochStatisticsTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS scoring_epoch_statistics (epoch_id TEXT PRIMARY KEY, strategy_name "
                    "TEXT, calibration_version TEXT, "
                    "normalizer_version TEXT, estimator_version TEXT, weight_version TEXT, uncertainty_weight_locked "
                    "INTEGER DEFAULT 1, calibration_summary TEXT, "
                    "selection_bias_summary TEXT, active INTEGER DEFAULT 0, start_sec INTEGER, start_nanosec INTEGER, "
                    "end_sec INTEGER, end_nanosec INTEGER, "
                    "recorded_at_sec INTEGER, recorded_at_nanosec INTEGER, provenance TEXT);";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createPlanSkillAttributionTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS plan_skill_attribution (planning_request_id TEXT, candidate_plan_id "
                    "TEXT, graph_hash TEXT, candidate_node_id TEXT, "
                    "runtime_plan_id TEXT DEFAULT '', runtime_instance_id TEXT DEFAULT '', "
                    "skill_name TEXT, skill_id TEXT, context_key TEXT, context_class TEXT, selected_skill_id TEXT, "
                    "selected_key TEXT, estimator_window TEXT, "
                    "fallback_level TEXT, proxy_source TEXT, selected INTEGER, terminal_success INTEGER, "
                    "terminal_outcome TEXT, recorded_at_sec INTEGER, "
                    "recorded_at_nanosec INTEGER, provenance TEXT, PRIMARY KEY (planning_request_id, "
                    "candidate_plan_id, graph_hash, candidate_node_id));";
  const bool created = sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
  if (!created)
    return false;

  return ensureTextColumn("plan_skill_attribution", "runtime_plan_id", "DEFAULT ''") &&
         ensureTextColumn("plan_skill_attribution", "runtime_instance_id", "DEFAULT ''");
}

inline bool SQLiteDB::createPlanSkillEdgeAttributionTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS plan_skill_edge_attribution (planning_request_id TEXT, "
                    "candidate_plan_id TEXT, graph_hash TEXT, source_node_id TEXT, "
                    "target_node_id TEXT, source_skill_id TEXT, target_skill_id TEXT, edge_label TEXT, selected "
                    "INTEGER, terminal_success INTEGER, terminal_outcome TEXT, "
                    "recorded_at_sec INTEGER, recorded_at_nanosec INTEGER, provenance TEXT, PRIMARY KEY "
                    "(planning_request_id, candidate_plan_id, graph_hash, source_node_id, target_node_id, "
                    "edge_label));";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::createPlanNodeScoreExplanationTable()
{
  const char* sql = "CREATE TABLE IF NOT EXISTS plan_node_score_explanation (planning_request_id TEXT, "
                    "candidate_plan_id TEXT, graph_hash TEXT, candidate_node_id TEXT, "
                    "skill_name TEXT, skill_id TEXT, requested_context_key TEXT, requested_context_class TEXT, "
                    "selected_skill_id TEXT, selected_key TEXT, estimator_window TEXT, "
                    "fallback_level TEXT, proxy_source TEXT, reason TEXT, semantic_familiarity REAL DEFAULT 0.0, "
                    "semantic_relevant_capabilities TEXT DEFAULT '', "
                    "semantic_related_entities TEXT DEFAULT '', semantic_dependencies TEXT DEFAULT '', "
                    "semantic_plan_patterns TEXT DEFAULT '', semantic_fault_concepts TEXT DEFAULT '', "
                    "semantic_community_summaries TEXT DEFAULT '', estimator_names TEXT DEFAULT '', estimator_values "
                    "TEXT DEFAULT '', estimator_available TEXT DEFAULT '', "
                    "estimator_reasons TEXT DEFAULT '', recorded_at_sec INTEGER, recorded_at_nanosec INTEGER, "
                    "provenance TEXT, "
                    "PRIMARY KEY (planning_request_id, candidate_plan_id, graph_hash, candidate_node_id));";
  return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline bool SQLiteDB::ensureTextColumn(const std::string& table_name, const std::string& column_name,
                                       const std::string& column_suffix)
{
  const std::string pragma = "PRAGMA table_info(" + table_name + ");";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, pragma.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    return false;

  bool found = false;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const unsigned char* text = sqlite3_column_text(stmt, 1);
    if (text != nullptr && column_name == reinterpret_cast<const char*>(text))
    {
      found = true;
      break;
    }
  }
  sqlite3_finalize(stmt);

  if (found)
    return true;

  std::string alter = "ALTER TABLE " + table_name + " ADD COLUMN " + column_name + " TEXT";
  if (!column_suffix.empty())
    alter += " " + column_suffix;
  alter += ";";
  return sqlite3_exec(db_, alter.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
}
}  // namespace experience::database
