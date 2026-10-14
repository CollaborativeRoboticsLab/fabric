#pragma once

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string_view>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <sqlite3.h>
#include <Eigen/Dense>
#include <rclcpp/rclcpp.hpp>

#include "fabric_experience_core_grag/database/sqlite/detail.hpp"
#include "fabric_experience_core_grag/database/structs.hpp"
#include "fabric_experience_core_grag/graph/structs.hpp"
#include "fabric_experience_core_grag/vectordb/structs.hpp"

namespace experience::database
{

class SQLiteDB
{
public:
  explicit SQLiteDB(const rclcpp::Node::SharedPtr& node, const std::string& db_path)
  {
    node_ = node;
    db_path_ = db_path;
    db_ = nullptr;
    open();
  }

  ~SQLiteDB()
  {
    close();
  }

  bool open()
  {
    if (db_)
      return true;

    if (sqlite3_open(db_path_.c_str(), &db_) != SQLITE_OK)
    {
      return false;
    }

    sqlite3_busy_timeout(db_, 5000);
    execStatement("PRAGMA synchronous = FULL;");
    execStatement("PRAGMA foreign_keys = ON;");
    execStatement("PRAGMA optimize;");
    return true;
  }

  void close()
  {
    if (db_)
    {
      flush();

      const int rc = sqlite3_close_v2(db_);
      if (rc != SQLITE_OK)
      {
        if (node_)
        {
          RCLCPP_WARN(node_->get_logger(), "Failed to close SQLiteDB cleanly for %s: %s", db_path_.c_str(),
                      sqlite3_errmsg(db_));
        }
        return;
      }

      db_ = nullptr;
    }
  }

  bool flush()
  {
    if (!db_)
    {
      return true;
    }

    const int flush_rc = sqlite3_db_cacheflush(db_);
    if (flush_rc != SQLITE_OK && flush_rc != SQLITE_BUSY)
    {
      if (node_)
      {
        RCLCPP_WARN(node_->get_logger(), "Failed to flush SQLiteDB cache for %s: %s", db_path_.c_str(),
                    sqlite3_errmsg(db_));
      }
      return false;
    }

    return execStatement("PRAGMA optimize;");
  }

  bool isOpen() const
  {
    return db_ != nullptr;
  }

  bool createNodeInformationTable();
  bool createEdgeInformationTable();
  bool createCommunityInformationTable();
  bool createEmbeddingTable();
  bool createGraphNodeTable();
  bool createGraphEdgeTable();
  bool createGraphCommunityTable();
  bool createSkillRunStatisticsTable();
  bool createSkillContextStatisticsTable();
  bool createSkillEdgeStatisticsTable();
  bool createPlanFeedbackStatisticsTable();
  bool createScoringEpochStatisticsTable();
  bool createPlanSkillAttributionTable();
  bool createPlanSkillEdgeAttributionTable();
  bool createPlanNodeScoreExplanationTable();

  bool writeNodeInformation(const NodeInformation& node);
  bool writeEdgeInformation(const EdgeInformation& edge);
  bool writeCommunityInformation(const CommunityInformation& community);
  bool writeEmbedding(const experience::vectordb::Embedding& embedding);
  bool writeVectorDB(const std::vector<experience::vectordb::Embedding>& embeddings);
  bool writeGraphNode(const experience::graph::NodeInfo& node);
  bool writeGraphEdge(const experience::graph::EdgeInfo& edge);
  bool writeGraphCommunity(const experience::graph::CommunityInfo& community);
  bool writeGraph(const experience::graph::Graph& graph);
  bool writeSkillRunStatistics(const SkillRunStatistics& data);
  bool writeSkillContextStatistics(const SkillContextStatistics& data);
  bool writeSkillEdgeStatistics(const SkillEdgeStatistics& data);
  bool writePlanFeedbackStatistics(const PlanFeedbackStatistics& data);
  bool writeScoringEpochStatistics(const ScoringEpochStatistics& data);
  bool writePlanSkillAttribution(const PlanSkillAttribution& data);
  bool writePlanSkillEdgeAttribution(const PlanSkillEdgeAttribution& data);
  bool writePlanNodeScoreExplanation(const PlanNodeScoreExplanation& data);

  std::vector<PlanNodeScoreExplanation> readPlanNodeScoreExplanations(const std::string& planning_request_id,
                                                                      const std::string& candidate_plan_id,
                                                                      const std::string& graph_hash);
  std::optional<PlanNodeScoreExplanation> readPlanNodeScoreExplanation(const std::string& planning_request_id,
                                                                       const std::string& candidate_plan_id,
                                                                       const std::string& graph_hash,
                                                                       const std::string& candidate_node_id);
  std::optional<PlanFeedbackStatistics> readSelectedPlanFeedbackStatistics(const std::string& planning_request_id,
                                                                           const std::string& candidate_plan_id,
                                                                           const std::string& graph_hash);
  std::optional<ScoringEpochStatistics> readScoringEpochStatistics(const std::string& epoch_id);
  PlanFeedbackCalibrationStatistics summarizePlanFeedbackCalibrationStatistics(const std::string& calibration_version,
                                                                               const std::string& normalizer_version,
                                                                               const std::string& estimator_version,
                                                                               const std::string& weight_version);
  PlanFeedbackSelectionBiasStatistics summarizePlanFeedbackSelectionBiasStatistics(
      const std::string& calibration_version, const std::string& normalizer_version,
      const std::string& estimator_version, const std::string& weight_version);
  StatisticsFallbackSelection selectSkillStatistics(const std::string& skill_id, const std::string& context_key,
                                                    const std::string& context_class);
  StatisticsFallbackSelection selectSkillStatisticsWithSemanticFallback(const std::string& skill_id,
                                                                        const std::string& skill_name,
                                                                        const std::string& context_key,
                                                                        const std::string& context_class,
                                                                        size_t semantic_limit = 8);
  std::optional<NodeInformation> readNodeInformation(const std::string& id);
  std::optional<EdgeInformation> readEdgeInformation(const std::string& id);
  std::optional<CommunityInformation> readCommunityInformation(const std::string& id);
  std::optional<experience::vectordb::Embedding> readEmbedding(const std::string& id);
  std::optional<std::vector<experience::vectordb::Embedding>> readVectorDB();
  std::optional<experience::graph::NodeInfo> readGraphNode(const std::string& id);
  std::optional<experience::graph::EdgeInfo> readGraphEdge(const std::string& id);
  std::optional<experience::graph::CommunityInfo> readGraphCommunity(const std::string& id);
  std::optional<experience::graph::Graph> readGraph();

private:
  std::string db_path_;
  sqlite3* db_ = nullptr;
  rclcpp::Node::SharedPtr node_;

  bool execStatement(std::string_view sql);
  bool finishWriteStatement(sqlite3_stmt* stmt);

  bool ensureTextColumn(const std::string& table_name, const std::string& column_name,
                        const std::string& column_suffix = "");
  static std::string columnText(sqlite3_stmt* stmt, int column);
  bool ensureEdgeInformationColumn(const std::string& column_name, const std::string& column_sql);
  static std::string normalizeSkillId(const std::string& value);
  std::vector<std::string> readRelatedSkillIds(const std::string& skill_name, size_t limit);
  static SkillStatisticsSummary summaryFromColumns(sqlite3_stmt* stmt, int start_column);
  std::optional<SkillStatisticsSummary> readExactContextStatistics(const std::string& skill_id,
                                                                   const std::string& context_key);
  std::optional<SkillStatisticsSummary> readContextClassStatistics(const std::string& skill_id,
                                                                   const std::string& context_class);
  std::optional<SkillStatisticsSummary> readSkillAggregateStatistics(const std::string& skill_id);
  std::optional<SkillStatisticsSummary> readWinningPlanPriorStatistics(const std::string& skill_id,
                                                                       const std::string& context_key,
                                                                       const std::string& context_class);
};

}  // namespace experience::database

#include "fabric_experience_core_grag/database/sqlite/private.hpp"
#include "fabric_experience_core_grag/database/sqlite/schema.hpp"
#include "fabric_experience_core_grag/database/sqlite/write.hpp"
#include "fabric_experience_core_grag/database/sqlite/read.hpp"
