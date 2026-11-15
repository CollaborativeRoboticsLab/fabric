#pragma once

namespace fabric::experience::database
{
inline bool SQLiteDB::execStatement(std::string_view sql)
{
  if (!db_)
    return false;

  char* error_message = nullptr;
  const int rc = sqlite3_exec(db_, sql.data(), nullptr, nullptr, &error_message);
  if (rc == SQLITE_OK)
    return true;

  if (node_)
  {
    RCLCPP_WARN(node_->get_logger(), "SQLiteDB statement failed for %s: %s (%s)", db_path_.c_str(), sql.data(),
                error_message != nullptr ? error_message : sqlite3_errmsg(db_));
  }
  if (error_message != nullptr)
    sqlite3_free(error_message);
  return false;
}

inline bool SQLiteDB::finishWriteStatement(sqlite3_stmt* stmt)
{
  const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
  sqlite3_finalize(stmt);
  if (!ok)
    return false;

  return flush();
}

inline std::string SQLiteDB::columnText(sqlite3_stmt* stmt, int column)
{
  const unsigned char* text = sqlite3_column_text(stmt, column);
  return text == nullptr ? "" : reinterpret_cast<const char*>(text);
}

inline bool SQLiteDB::ensureEdgeInformationColumn(const std::string& column_name, const std::string& column_sql)
{
  std::string pragma = "PRAGMA table_info(edge_information);";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, pragma.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
    return false;

  bool found = false;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    if (columnText(stmt, 1) == column_name)
    {
      found = true;
      break;
    }
  }
  sqlite3_finalize(stmt);

  if (found)
    return true;

  const std::string alter_sql = "ALTER TABLE edge_information ADD COLUMN " + column_name + " " + column_sql + ";";
  return sqlite3_exec(db_, alter_sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
}

inline std::string SQLiteDB::normalizeSkillId(const std::string& value)
{
  std::string normalized;
  normalized.reserve(value.size());
  for (unsigned char ch : value)
  {
    if (std::isalnum(ch))
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    else if (ch == '/' || ch == '_' || ch == '-' || std::isspace(ch))
      normalized.push_back('_');
  }

  std::string collapsed;
  collapsed.reserve(normalized.size());
  bool previous_separator = false;
  for (char ch : normalized)
  {
    if (ch == '_')
    {
      if (!previous_separator)
        collapsed.push_back(ch);
      previous_separator = true;
    }
    else
    {
      collapsed.push_back(ch);
      previous_separator = false;
    }
  }

  while (!collapsed.empty() && collapsed.front() == '_')
    collapsed.erase(collapsed.begin());
  while (!collapsed.empty() && collapsed.back() == '_')
    collapsed.pop_back();
  return collapsed;
}

inline std::vector<std::string> SQLiteDB::readRelatedSkillIds(const std::string& skill_name, size_t limit)
{
  std::vector<std::string> related_ids;
  std::unordered_set<std::string> seen;
  const char* sql = "SELECT source, target FROM edge_information WHERE source=? OR target=?;";
  sqlite3_stmt* stmt;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
    return related_ids;

  sqlite3_bind_text(stmt, 1, skill_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, skill_name.c_str(), -1, SQLITE_TRANSIENT);
  const std::string requested_skill_id = normalizeSkillId(skill_name);

  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    const std::string source = columnText(stmt, 0);
    const std::string target = columnText(stmt, 1);
    const std::string related_name = source == skill_name ? target : source;
    const std::string related_id = normalizeSkillId(related_name);
    if (related_id.empty() || related_id == requested_skill_id)
      continue;
    if (!seen.insert(related_id).second)
      continue;
    related_ids.push_back(related_id);
    if (limit > 0 && related_ids.size() >= limit)
      break;
  }

  sqlite3_finalize(stmt);
  return related_ids;
}

inline SkillStatisticsSummary SQLiteDB::summaryFromColumns(sqlite3_stmt* stmt, int start_column)
{
  SkillStatisticsSummary summary;
  summary.runs_started = sqlite3_column_int(stmt, start_column);
  summary.runs_completed = sqlite3_column_int(stmt, start_column + 1);
  summary.runs_succeeded = sqlite3_column_int(stmt, start_column + 2);
  summary.runs_failed = sqlite3_column_int(stmt, start_column + 3);
  summary.runs_aborted = sqlite3_column_int(stmt, start_column + 4);
  summary.runs_timed_out = sqlite3_column_int(stmt, start_column + 5);
  summary.runtime_sample_count = sqlite3_column_int(stmt, start_column + 6);
  summary.runtime_sum_seconds = sqlite3_column_double(stmt, start_column + 7);
  summary.runtime_sum_squares_seconds = sqlite3_column_double(stmt, start_column + 8);
  summary.energy_sample_count = sqlite3_column_int(stmt, start_column + 9);
  summary.energy_sum_delta_percentage = sqlite3_column_double(stmt, start_column + 10);
  summary.energy_sum_squares_delta_percentage = sqlite3_column_double(stmt, start_column + 11);
  summary.warning_count = sqlite3_column_int(stmt, start_column + 12);
  summary.error_count = sqlite3_column_int(stmt, start_column + 13);
  summary.fault_exposure_count = sqlite3_column_int(stmt, start_column + 14);
  summary.resource_missing_dimensions = columnText(stmt, start_column + 15);
  summary.estimator_window = columnText(stmt, start_column + 16);
  return summary;
}
}  // namespace fabric::experience::database
