#pragma once

namespace experience::database
{
inline std::vector<PlanNodeScoreExplanation> SQLiteDB::readPlanNodeScoreExplanations(
    const std::string& planning_request_id, const std::string& candidate_plan_id, const std::string& graph_hash)
{
  const char* sql = "SELECT candidate_node_id, skill_name, skill_id, requested_context_key, requested_context_class, "
                    "selected_skill_id, selected_key, estimator_window, fallback_level, proxy_source, reason, "
                    "semantic_familiarity, semantic_relevant_capabilities, semantic_related_entities, "
                    "semantic_dependencies, "
                    "semantic_plan_patterns, semantic_fault_concepts, semantic_community_summaries, estimator_names, "
                    "estimator_values, estimator_available, "
                    "estimator_reasons, recorded_at_sec, recorded_at_nanosec, provenance FROM "
                    "plan_node_score_explanation "
                    "WHERE planning_request_id=? AND candidate_plan_id=? AND graph_hash=? ORDER BY candidate_node_id "
                    "ASC;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return {};

  sqlite3_bind_text(stmt, 1, planning_request_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, candidate_plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, graph_hash.c_str(), -1, SQLITE_TRANSIENT);

  std::vector<PlanNodeScoreExplanation> records;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    PlanNodeScoreExplanation record;
    record.planning_request_id = planning_request_id;
    record.candidate_plan_id = candidate_plan_id;
    record.graph_hash = graph_hash;
    record.candidate_node_id = columnText(stmt, 0);
    record.skill_name = columnText(stmt, 1);
    record.skill_id = columnText(stmt, 2);
    record.requested_context_key = columnText(stmt, 3);
    record.requested_context_class = columnText(stmt, 4);
    record.selected_skill_id = columnText(stmt, 5);
    record.selected_key = columnText(stmt, 6);
    record.estimator_window = columnText(stmt, 7);
    record.fallback_level = columnText(stmt, 8);
    record.proxy_source = columnText(stmt, 9);
    record.reason = columnText(stmt, 10);
    record.semantic_familiarity = sqlite3_column_double(stmt, 11);
    record.semantic_relevant_capabilities = detail::split(columnText(stmt, 12));
    record.semantic_related_entities = detail::split(columnText(stmt, 13));
    record.semantic_dependencies = detail::split(columnText(stmt, 14));
    record.semantic_plan_patterns = detail::split(columnText(stmt, 15));
    record.semantic_fault_concepts = detail::split(columnText(stmt, 16));
    record.semantic_community_summaries = detail::split(columnText(stmt, 17));
    record.estimator_names = detail::split(columnText(stmt, 18));
    record.estimator_values = detail::splitDoubles(columnText(stmt, 19));
    record.estimator_available = detail::splitBools(columnText(stmt, 20));
    record.estimator_reasons = detail::split(columnText(stmt, 21));
    record.recorded_at_sec = sqlite3_column_int(stmt, 22);
    record.recorded_at_nanosec = static_cast<uint32_t>(sqlite3_column_int(stmt, 23));
    record.provenance = columnText(stmt, 24);
    records.push_back(record);
  }

  sqlite3_finalize(stmt);
  return records;
}

inline std::optional<PlanNodeScoreExplanation>
SQLiteDB::readPlanNodeScoreExplanation(const std::string& planning_request_id, const std::string& candidate_plan_id,
                                       const std::string& graph_hash, const std::string& candidate_node_id)
{
  const char* sql = "SELECT skill_name, skill_id, requested_context_key, requested_context_class, selected_skill_id, "
                    "selected_key, estimator_window, fallback_level, proxy_source, reason, semantic_familiarity, "
                    "semantic_relevant_capabilities, semantic_related_entities, semantic_dependencies, "
                    "semantic_plan_patterns, semantic_fault_concepts, semantic_community_summaries, estimator_names, "
                    "estimator_values, estimator_available, estimator_reasons, recorded_at_sec, recorded_at_nanosec, "
                    "provenance FROM plan_node_score_explanation WHERE planning_request_id=? AND candidate_plan_id=? "
                    "AND graph_hash=? AND candidate_node_id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;

  sqlite3_bind_text(stmt, 1, planning_request_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, candidate_plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, graph_hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, candidate_node_id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_ROW)
  {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }

  PlanNodeScoreExplanation record;
  record.planning_request_id = planning_request_id;
  record.candidate_plan_id = candidate_plan_id;
  record.graph_hash = graph_hash;
  record.candidate_node_id = candidate_node_id;
  record.skill_name = columnText(stmt, 0);
  record.skill_id = columnText(stmt, 1);
  record.requested_context_key = columnText(stmt, 2);
  record.requested_context_class = columnText(stmt, 3);
  record.selected_skill_id = columnText(stmt, 4);
  record.selected_key = columnText(stmt, 5);
  record.estimator_window = columnText(stmt, 6);
  record.fallback_level = columnText(stmt, 7);
  record.proxy_source = columnText(stmt, 8);
  record.reason = columnText(stmt, 9);
  record.semantic_familiarity = sqlite3_column_double(stmt, 10);
  record.semantic_relevant_capabilities = detail::split(columnText(stmt, 11));
  record.semantic_related_entities = detail::split(columnText(stmt, 12));
  record.semantic_dependencies = detail::split(columnText(stmt, 13));
  record.semantic_plan_patterns = detail::split(columnText(stmt, 14));
  record.semantic_fault_concepts = detail::split(columnText(stmt, 15));
  record.semantic_community_summaries = detail::split(columnText(stmt, 16));
  record.estimator_names = detail::split(columnText(stmt, 17));
  record.estimator_values = detail::splitDoubles(columnText(stmt, 18));
  record.estimator_available = detail::splitBools(columnText(stmt, 19));
  record.estimator_reasons = detail::split(columnText(stmt, 20));
  record.recorded_at_sec = sqlite3_column_int(stmt, 21);
  record.recorded_at_nanosec = static_cast<uint32_t>(sqlite3_column_int(stmt, 22));
  record.provenance = columnText(stmt, 23);
  sqlite3_finalize(stmt);
  return record;
}

inline std::optional<PlanFeedbackStatistics> SQLiteDB::readSelectedPlanFeedbackStatistics(
    const std::string& planning_request_id, const std::string& candidate_plan_id, const std::string& graph_hash)
{
  const char* sql = "SELECT selected, scoring_supported, feasible, sequential_graph, feasibility_reason, "
                    "terminal_success_estimate, expected_runtime_seconds, expected_energy_delta_percentage, "
                    "expected_fault_burden, expected_resource_demand, expected_uncertainty, final_rapfs, "
                    "terminal_outcome, terminal_success, runtime_seconds, energy_delta_percentage, warning_count, "
                    "error_count, human_judgement, score_components, calibration_version, normalizer_version, "
                    "estimator_version, weight_version, recorded_at_sec, recorded_at_nanosec, provenance FROM "
                    "plan_feedback_statistics WHERE planning_request_id=? AND candidate_plan_id=? AND graph_hash=? AND "
                    "selected=1;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;

  sqlite3_bind_text(stmt, 1, planning_request_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, candidate_plan_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, graph_hash.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_ROW)
  {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }

  PlanFeedbackStatistics feedback;
  feedback.planning_request_id = planning_request_id;
  feedback.candidate_plan_id = candidate_plan_id;
  feedback.graph_hash = graph_hash;
  feedback.selected = sqlite3_column_int(stmt, 0) != 0;
  feedback.scoring_supported = sqlite3_column_int(stmt, 1) != 0;
  feedback.feasible = sqlite3_column_int(stmt, 2) != 0;
  feedback.sequential_graph = sqlite3_column_int(stmt, 3) != 0;
  feedback.feasibility_reason = columnText(stmt, 4);
  feedback.terminal_success_estimate = sqlite3_column_double(stmt, 5);
  feedback.expected_runtime_seconds = sqlite3_column_double(stmt, 6);
  feedback.expected_energy_delta_percentage = sqlite3_column_double(stmt, 7);
  feedback.expected_fault_burden = sqlite3_column_double(stmt, 8);
  feedback.expected_resource_demand = sqlite3_column_double(stmt, 9);
  feedback.expected_uncertainty = sqlite3_column_double(stmt, 10);
  feedback.final_rapfs = sqlite3_column_double(stmt, 11);
  feedback.terminal_outcome = columnText(stmt, 12);
  feedback.terminal_success = sqlite3_column_int(stmt, 13) != 0;
  feedback.runtime_seconds = sqlite3_column_double(stmt, 14);
  feedback.energy_delta_percentage = sqlite3_column_double(stmt, 15);
  feedback.warning_count = sqlite3_column_int(stmt, 16);
  feedback.error_count = sqlite3_column_int(stmt, 17);
  feedback.human_judgement = columnText(stmt, 18);
  feedback.score_components = columnText(stmt, 19);
  feedback.calibration_version = columnText(stmt, 20);
  feedback.normalizer_version = columnText(stmt, 21);
  feedback.estimator_version = columnText(stmt, 22);
  feedback.weight_version = columnText(stmt, 23);
  feedback.recorded_at_sec = sqlite3_column_int(stmt, 24);
  feedback.recorded_at_nanosec = static_cast<uint32_t>(sqlite3_column_int(stmt, 25));
  feedback.provenance = columnText(stmt, 26);
  sqlite3_finalize(stmt);
  return feedback;
}

inline std::optional<ScoringEpochStatistics> SQLiteDB::readScoringEpochStatistics(const std::string& epoch_id)
{
  const char* sql = "SELECT strategy_name, calibration_version, normalizer_version, estimator_version, weight_version, "
                    "uncertainty_weight_locked, calibration_summary, selection_bias_summary, active, start_sec, "
                    "start_nanosec, end_sec, end_nanosec, recorded_at_sec, recorded_at_nanosec, provenance FROM "
                    "scoring_epoch_statistics WHERE epoch_id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;

  sqlite3_bind_text(stmt, 1, epoch_id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) != SQLITE_ROW)
  {
    sqlite3_finalize(stmt);
    return std::nullopt;
  }

  ScoringEpochStatistics record;
  record.epoch_id = epoch_id;
  record.strategy_name = columnText(stmt, 0);
  record.calibration_version = columnText(stmt, 1);
  record.normalizer_version = columnText(stmt, 2);
  record.estimator_version = columnText(stmt, 3);
  record.weight_version = columnText(stmt, 4);
  record.uncertainty_weight_locked = sqlite3_column_int(stmt, 5) != 0;
  record.calibration_summary = columnText(stmt, 6);
  record.selection_bias_summary = columnText(stmt, 7);
  record.active = sqlite3_column_int(stmt, 8) != 0;
  record.start_sec = sqlite3_column_int(stmt, 9);
  record.start_nanosec = static_cast<uint32_t>(sqlite3_column_int(stmt, 10));
  record.end_sec = sqlite3_column_int(stmt, 11);
  record.end_nanosec = static_cast<uint32_t>(sqlite3_column_int(stmt, 12));
  record.recorded_at_sec = sqlite3_column_int(stmt, 13);
  record.recorded_at_nanosec = static_cast<uint32_t>(sqlite3_column_int(stmt, 14));
  record.provenance = columnText(stmt, 15);
  sqlite3_finalize(stmt);
  return record;
}

inline PlanFeedbackCalibrationStatistics SQLiteDB::summarizePlanFeedbackCalibrationStatistics(
    const std::string& calibration_version, const std::string& normalizer_version, const std::string& estimator_version,
    const std::string& weight_version)
{
  const char* sql = "SELECT COUNT(*), COALESCE(SUM(CASE WHEN terminal_success=1 THEN 1 ELSE 0 END), 0), "
                    "COALESCE(AVG(terminal_success_estimate), 0.0), "
                    "COALESCE(AVG((terminal_success_estimate - CASE WHEN terminal_success=1 THEN 1.0 ELSE 0.0 END) * "
                    "(terminal_success_estimate - CASE WHEN terminal_success=1 THEN 1.0 ELSE 0.0 END)), 0.0), "
                    "COALESCE(AVG(CASE WHEN expected_runtime_seconds > 0.0 OR runtime_seconds > 0.0 THEN "
                    "ABS(expected_runtime_seconds - runtime_seconds) END), 0.0), "
                    "COALESCE(AVG(CASE WHEN expected_energy_delta_percentage > 0.0 OR energy_delta_percentage > 0.0 "
                    "THEN ABS(expected_energy_delta_percentage - energy_delta_percentage) END), 0.0), "
                    "COALESCE(AVG(expected_fault_burden), 0.0), COALESCE(AVG(warning_count), 0.0), "
                    "COALESCE(AVG(error_count), 0.0), COALESCE(AVG(expected_uncertainty), 0.0), "
                    "COALESCE(AVG(CASE WHEN terminal_success=1 THEN expected_uncertainty END), 0.0), COALESCE(AVG(CASE "
                    "WHEN terminal_success=0 THEN expected_uncertainty END), 0.0) "
                    "FROM plan_feedback_statistics WHERE selected=1 AND terminal_outcome <> '' AND "
                    "calibration_version=? AND normalizer_version=? AND estimator_version=? AND weight_version=?;";

  PlanFeedbackCalibrationStatistics summary;
  summary.calibration_version = calibration_version;
  summary.normalizer_version = normalizer_version;
  summary.estimator_version = estimator_version;
  summary.weight_version = weight_version;

  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return summary;

  sqlite3_bind_text(stmt, 1, calibration_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, normalizer_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, estimator_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, weight_version.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    summary.sample_count = sqlite3_column_int(stmt, 0);
    summary.success_count = sqlite3_column_int(stmt, 1);
    summary.failure_count = std::max(0, summary.sample_count - summary.success_count);
    summary.mean_predicted_success = sqlite3_column_double(stmt, 2);
    summary.brier_score = sqlite3_column_double(stmt, 3);
    summary.mean_runtime_absolute_error_seconds = sqlite3_column_double(stmt, 4);
    summary.mean_energy_absolute_error_percentage = sqlite3_column_double(stmt, 5);
    summary.mean_expected_fault_burden = sqlite3_column_double(stmt, 6);
    summary.mean_warning_count = sqlite3_column_double(stmt, 7);
    summary.mean_error_count = sqlite3_column_double(stmt, 8);
    summary.mean_uncertainty = sqlite3_column_double(stmt, 9);
    summary.mean_uncertainty_on_success = sqlite3_column_double(stmt, 10);
    summary.mean_uncertainty_on_failure = sqlite3_column_double(stmt, 11);
    summary.observed_success_rate = summary.sample_count > 0 ? static_cast<double>(summary.success_count) /
                                                                   static_cast<double>(summary.sample_count) :
                                                               0.0;
  }

  sqlite3_finalize(stmt);
  return summary;
}

inline PlanFeedbackSelectionBiasStatistics SQLiteDB::summarizePlanFeedbackSelectionBiasStatistics(
    const std::string& calibration_version, const std::string& normalizer_version, const std::string& estimator_version,
    const std::string& weight_version)
{
  const char* sql = "SELECT COUNT(DISTINCT planning_request_id), COUNT(*), COALESCE(SUM(CASE WHEN selected=1 THEN 1 "
                    "ELSE 0 END), 0), COALESCE(SUM(CASE WHEN selected=0 THEN 1 ELSE 0 END), 0), COALESCE(SUM(CASE WHEN "
                    "feasible=1 THEN 1 ELSE 0 END), 0), COALESCE(SUM(CASE WHEN selected=1 AND feasible=1 THEN 1 ELSE 0 "
                    "END), 0), COALESCE(AVG(CASE WHEN selected=1 THEN final_rapfs END), 0.0), COALESCE(AVG(CASE WHEN "
                    "selected=0 THEN final_rapfs END), 0.0), COALESCE(AVG(CASE WHEN selected=1 THEN "
                    "terminal_success_estimate END), 0.0), COALESCE(AVG(CASE WHEN selected=0 THEN "
                    "terminal_success_estimate END), 0.0), COALESCE(AVG(candidate_count_per_request.candidate_count), "
                    "0.0) FROM plan_feedback_statistics JOIN (SELECT planning_request_id, COUNT(*) AS candidate_count "
                    "FROM plan_feedback_statistics WHERE calibration_version=? AND normalizer_version=? AND "
                    "estimator_version=? AND weight_version=? GROUP BY planning_request_id) AS "
                    "candidate_count_per_request ON plan_feedback_statistics.planning_request_id = "
                    "candidate_count_per_request.planning_request_id WHERE calibration_version=? AND "
                    "normalizer_version=? AND estimator_version=? AND weight_version=?;";

  PlanFeedbackSelectionBiasStatistics summary;
  summary.calibration_version = calibration_version;
  summary.normalizer_version = normalizer_version;
  summary.estimator_version = estimator_version;
  summary.weight_version = weight_version;

  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return summary;

  sqlite3_bind_text(stmt, 1, calibration_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, normalizer_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, estimator_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, weight_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, calibration_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, normalizer_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 7, estimator_version.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 8, weight_version.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    summary.planning_request_count = sqlite3_column_int(stmt, 0);
    summary.total_candidate_count = sqlite3_column_int(stmt, 1);
    summary.selected_candidate_count = sqlite3_column_int(stmt, 2);
    summary.unselected_candidate_count = sqlite3_column_int(stmt, 3);
    summary.feasible_candidate_count = sqlite3_column_int(stmt, 4);
    summary.selected_feasible_count = sqlite3_column_int(stmt, 5);
    summary.mean_selected_rapfs = sqlite3_column_double(stmt, 6);
    summary.mean_unselected_rapfs = sqlite3_column_double(stmt, 7);
    summary.mean_selected_success_estimate = sqlite3_column_double(stmt, 8);
    summary.mean_unselected_success_estimate = sqlite3_column_double(stmt, 9);
    summary.mean_candidates_per_request = sqlite3_column_double(stmt, 10);
    summary.selected_execution_rate =
        summary.total_candidate_count > 0 ?
            static_cast<double>(summary.selected_candidate_count) / static_cast<double>(summary.total_candidate_count) :
            0.0;
  }

  sqlite3_finalize(stmt);
  return summary;
}

inline StatisticsFallbackSelection SQLiteDB::selectSkillStatistics(const std::string& skill_id,
                                                                   const std::string& context_key,
                                                                   const std::string& context_class)
{
  StatisticsFallbackSelection selection;
  selection.skill_id = skill_id;
  selection.selected_skill_id = skill_id;
  selection.requested_context_key = context_key;
  selection.requested_context_class = context_class;
  selection.proxy_source = "requested_skill";

  if (!context_key.empty())
  {
    auto statistics = readExactContextStatistics(skill_id, context_key);
    if (statistics.has_value())
    {
      selection.level = StatisticsFallbackLevel::EXACT_CONTEXT;
      selection.selected_key = context_key;
      selection.estimator_window = statistics->estimator_window;
      selection.reason = "matched exact context key";
      selection.statistics = statistics.value();
      return selection;
    }
  }

  if (!context_class.empty())
  {
    auto statistics = readContextClassStatistics(skill_id, context_class);
    if (statistics.has_value())
    {
      selection.level = StatisticsFallbackLevel::CONTEXT_CLASS;
      selection.selected_key = "context_class:" + context_class;
      selection.estimator_window = statistics->estimator_window;
      selection.reason = context_key.empty() ? "no exact context key requested; using broader context class evidence" :
                                               "exact context key unavailable; using broader context class evidence";
      selection.statistics = statistics.value();
      return selection;
    }
  }

  auto aggregate = readSkillAggregateStatistics(skill_id);
  if (aggregate.has_value())
  {
    selection.level = StatisticsFallbackLevel::SKILL_AGGREGATE;
    selection.selected_key = "skill:" + skill_id;
    selection.estimator_window = aggregate->estimator_window;
    selection.reason = "no exact-context or context-class evidence available; using capability aggregate";
    selection.statistics = aggregate.value();
    return selection;
  }

  selection.reason = "no exact-context, context-class, or capability-aggregate evidence available";
  return selection;
}

inline StatisticsFallbackSelection SQLiteDB::selectSkillStatisticsWithSemanticFallback(const std::string& skill_id,
                                                                                       const std::string& skill_name,
                                                                                       const std::string& context_key,
                                                                                       const std::string& context_class,
                                                                                       size_t semantic_limit)
{
  auto selection = selectSkillStatistics(skill_id, context_key, context_class);
  if (selection.level != StatisticsFallbackLevel::PRIOR_DEFAULT)
    return selection;

  const auto related_skill_ids = readRelatedSkillIds(skill_name, semantic_limit);
  for (const auto& related_skill_id : related_skill_ids)
  {
    auto aggregate = readSkillAggregateStatistics(related_skill_id);
    if (!aggregate.has_value())
      continue;

    selection.level = StatisticsFallbackLevel::SEMANTIC_RELATION;
    selection.selected_skill_id = related_skill_id;
    selection.selected_key = "skill:" + related_skill_id;
    selection.estimator_window = aggregate->estimator_window;
    selection.proxy_source = "semantic_relation";
    selection.reason = "no direct evidence available; using semantic neighbor skill aggregate";
    selection.statistics = aggregate.value();
    return selection;
  }

  auto winning_plan_prior = readWinningPlanPriorStatistics(skill_id, context_key, context_class);
  if (winning_plan_prior.has_value())
  {
    selection.level = StatisticsFallbackLevel::WINNING_PLAN_PRIOR;
    selection.selected_skill_id = skill_id;
    selection.selected_key =
        context_key.empty() ?
            (context_class.empty() ? "winning_plan_prior:" + skill_id : "winning_plan_prior_class:" + context_class) :
            "winning_plan_prior:" + context_key;
    selection.estimator_window = winning_plan_prior->estimator_window;
    selection.proxy_source = "winning_plan_prior";
    selection.reason = "no direct evidence available; using aggregate from previously selected candidate plans";
    selection.statistics = winning_plan_prior.value();
    return selection;
  }

  selection.reason = "no direct, semantic-neighbor, or winning-plan empirical evidence available";
  return selection;
}

inline std::optional<NodeInformation> SQLiteDB::readNodeInformation(const std::string& id)
{
  const char* sql = "SELECT name, provider, descriptions, type FROM node_information WHERE id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;

  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);

  NodeInformation node;
  node.id = id;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    node.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    node.provider = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    node.descriptions = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    node.type = static_cast<NodeType>(sqlite3_column_int(stmt, 3));
    sqlite3_finalize(stmt);
    return node;
  }

  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<EdgeInformation> SQLiteDB::readEdgeInformation(const std::string& id)
{
  const char* sql = "SELECT source, target, relationship, edge_type, edge_strength_class, structural_score, "
                    "matched_runtime_inputs, total_runtime_inputs, matched_parameter_keys FROM edge_information WHERE "
                    "id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  EdgeInformation edge;
  edge.id = id;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    edge.source = columnText(stmt, 0);
    edge.target = columnText(stmt, 1);
    edge.relationship = columnText(stmt, 2);
    edge.edge_type = columnText(stmt, 3);
    edge.edge_strength_class = columnText(stmt, 4);
    edge.structural_score = sqlite3_column_double(stmt, 5);
    edge.matched_runtime_inputs = sqlite3_column_int(stmt, 6);
    edge.total_runtime_inputs = sqlite3_column_int(stmt, 7);
    edge.matched_parameter_keys = detail::split(columnText(stmt, 8));
    sqlite3_finalize(stmt);
    return edge;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<CommunityInformation> SQLiteDB::readCommunityInformation(const std::string& id)
{
  const char* sql = "SELECT description FROM community_information WHERE id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  CommunityInformation community;
  community.id = id;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    community.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);
    return community;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<experience::vectordb::Embedding> SQLiteDB::readEmbedding(const std::string& id)
{
  const char* sql = "SELECT vector FROM embedding WHERE id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  experience::vectordb::Embedding embedding;
  embedding.id = id;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    embedding.vector = detail::deserialize_vector(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    return embedding;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<std::vector<experience::vectordb::Embedding>> SQLiteDB::readVectorDB()
{
  const char* sql = "SELECT id, vector FROM embedding;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;

  std::vector<experience::vectordb::Embedding> embeddings;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    experience::vectordb::Embedding embedding;
    embedding.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    embedding.vector = detail::deserialize_vector(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    embeddings.push_back(embedding);
  }
  sqlite3_finalize(stmt);
  return embeddings;
}

inline std::optional<experience::graph::NodeInfo> SQLiteDB::readGraphNode(const std::string& id)
{
  const char* sql = "SELECT level, cluster_id, feature_vector FROM graph_node WHERE id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  experience::graph::NodeInfo node;
  node.id = id;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    node.level = sqlite3_column_int(stmt, 0);
    node.cluster_id = sqlite3_column_int(stmt, 1);
    node.feature_vector = detail::splitDoubles(columnText(stmt, 2));
    sqlite3_finalize(stmt);
    return node;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<experience::graph::EdgeInfo> SQLiteDB::readGraphEdge(const std::string& id)
{
  const char* sql = "SELECT source, target FROM graph_edge WHERE id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  experience::graph::EdgeInfo edge;
  edge.id = id;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    edge.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    edge.target = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    sqlite3_finalize(stmt);
    return edge;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<experience::graph::CommunityInfo> SQLiteDB::readGraphCommunity(const std::string& id)
{
  const char* sql = "SELECT level, node_ids, edge_ids, occurrence FROM graph_community WHERE id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  experience::graph::CommunityInfo community;
  community.id = id;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    community.level = sqlite3_column_int(stmt, 0);
    community.node_ids = detail::split(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    community.edge_ids = detail::split(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
    community.occurrence = sqlite3_column_double(stmt, 3);
    sqlite3_finalize(stmt);
    return community;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<experience::graph::Graph> SQLiteDB::readGraph()
{
  experience::graph::Graph graph;

  const char* sql_nodes = "SELECT id, level, cluster_id, feature_vector FROM graph_node;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql_nodes, -1, &stmt, nullptr) == SQLITE_OK)
  {
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      experience::graph::NodeInfo node;
      node.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      node.level = sqlite3_column_int(stmt, 1);
      node.cluster_id = sqlite3_column_int(stmt, 2);
      node.feature_vector = detail::splitDoubles(columnText(stmt, 3));
      graph.nodes[node.id] = node;
    }
    sqlite3_finalize(stmt);
  }

  const char* sql_edges = "SELECT id, source, target FROM graph_edge;";
  if (sqlite3_prepare_v2(db_, sql_edges, -1, &stmt, nullptr) == SQLITE_OK)
  {
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      experience::graph::EdgeInfo edge;
      edge.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      edge.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
      edge.target = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
      graph.edges[edge.generate_key()] = edge;
    }
    sqlite3_finalize(stmt);
  }

  const char* sql_communities = "SELECT id, level, node_ids, edge_ids, occurrence FROM graph_community;";
  if (sqlite3_prepare_v2(db_, sql_communities, -1, &stmt, nullptr) == SQLITE_OK)
  {
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
      experience::graph::CommunityInfo community;
      community.id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
      community.level = sqlite3_column_int(stmt, 1);
      community.node_ids = detail::split(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)));
      community.edge_ids = detail::split(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)));
      community.occurrence = sqlite3_column_double(stmt, 4);
      graph.communities[community.id] = community;
    }
    sqlite3_finalize(stmt);
  }

  for (const auto& [edge_id, edge] : graph.edges)
  {
    graph.adjacency[edge.source].insert(edge.target);
    graph.adjacency[edge.target].insert(edge.source);
  }

  return graph.nodes.empty() ? std::nullopt : std::optional<experience::graph::Graph>(graph);
}

inline std::optional<SkillStatisticsSummary> SQLiteDB::readExactContextStatistics(const std::string& skill_id,
                                                                                  const std::string& context_key)
{
  const char* sql = "SELECT runs_started, runs_completed, runs_succeeded, runs_failed, runs_aborted, runs_timed_out, "
                    "runtime_sample_count, runtime_sum_seconds, runtime_sum_squares_seconds, energy_sample_count, "
                    "energy_sum_delta_percentage, energy_sum_squares_delta_percentage, warning_count, error_count, "
                    "fault_exposure_count, resource_missing_dimensions, estimator_window FROM skill_context_statistics "
                    "WHERE skill_id=? AND context_key=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, context_key.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    auto summary = summaryFromColumns(stmt, 0);
    sqlite3_finalize(stmt);
    return summary;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<SkillStatisticsSummary> SQLiteDB::readContextClassStatistics(const std::string& skill_id,
                                                                                  const std::string& context_class)
{
  const char* sql = "SELECT COUNT(*), COALESCE(SUM(runs_started), 0), COALESCE(SUM(runs_completed), 0), "
                    "COALESCE(SUM(runs_succeeded), 0), COALESCE(SUM(runs_failed), 0), COALESCE(SUM(runs_aborted), 0), "
                    "COALESCE(SUM(runs_timed_out), 0), COALESCE(SUM(runtime_sample_count), 0), "
                    "COALESCE(SUM(runtime_sum_seconds), 0.0), COALESCE(SUM(runtime_sum_squares_seconds), 0.0), "
                    "COALESCE(SUM(energy_sample_count), 0), COALESCE(SUM(energy_sum_delta_percentage), 0.0), "
                    "COALESCE(SUM(energy_sum_squares_delta_percentage), 0.0), COALESCE(SUM(warning_count), 0), "
                    "COALESCE(SUM(error_count), 0), COALESCE(SUM(fault_exposure_count), 0), "
                    "COALESCE(GROUP_CONCAT(DISTINCT resource_missing_dimensions), ''), COALESCE(MAX(estimator_window), "
                    "'context_class') FROM skill_context_statistics WHERE skill_id=? AND context_class=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, skill_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, context_class.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) > 0)
  {
    auto summary = summaryFromColumns(stmt, 1);
    sqlite3_finalize(stmt);
    return summary;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<SkillStatisticsSummary> SQLiteDB::readSkillAggregateStatistics(const std::string& skill_id)
{
  const char* sql = "SELECT COUNT(*), COALESCE(SUM(runs_started), 0), COALESCE(SUM(runs_completed), 0), "
                    "COALESCE(SUM(runs_succeeded), 0), COALESCE(SUM(runs_failed), 0), COALESCE(SUM(runs_aborted), 0), "
                    "COALESCE(SUM(runs_timed_out), 0), COALESCE(SUM(runtime_sample_count), 0), "
                    "COALESCE(SUM(runtime_sum_seconds), 0.0), COALESCE(SUM(runtime_sum_squares_seconds), 0.0), "
                    "COALESCE(SUM(energy_sample_count), 0), COALESCE(SUM(energy_sum_delta_percentage), 0.0), "
                    "COALESCE(SUM(energy_sum_squares_delta_percentage), 0.0), COALESCE(SUM(warning_count), 0), "
                    "COALESCE(SUM(error_count), 0), COALESCE(SUM(fault_exposure_count), 0), "
                    "COALESCE(GROUP_CONCAT(DISTINCT resource_missing_dimensions), ''), COALESCE(MAX(estimator_window), "
                    "'all') FROM skill_run_statistics WHERE skill_id=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, skill_id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) > 0)
  {
    auto summary = summaryFromColumns(stmt, 1);
    sqlite3_finalize(stmt);
    return summary;
  }
  sqlite3_finalize(stmt);
  return std::nullopt;
}

inline std::optional<SkillStatisticsSummary> SQLiteDB::readWinningPlanPriorStatistics(const std::string& skill_id,
                                                                                      const std::string& context_key,
                                                                                      const std::string& context_class)
{
  std::string sql = "SELECT COUNT(*), COUNT(*), COUNT(*), COALESCE(SUM(CASE WHEN (CASE WHEN pca.terminal_outcome <> "
                    "'' THEN pca.terminal_success ELSE pfs.terminal_success END) = 1 THEN 1 ELSE 0 END), 0), "
                    "COALESCE(SUM(CASE WHEN (CASE WHEN pca.terminal_outcome <> '' THEN pca.terminal_success ELSE "
                    "pfs.terminal_success END) = 0 THEN 1 ELSE 0 END), 0), 0, 0, "
                    "COUNT(CASE WHEN pfs.runtime_seconds > 0.0 THEN 1 END), COALESCE(SUM(pfs.runtime_seconds), 0.0), "
                    "COALESCE(SUM(pfs.runtime_seconds * pfs.runtime_seconds), 0.0), COUNT(CASE WHEN "
                    "pfs.energy_delta_percentage != 0.0 THEN 1 END), COALESCE(SUM(pfs.energy_delta_percentage), 0.0), "
                    "COALESCE(SUM(pfs.energy_delta_percentage * pfs.energy_delta_percentage), 0.0), "
                    "COALESCE(SUM(pfs.warning_count), 0), COALESCE(SUM(pfs.error_count), 0), COUNT(*), '', "
                    "'winning_plan_prior' FROM plan_skill_attribution pca JOIN plan_feedback_statistics pfs ON "
                    "pca.planning_request_id = pfs.planning_request_id AND pca.candidate_plan_id = "
                    "pfs.candidate_plan_id AND pca.graph_hash = pfs.graph_hash WHERE pca.skill_id=? AND pfs.selected=1 "
                    "AND (pca.terminal_outcome <> '' OR pfs.terminal_outcome <> '')";

  if (!context_key.empty())
    sql += " AND pca.context_key=?";
  else if (!context_class.empty())
    sql += " AND pca.context_class=?";

  sql += ";";

  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    return std::nullopt;
  sqlite3_bind_text(stmt, 1, skill_id.c_str(), -1, SQLITE_TRANSIENT);
  if (!context_key.empty())
    sqlite3_bind_text(stmt, 2, context_key.c_str(), -1, SQLITE_TRANSIENT);
  else if (!context_class.empty())
    sqlite3_bind_text(stmt, 2, context_class.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) > 0)
  {
    auto summary = summaryFromColumns(stmt, 1);
    sqlite3_finalize(stmt);
    return summary;
  }

  sqlite3_finalize(stmt);
  return std::nullopt;
}
}  // namespace experience::database
