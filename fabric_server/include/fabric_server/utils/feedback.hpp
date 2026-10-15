 static std::string sanitizeFeedbackPathComponent(const std::string& value)
  {
    std::string sanitized;
    sanitized.reserve(value.size());

    for (const unsigned char ch : value)
    {
      if (std::isalnum(ch) || ch == '_' || ch == '-')
        sanitized.push_back(static_cast<char>(ch));
      else
        sanitized.push_back('_');
    }

    return sanitized.empty() ? std::string("execution") : sanitized;
  }

  std::filesystem::path resolveUserFeedbackRootDir() const
  {
    if (!user_feedback_root_dir_.empty())
      return user_feedback_root_dir_;

    return std::filesystem::current_path() / "user_feedback";
  }

  std::filesystem::path resolveUserFeedbackFilePath(const PlanningRequest& request) const
  {
    const std::string execution_key =
      sanitizeFeedbackPathComponent(request.uuid.empty() ? request.planning_request_id : request.uuid);
    return resolveUserFeedbackRootDir() / execution_key / "user_feedback.json";
  }

  std::filesystem::path resolveGeneratedPlansFilePath(const PlanningRequest& request) const
  {
    const std::string execution_key =
      sanitizeFeedbackPathComponent(request.uuid.empty() ? request.planning_request_id : request.uuid);
    return resolveUserFeedbackRootDir() / execution_key / "generated_plans.json";
  }

  bool loadUserFeedbackDocument(const std::filesystem::path& file_path, nlohmann::json& document)
  {
    document = nlohmann::json::object();

    if (!std::filesystem::exists(file_path))
      return true;

    std::ifstream input(file_path);
    if (!input.is_open())
    {
      RCLCPP_WARN(this->get_logger(), "Failed to open user feedback file for reading: %s", file_path.c_str());
      return false;
    }

    try
    {
      input >> document;
    }
    catch (const std::exception& ex)
    {
      RCLCPP_WARN(this->get_logger(), "Failed to parse user feedback file %s: %s", file_path.c_str(), ex.what());
      return false;
    }

    if (!document.is_object())
    {
      RCLCPP_WARN(this->get_logger(), "User feedback file %s must contain a JSON object", file_path.c_str());
      return false;
    }

    return true;
  }

  bool writeUserFeedbackDocument(const std::filesystem::path& file_path, const nlohmann::json& document)
  {
    std::error_code ec;
    std::filesystem::create_directories(file_path.parent_path(), ec);
    if (ec)
    {
      RCLCPP_WARN(this->get_logger(), "Failed to create user feedback directory %s: %s",
                  file_path.parent_path().c_str(), ec.message().c_str());
      return false;
    }

    const auto temp_path = file_path.string() + ".tmp";
    std::ofstream output(temp_path, std::ios::trunc);
    if (!output.is_open())
    {
      RCLCPP_WARN(this->get_logger(), "Failed to open user feedback file for writing: %s", temp_path.c_str());
      return false;
    }

    output << std::setw(2) << document << '\n';
    output.close();

    if (!output.good())
    {
      RCLCPP_WARN(this->get_logger(), "Failed to flush user feedback file: %s", temp_path.c_str());
      return false;
    }

    std::filesystem::remove(file_path, ec);
    ec.clear();
    std::filesystem::rename(temp_path, file_path, ec);
    if (ec)
    {
      std::filesystem::remove(temp_path);
      RCLCPP_WARN(this->get_logger(), "Failed to replace user feedback file %s: %s", file_path.c_str(),
                  ec.message().c_str());
      return false;
    }

    return true;
  }

  static int nextUserFeedbackStageIndex(const nlohmann::json& document)
  {
    int highest_index = 0;
    for (auto it = document.begin(); it != document.end(); ++it)
    {
      try
      {
        highest_index = std::max(highest_index, std::stoi(it.key()));
      }
      catch (const std::exception&)
      {
      }
    }
    return highest_index + 1;
  }

  static nlohmann::json finiteJsonNumberOrNull(const double value)
  {
    return std::isfinite(value) ? nlohmann::json(value) : nlohmann::json(nullptr);
  }

  static nlohmann::json parseScoreComponentsJson(const std::string& score_components)
  {
    if (score_components.empty())
      return nlohmann::json::object();

    try
    {
      return nlohmann::json::parse(score_components);
    }
    catch (const std::exception&)
    {
      return nlohmann::json{ { "raw", score_components }, { "parse_error", true } };
    }
  }

  static nlohmann::json planScoreToJson(const CandidatePlanScore& score)
  {
    return nlohmann::json{
      { "scoring_supported", score.supported },
      { "feasible", score.feasible },
      { "sequential_graph", score.sequential_graph },
      { "feasibility_reason", score.reason },
      { "terminal_success_estimate", finiteJsonNumberOrNull(score.terminal_success_estimate) },
      { "expected_runtime_seconds", finiteJsonNumberOrNull(score.expected_runtime_seconds) },
      { "expected_energy_delta_percentage", finiteJsonNumberOrNull(score.expected_energy_delta_percentage) },
      { "expected_fault_burden", finiteJsonNumberOrNull(score.expected_fault_burden) },
      { "expected_resource_demand", finiteJsonNumberOrNull(score.expected_resource_demand) },
      { "expected_uncertainty", finiteJsonNumberOrNull(score.expected_uncertainty) },
      { "final_rapfs", finiteJsonNumberOrNull(score.final_rapfs) },
      { "normalized_scores", score.normalized_scores },
      { "weighted_scores", score.weighted_scores },
      { "score_weights", score.score_weights },
      { "score_components", parseScoreComponentsJson(score.score_components) },
      { "calibration_version", score.calibration_version },
      { "normalizer_version", score.normalizer_version },
      { "estimator_version", score.estimator_version },
      { "weight_version", score.weight_version }
    };
  }

  static std::string buildFeedbackVersionKey(const PlanFeedbackRecord& feedback)
  {
    return feedback.calibration_version + "|" + feedback.normalizer_version + "|" + feedback.estimator_version +
           "|" + feedback.weight_version;
  }

  static std::string normalizeUserFeedbackOutcome(const std::string& value)
  {
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char ch : value)
    {
      if (std::isalnum(ch))
        normalized.push_back(static_cast<char>(std::tolower(ch)));
      else if (ch == '_' || ch == '-' || std::isspace(ch))
        normalized.push_back('_');
    }

    while (!normalized.empty() && normalized.front() == '_')
      normalized.erase(normalized.begin());
    while (!normalized.empty() && normalized.back() == '_')
      normalized.pop_back();

    return normalized;
  }

  static std::optional<bool> userFeedbackSuccessOverride(const nlohmann::json& entry)
  {
    const auto it = entry.find("feedback_success");
    if (it == entry.end() || !it->is_boolean())
      return std::nullopt;
    return it->get<bool>();
  }

  static std::vector<std::string> readStringListField(const nlohmann::json& entry, const char* key)
  {
    std::vector<std::string> values;
    const auto it = entry.find(key);
    if (it == entry.end() || !it->is_array())
      return values;

    for (const auto& item : *it)
    {
      if (item.is_string())
        values.push_back(item.get<std::string>());
    }
    return values;
  }

  static std::string buildUserFeedbackConsumptionSignature(const nlohmann::json& entry)
  {
    nlohmann::json signature = {
      { "outcome", entry.value("outcome", std::string()) },
      { "feedback", entry.value("feedback", std::string()) },
      { "feedback_success", nullptr },
      { "feedback_failed_candidate_node_ids", readStringListField(entry, "feedback_failed_candidate_node_ids") },
      { "feedback_failed_skill_names", readStringListField(entry, "feedback_failed_skill_names") }
    };

    const auto feedback_success = userFeedbackSuccessOverride(entry);
    if (feedback_success.has_value())
      signature["feedback_success"] = *feedback_success;

    return signature.dump();
  }

  static bool hasStructuredUserFeedback(const nlohmann::json& entry)
  {
    if (!entry.value("outcome", std::string()).empty() || !entry.value("feedback", std::string()).empty())
      return true;
    if (userFeedbackSuccessOverride(entry).has_value())
      return true;
    if (!readStringListField(entry, "feedback_failed_candidate_node_ids").empty())
      return true;
    if (!readStringListField(entry, "feedback_failed_skill_names").empty())
      return true;
    return false;
  }

  static std::string joinStringVector(const std::vector<std::string>& values, const char* delimiter)
  {
    std::ostringstream stream;
    for (size_t index = 0; index < values.size(); ++index)
    {
      if (index > 0)
        stream << delimiter;
      stream << values[index];
    }
    return stream.str();
  }

  static std::string terminalOutcomeFromSuccess(const bool success)
  {
    return success ? "success" : "failure";
  }

  static bool tryApplyUserFeedbackOutcome(PlanFeedbackRecord& feedback, const std::string& user_outcome)
  {
    const std::string normalized = normalizeUserFeedbackOutcome(user_outcome);
    if (normalized.empty())
      return false;

    if (normalized == "success" || normalized == "succeeded" || normalized == "completed")
    {
      feedback.terminal_outcome = "success";
      feedback.terminal_success = true;
      return true;
    }

    if (normalized == "partial_success" || normalized == "partial" || normalized == "partially_succeeded")
    {
      feedback.terminal_outcome = "partial_success";
      feedback.terminal_success = false;
      return true;
    }

    if (normalized == "failure" || normalized == "failed")
    {
      feedback.terminal_outcome = "failure";
      feedback.terminal_success = false;
      return true;
    }

    return false;
  }

  void recordConsumedFeedbackEpochs(const std::unordered_map<std::string, PlanFeedbackRecord>& consumed_versions,
                                    const size_t consumed_count)
  {
    if (consumed_versions.empty())
      return;

    const auto now = this->now();
    const int32_t recorded_at_sec = static_cast<int32_t>(now.seconds());
    const uint32_t recorded_at_nanosec = static_cast<uint32_t>(now.nanoseconds() % 1000000000ULL);

    for (const auto& [version_key, feedback] : consumed_versions)
    {
      (void)version_key;

      ScoringEpochRecord record;
      record.epoch_id = "feedback_ingest_" + std::to_string(now.nanoseconds()) + "_" +
                        sanitizeFeedbackPathComponent(feedback.calibration_version) + "_" +
                        sanitizeFeedbackPathComponent(feedback.normalizer_version) + "_" +
                        sanitizeFeedbackPathComponent(feedback.estimator_version) + "_" +
                        sanitizeFeedbackPathComponent(feedback.weight_version);
      record.strategy_name = "user_feedback_ingest";
      record.calibration_version = feedback.calibration_version;
      record.normalizer_version = feedback.normalizer_version;
      record.estimator_version = feedback.estimator_version;
      record.weight_version = feedback.weight_version;
      record.uncertainty_weight_locked = true;
      record.calibration_summary = "feedback consume run imported " + std::to_string(consumed_count) +
                                   " updated user feedback entries";
      record.selection_bias_summary = "feedback consume run snapshot after user feedback import";
      record.active = false;
      record.start_sec = recorded_at_sec;
      record.start_nanosec = recorded_at_nanosec;
      record.end_sec = recorded_at_sec;
      record.end_nanosec = recorded_at_nanosec;
      record.recorded_at_sec = recorded_at_sec;
      record.recorded_at_nanosec = recorded_at_nanosec;
      record.provenance = "experience_server consume_feedback";

      if (!core_plugin_->recordScoringEpoch(record))
      {
        RCLCPP_WARN(this->get_logger(), "Failed to record scoring epoch snapshot for consumed feedback epoch_id=%s",
                    record.epoch_id.c_str());
      }
    }
  }

  void persistSelectedPlanUserFeedback(const PlanningRequest& request, const PlanningResult& planning_result)
  {
    const CandidatePlan* selected_candidate = nullptr;
    for (const auto& candidate : planning_result.candidates)
    {
      if (candidate.candidate_plan_id == planning_result.selected_candidate_plan_id)
      {
        selected_candidate = &candidate;
        break;
      }
    }

    if (selected_candidate == nullptr)
      return;

    const auto file_path = resolveUserFeedbackFilePath(request);

    std::lock_guard<std::mutex> lock(user_feedback_file_mutex_);

    nlohmann::json document;
    if (!loadUserFeedbackDocument(file_path, document))
      return;

    const std::vector<std::string> score_names = { "success", "time", "energy", "fault", "resource",
                                                   "uncertainty" };
    nlohmann::json candidate_node_ids = nlohmann::json::array();
    nlohmann::json candidate_skill_names = nlohmann::json::array();
    for (const auto& node : selected_candidate->nodes)
    {
      candidate_node_ids.push_back(node.candidate_node_id);
      candidate_skill_names.push_back(node.skill_name);
    }
    nlohmann::json expected_scores = nlohmann::json::array(
      { selected_candidate->score.terminal_success_estimate, selected_candidate->score.expected_runtime_seconds,
        selected_candidate->score.expected_energy_delta_percentage, selected_candidate->score.expected_fault_burden,
        selected_candidate->score.expected_resource_demand, selected_candidate->score.expected_uncertainty });
    const nlohmann::json normalized_scores = selected_candidate->score.normalized_scores;
    const nlohmann::json weighted_scores = selected_candidate->score.weighted_scores;
    const nlohmann::json score_weights = selected_candidate->score.score_weights;

    const std::string stage_key = std::to_string(nextUserFeedbackStageIndex(document));
    document[stage_key] = {
      { "plan", selected_candidate->plan },
      { "reasoning", selected_candidate->reasoning },
      { "scoring_supported", selected_candidate->score.supported },
      { "feasible", selected_candidate->score.feasible },
      { "sequential_graph", selected_candidate->score.sequential_graph },
      { "feasibility_reason", selected_candidate->score.reason },
      { "score_name_list", score_names },
      { "expected_scores", expected_scores },
      { "normalized_scores", normalized_scores },
      { "weighted_scores", weighted_scores },
      { "score_weights", score_weights },
      { "final_rapfs", finiteJsonNumberOrNull(selected_candidate->score.final_rapfs) },
      { "score_components", parseScoreComponentsJson(selected_candidate->score.score_components) },
      { "calibration_version", selected_candidate->score.calibration_version },
      { "normalizer_version", selected_candidate->score.normalizer_version },
      { "estimator_version", selected_candidate->score.estimator_version },
      { "weight_version", selected_candidate->score.weight_version },
      { "candidate_node_id_list", candidate_node_ids },
      { "candidate_skill_name_list", candidate_skill_names },
      { "feedback_success", nullptr },
      { "feedback_failed_candidate_node_ids", nlohmann::json::array() },
      { "feedback_failed_skill_names", nlohmann::json::array() },
      { "outcome", "" },
      { "feedback", "" },
      { "outcome_consumed", false },
      { "last_consumed_outcome", "" },
      { "feedback_consumed", false },
      { "last_consumed_feedback", "" },
      { "last_consumed_signature", "" },
      { "feedback_consumed_at_sec", 0 },
      { "feedback_consumed_at_nanosec", 0 },
      { "planning_request_id", request.planning_request_id },
      { "candidate_plan_id", selected_candidate->candidate_plan_id },
      { "graph_hash", selected_candidate->graph_hash },
      { "execution_uuid", request.uuid },
      { "recorded_at_sec", request.recorded_at_sec },
      { "recorded_at_nanosec", request.recorded_at_nanosec }
    };

    if (writeUserFeedbackDocument(file_path, document))
    {
      RCLCPP_INFO(this->get_logger(),
                  "Persisted selected-plan feedback stub at %s (execution_uuid=%s, planning_request_id=%s, stage=%s)",
                  file_path.c_str(), request.uuid.c_str(), request.planning_request_id.c_str(), stage_key.c_str());
    }
  }

  void persistGeneratedPlansArtifact(const PlanningRequest& request,
                                     const std::vector<PlanningResult>& generation_results)
  {
    if (generation_results.empty())
      return;

    const auto file_path = resolveGeneratedPlansFilePath(request);
    std::lock_guard<std::mutex> lock(user_feedback_file_mutex_);

    nlohmann::json document;
    if (!loadUserFeedbackDocument(file_path, document))
      return;

    const int iteration_offset = nextUserFeedbackStageIndex(document) - 1;
    for (size_t iteration_index = 0; iteration_index < generation_results.size(); ++iteration_index)
    {
      const auto& planning_result = generation_results[iteration_index];
      const std::string iteration_key = "iteration_" + std::to_string(iteration_offset + iteration_index + 1);
      nlohmann::json iteration = nlohmann::json::object();
      iteration["selected_candidate_plan_id"] = planning_result.selected_candidate_plan_id;
      iteration["selected_reasoning"] = planning_result.selected_reasoning;
      iteration["candidate_count"] = planning_result.candidates.size();

      for (size_t candidate_index = 0; candidate_index < planning_result.candidates.size(); ++candidate_index)
      {
        const auto& candidate = planning_result.candidates[candidate_index];
        nlohmann::json candidate_node_ids = nlohmann::json::array();
        nlohmann::json candidate_skill_names = nlohmann::json::array();
        for (const auto& node : candidate.nodes)
        {
          candidate_node_ids.push_back(node.candidate_node_id);
          candidate_skill_names.push_back(node.skill_name);
        }

        nlohmann::json candidate_record = planScoreToJson(candidate.score);
        candidate_record["planning_request_id"] = request.planning_request_id;
        candidate_record["candidate_plan_id"] = candidate.candidate_plan_id;
        candidate_record["graph_hash"] = candidate.graph_hash;
        candidate_record["selected"] = candidate.candidate_plan_id == planning_result.selected_candidate_plan_id;
        candidate_record["ranking_index"] = candidate_index + 1;
        candidate_record["terminal_success"] = candidate.score.feasible;
        candidate_record["terminal_outcome"] = "";
        candidate_record["rejection_reason"] = experience::server::feedback::summarizePersistedCandidateReason(candidate);
        candidate_record["plan"] = candidate.plan;
        candidate_record["reasoning"] = candidate.reasoning;
        candidate_record["candidate_node_id_list"] = candidate_node_ids;
        candidate_record["candidate_skill_name_list"] = candidate_skill_names;
        candidate_record["recorded_at_sec"] = request.recorded_at_sec;
        candidate_record["recorded_at_nanosec"] = request.recorded_at_nanosec;

        iteration["plan_" + std::to_string(candidate_index + 1)] = candidate_record;
      }

      document[iteration_key] = iteration;
    }

    if (writeUserFeedbackDocument(file_path, document))
    {
      RCLCPP_INFO(this->get_logger(), "Persisted generated-plan artifact at %s (planning_request_id=%s)",
                  file_path.c_str(), request.planning_request_id.c_str());
    }
  }

  bool applyUserFeedbackNodeOverrides(const SelectedPlanScoreExplanationBundle& existing,
                                      const std::vector<std::string>& failed_candidate_node_ids,
                                      const std::vector<std::string>& failed_skill_names,
                                      const int32_t recorded_at_sec,
                                      const uint32_t recorded_at_nanosec)
  {
    if (failed_candidate_node_ids.empty() && failed_skill_names.empty())
      return true;

    std::unordered_set<std::string> failed_node_id_set(failed_candidate_node_ids.begin(), failed_candidate_node_ids.end());
    std::unordered_set<std::string> failed_skill_name_set(failed_skill_names.begin(), failed_skill_names.end());
    size_t applied_count = 0;

    for (const auto& node : existing.node_explanations)
    {
      if (failed_node_id_set.count(node.candidate_node_id) == 0 && failed_skill_name_set.count(node.skill_name) == 0)
        continue;

      const bool stored = core_plugin_->recordCandidatePlanSkillAttribution(CandidatePlanSkillAttribution{
        existing.feedback.planning_request_id,
        existing.feedback.candidate_plan_id,
        existing.feedback.graph_hash,
        node.candidate_node_id,
        "",
        "",
        node.skill_name,
        node.skill_id,
        node.requested_context_key,
        node.requested_context_class,
        node.selected_skill_id,
        node.selected_key,
        node.estimator_window,
        node.fallback_level,
        node.proxy_source,
        true,
        false,
        "failure",
        recorded_at_sec,
        recorded_at_nanosec,
        "consumed user_feedback.json targeted node override"
      });
      if (!stored)
        return false;

      ++applied_count;
    }

    if (applied_count == 0)
    {
      RCLCPP_WARN(this->get_logger(),
                  "User feedback requested targeted runner penalty but matched no nodes for planning_request_id=%s candidate_plan_id=%s",
                  existing.feedback.planning_request_id.c_str(), existing.feedback.candidate_plan_id.c_str());
    }
    else
    {
      RCLCPP_INFO(this->get_logger(),
                  "Applied %zu targeted runner feedback override(s) for planning_request_id=%s candidate_plan_id=%s failed_candidate_node_ids=[%s] failed_skill_names=[%s]",
                  applied_count, existing.feedback.planning_request_id.c_str(), existing.feedback.candidate_plan_id.c_str(),
                  joinStringVector(failed_candidate_node_ids, ",").c_str(),
                  joinStringVector(failed_skill_names, ",").c_str());
    }

    return true;
  }

  bool mergePersistedUserFeedback(const std::string& planning_request_id, const std::string& candidate_plan_id,
                                  const std::string& graph_hash, const std::string& user_outcome,
                                  const std::string& human_judgement,
                                  const std::optional<bool>& feedback_success,
                                  const std::vector<std::string>& failed_candidate_node_ids,
                                  const std::vector<std::string>& failed_skill_names,
                                  const std::string& provenance, PlanFeedbackRecord* merged_feedback = nullptr)
  {
    const PlanNodeScoreExplanationQuery query{ planning_request_id, candidate_plan_id, graph_hash };
    const auto existing = core_plugin_->readSelectedPlanScoreExplanationBundle(query);
    if (!existing.has_value())
    {
      RCLCPP_WARN(this->get_logger(),
                  "Skipping user feedback for unknown selected plan planning_request_id=%s candidate_plan_id=%s graph_hash=%s",
                  planning_request_id.c_str(), candidate_plan_id.c_str(), graph_hash.c_str());
      return false;
    }

    PlanFeedbackRecord feedback = existing->feedback;
    feedback.planning_request_id = planning_request_id;
    feedback.candidate_plan_id = candidate_plan_id;
    feedback.graph_hash = graph_hash;

    if (!user_outcome.empty() && !tryApplyUserFeedbackOutcome(feedback, user_outcome))
    {
      RCLCPP_WARN(this->get_logger(), "Skipping user feedback with unsupported outcome '%s' for planning_request_id=%s",
                  user_outcome.c_str(), planning_request_id.c_str());
      return false;
    }
    else if (user_outcome.empty() && feedback_success.has_value())
    {
      feedback.terminal_success = *feedback_success;
      feedback.terminal_outcome = terminalOutcomeFromSuccess(*feedback_success);
    }

    feedback.human_judgement = human_judgement;

    const auto stamp = this->now();
    feedback.recorded_at_sec = static_cast<int32_t>(stamp.seconds());
    feedback.recorded_at_nanosec = static_cast<uint32_t>(stamp.nanoseconds() % 1000000000ULL);
    feedback.provenance = provenance;
    const bool stored = core_plugin_->recordPlanFeedback(feedback);
    if (stored && !applyUserFeedbackNodeOverrides(*existing, failed_candidate_node_ids, failed_skill_names,
                                                  feedback.recorded_at_sec, feedback.recorded_at_nanosec))
      return false;
    if (stored && merged_feedback != nullptr)
      *merged_feedback = feedback;
    return stored;
  }

  void consumePersistedUserFeedback()
  {
    if (!consume_feedback_)
      return;

    const auto root_dir = resolveUserFeedbackRootDir();
    if (!std::filesystem::exists(root_dir))
    {
      RCLCPP_INFO(this->get_logger(), "consume_feedback enabled but no user feedback directory exists at %s",
                  root_dir.c_str());
      return;
    }

    size_t consumed_count = 0;
    size_t skipped_count = 0;
  std::unordered_map<std::string, PlanFeedbackRecord> consumed_versions;
    std::error_code ec;
    for (std::filesystem::recursive_directory_iterator it(root_dir, ec), end; it != end; it.increment(ec))
    {
      if (ec)
      {
        RCLCPP_WARN(this->get_logger(), "Failed while walking user feedback directory %s: %s", root_dir.c_str(),
                    ec.message().c_str());
        break;
      }

      if (!it->is_regular_file() || it->path().filename() != "user_feedback.json")
        continue;

      nlohmann::json document;
      if (!loadUserFeedbackDocument(it->path(), document))
      {
        ++skipped_count;
        continue;
      }

      bool document_changed = false;

      for (auto entry_it = document.begin(); entry_it != document.end(); ++entry_it)
      {
        if (!entry_it.value().is_object())
        {
          ++skipped_count;
          continue;
        }

        const std::string user_outcome = entry_it.value().value("outcome", std::string());
        const std::string human_judgement = entry_it.value().value("feedback", std::string());
        const std::optional<bool> feedback_success = userFeedbackSuccessOverride(entry_it.value());
        const std::vector<std::string> failed_candidate_node_ids =
          readStringListField(entry_it.value(), "feedback_failed_candidate_node_ids");
        const std::vector<std::string> failed_skill_names =
          readStringListField(entry_it.value(), "feedback_failed_skill_names");
        if (!hasStructuredUserFeedback(entry_it.value()))
          continue;

        const std::string consumed_signature = buildUserFeedbackConsumptionSignature(entry_it.value());
        const std::string last_consumed_signature = entry_it.value().value("last_consumed_signature", std::string());
        const std::string last_consumed_outcome = entry_it.value().value("last_consumed_outcome", std::string());
        const std::string last_consumed_feedback = entry_it.value().value("last_consumed_feedback", std::string());
        if ((!last_consumed_signature.empty() && consumed_signature == last_consumed_signature) ||
            (last_consumed_signature.empty() && user_outcome == last_consumed_outcome &&
             human_judgement == last_consumed_feedback))
        {
          entry_it.value()["outcome_consumed"] = !user_outcome.empty();
          entry_it.value()["feedback_consumed"] = true;
          continue;
        }

        const std::string planning_request_id = entry_it.value().value("planning_request_id", std::string());
        const std::string candidate_plan_id = entry_it.value().value("candidate_plan_id", std::string());
        const std::string graph_hash = entry_it.value().value("graph_hash", std::string());
        if (planning_request_id.empty() || candidate_plan_id.empty() || graph_hash.empty())
        {
          ++skipped_count;
          continue;
        }

        PlanFeedbackRecord merged_feedback;
        if (mergePersistedUserFeedback(planning_request_id, candidate_plan_id, graph_hash, user_outcome,
                                       human_judgement, feedback_success, failed_candidate_node_ids,
                                       failed_skill_names,
                                       "consumed user_feedback.json", &merged_feedback))
        {
          ++consumed_count;
          consumed_versions[buildFeedbackVersionKey(merged_feedback)] = merged_feedback;

          if (feedback_success.has_value() || !failed_candidate_node_ids.empty() || !failed_skill_names.empty())
          {
            RCLCPP_INFO(this->get_logger(),
                        "Consumed structured user feedback from %s stage=%s planning_request_id=%s candidate_plan_id=%s feedback_success=%s failed_candidate_node_ids=[%s] failed_skill_names=[%s]",
                        it->path().c_str(), entry_it.key().c_str(), planning_request_id.c_str(), candidate_plan_id.c_str(),
                        feedback_success.has_value() ? (*feedback_success ? "true" : "false") : "unset",
                        joinStringVector(failed_candidate_node_ids, ",").c_str(),
                        joinStringVector(failed_skill_names, ",").c_str());
          }

          const auto stamp = this->now();
          entry_it.value()["outcome_consumed"] = !user_outcome.empty();
          entry_it.value()["last_consumed_outcome"] = user_outcome;
          entry_it.value()["feedback_consumed"] = true;
          entry_it.value()["last_consumed_feedback"] = human_judgement;
          entry_it.value()["last_consumed_signature"] = consumed_signature;
          entry_it.value()["feedback_consumed_at_sec"] = static_cast<int32_t>(stamp.seconds());
          entry_it.value()["feedback_consumed_at_nanosec"] =
            static_cast<uint32_t>(stamp.nanoseconds() % 1000000000ULL);
          document_changed = true;
        }
        else
          ++skipped_count;
      }

      if (document_changed && !writeUserFeedbackDocument(it->path(), document))
        ++skipped_count;
    }

    recordConsumedFeedbackEpochs(consumed_versions, consumed_count);

    RCLCPP_INFO(this->get_logger(), "Consumed %zu user feedback entries from %s (%zu skipped)", consumed_count,
                root_dir.c_str(), skipped_count);
  }