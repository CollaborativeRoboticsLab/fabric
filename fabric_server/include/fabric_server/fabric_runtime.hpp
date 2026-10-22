#pragma once

#include <string>
#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <iomanip>
#include <unordered_map>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>
#include <deque>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <unordered_set>
#include <map>

#include <ament_index_cpp/get_package_share_directory.hpp>
#include <tinyxml2.h>
#include <uuid/uuid.h>
#include <rclcpp/rclcpp.hpp>
#include <pluginlib/class_loader.hpp>
#include <fabric_base/llm_analysis_base.hpp>
#include <fabric_base/monitor_base.hpp>
#include <fabric_base/utils/structs.hpp>
#include <fabric_base/utils/metadata_structs.hpp>
#include <fabric_base/utils/plan_structs.hpp>
#include <fabric_base/utils/planning_structs.hpp>
#include <fabric_base/utils/xml_helper.hpp>

#include <fabric_base/validation_base.hpp>
#include <fabric_base/parser_base.hpp>
#include <fabric_base/generation_base.hpp>
#include <fabric_base/planner_base.hpp>
#include <fabric_base/scorer_base.hpp>
#include <fabric_base/validation_base.hpp>

#include <fabric_server/capability_client.hpp>
#include <fabric_server/bond_client.hpp>
#include <fabric_server/utils/experience_runtime.hpp>
#include <fabric_server/utils/feedback.hpp>
#include <fabric_server/utils/planning.hpp>
#include <fabric_server/utils/status.hpp>
#include <fabric_server/utils/telemetry.hpp>

namespace fabric
{
/** @brief Transport-neutral inputs for a plan-generation operation.
 *
 * @param task The natural-language task to be passed to the generation or planning plugin.
 * @param uuid The caller-supplied generation context identifier.
 * @param flush Whether the generation plugin should flush its request context.
 * @param auto_queue Whether to queue direct-generation output for execution.
 */
struct GenerationRequest
{
  std::string task;
  std::string uuid;
  bool flush{ false };
  bool auto_queue{ false };
};

/** @brief Transport-neutral milestones emitted during plan generation. */
enum class GenerationStage
{
  Requested,
  InformationCollected,
  PlansScored,
  PlanSelected,
  Complete
};

/** @brief One generation milestone and its optional associated plan ID. */
struct GenerationProgress
{
  GenerationStage stage;
  std::string plan_id;
};

/** @brief Generated plan data and the terminal outcome of a generation operation. */
struct GenerationResult
{
  /** @brief Whether generation succeeded, observed cancellation, or failed. */
  enum class State
  {
    Succeeded,
    Cancelled,
    Failed
  };
  State state{ State::Failed };
  std::string plan;
  std::string reasoning;
  std::string plan_id;
};

/**
 * @brief Optional adapter hooks invoked from a runtime-owned generation worker.
 *
 * Cancellation is polled cooperatively between plugin operations; it does not
 * interrupt an executing plugin call. Hooks must return promptly and must not
 * shut down or destroy the runtime from its worker. Captured state must remain
 * valid until completion, including during externally initiated shutdown.
 */
struct GenerationCallbacks
{
  /** @brief Return true when the caller requests cancellation. */
  std::function<bool()> cancelled;
  /** @brief Receive progress without depending on ROS action messages. */
  std::function<void(const GenerationProgress&)> progress;
  /** @brief Receive the terminal result when accepted generation work finishes. */
  std::function<void(const GenerationResult&)> complete;
};

/**
 * @brief Own Fabric plugins, execution state, generation workers, and persisted feedback.
 *
 * Exposes internal data types to ROS and other adapters without owning service
 * or action endpoints. Plugins and capability clients still require a live ROS
 * node and an executor servicing their dependencies.
 *
 * The supplied node must outlive this runtime. Call initialize() once, and
 * coordinate shutdownRuntime() externally with adapter calls. Generation and
 * explicit scoring serialize their plugin pipeline; this is not a guarantee
 * that every runtime operation can overlap shutdown or arbitrary plugin calls.
 */
class FabricRuntime
{
public:
  using PlanningRequest = fabric::planning::Request;
  using PlanningRefinementRequest = fabric::planning::RefinementRequest;
  using PlanningResult = fabric::planning::Result;
  using CandidatePlan = fabric::planning::CandidatePlan;
  using CandidatePlanNode = fabric::planning::CandidatePlanNode;
  using CandidatePlanEdge = fabric::planning::CandidatePlanEdge;
  using CandidateValidationSummary = fabric::planning::CandidateValidationSummary;
  using PendingGeneratedPlan = fabric::planning::PendingGeneratedPlan;
  using ValidationFindingLevel = fabric::planning::ValidationFindingLevel;
  using SkillInfo = fabric::plan::node;
  using CandidateSkillRequest = fabric::experience::CandidateSkillRequest;
  using CandidatePlanSkillAttribution = fabric::experience::CandidatePlanSkillAttribution;
  using CandidatePlanEdgeAttribution = fabric::experience::CandidatePlanEdgeAttribution;
  using PlanNodeScoreExplanationRecord = fabric::experience::PlanNodeScoreExplanationRecord;
  using PlanNodeScoreExplanationQuery = fabric::experience::PlanNodeScoreExplanationQuery;
  using PlanNodeScoreExplanationNodeQuery = fabric::experience::PlanNodeScoreExplanationNodeQuery;
  using PlanFeedbackRecord = fabric::experience::PlanFeedbackRecord;
  using ScoringEpochRecord = fabric::experience::ScoringEpochRecord;
  using SystemStatus = fabric::status::SystemStatus;
  using TelemetryRecord = fabric::server::status::TelemetryRecord;

  /**
   * @brief Struct representing an integrated generation run
   *
   * Contains the original planning request, all generated results, and the selected result.
   *
   */
  struct IntegratedGenerationRun
  {
    PlanningRequest request;
    std::vector<PlanningResult> generation_results;
    PlanningResult selected_result;
  };

  /**
   * @brief Construct plugin loaders without starting runtime workers.
   * @param node Non-owning ROS node reference used for parameters, logging, and clients.
   * @pre The ROS context and required package/plugin metadata are available.
   */
  explicit FabricRuntime(rclcpp::Node& node)
    : node_(node)
    , plan_validation_loader_("fabric_base", "fabric::ValidationBase")
    , parsing_loader_("fabric_base", "fabric::ParserBase")
    , generation_loader_("fabric_base", "fabric::GenerationBase")
    , llm_analysis_loader_("fabric_base", "fabric::LlmAnalysisBase")
    , monitor_loader_("fabric_base", "fabric::MonitorBase")
    , core_loader_("fabric_base", "fabric::CoreBase")
    , planner_loader_("fabric_base", "fabric::PlannerBase")
    , scorer_loader_("fabric_base", "fabric::ScorerBase")
    , validation_loader_("fabric_base", "fabric::ValidationBase")
    , estimator_loader_("fabric_base", "fabric::EstimateBase")
  {
  }

  /** @brief Shut down workers and plugins before releasing runtime state. */
  ~FabricRuntime()
  {
    shutdownRuntime();
  }

  using ScoreBundle = fabric::experience::SelectedPlanScoreExplanationBundle;
  /** @brief Synchronous status hook invoked on the thread producing a plan update. */
  using StatusCallback = std::function<void(const fabric::plan::Plan&)>;

  /**
   * @brief Load parameters/plugins, queue the starter plan, and begin asynchronous startup.
   * @param status_callback Optional plan-status sink; receives borrowed snapshots synchronously.
   * @pre The node is shared-owned and this runtime has not been initialized or stopped.
   * @note The callback can run during initialization or on execution/caller threads;
   *       it must be thread-safe, return promptly, and avoid reentering runtime operations.
   */
  void initialize(StatusCallback status_callback)
  {
    status_callback_ = std::move(status_callback);
    loadRuntimeParameters();
    initializePlanValidationPlugins();
    initializeParsingPlugin();
    initializeLlmAnalysisPlugin();
    initializeMonitorPlugins();
    initializeGenerationPlugin();
    initializeCapabilityClient();
    queueStarterPlan();
    RCLCPP_INFO(node_.get_logger(), "[server] Fabric node initialized. Waiting for capabilities2 readiness.");
    startBackgroundThreads();
  }

  /** @brief Return whether startup completed and shutdown has not begun. */
  bool ready() const
  {
    return startup_complete_.load() && !shutting_down_.load();
  }

  /** @brief Return whether the optional experience pipeline is ready for requests. */
  bool experienceReady() const
  {
    return ready() && experience_ready_.load();
  }

  /**
   * @brief Check parser compatibility, assign an execution ID, and enqueue a plan.
   * @param plan Plan XML and optional attribution metadata; copied into the execution queue.
   * @return Newly assigned execution plan ID.
   * @pre initialize() has completed; capabilities may still be becoming ready.
   * @throws std::runtime_error If stopping or incompatible with the parser.
   */
  std::string submitPlan(fabric::plan::Plan plan)
  {
    if (shutting_down_.load())
    {
      throw std::runtime_error("Fabric runtime is shutting down");
    }
    if (!parsing_plugin_->check_compatibility(plan))
    {
      throw std::runtime_error("[server] Plan is not compatible with the loaded parser.");
    }
    plan.plan_id = generate_uuid();
    plan.status = fabric::plan::Status::QUEUED;
    enqueuePlan(plan);
    start_execution_process_thread();
    return plan.plan_id;
  }

  /**
   * @brief Parse plan XML into internal connections without queueing execution.
   * @param xml Plan definition accepted by the configured parser.
   * @return Parsed plan with connection data.
   * @pre initialize() has completed and shutdown has not begun.
   * @note Parser exceptions propagate to the caller.
   */
  fabric::plan::Plan parsePlan(const std::string& xml)
  {
    fabric::plan::Plan plan;
    plan.plan = xml;
    plan.plan_id = "fabric_parse_request";
    parsing_plugin_->parsePlan(plan);
    return plan;
  }

  /**
   * @brief Cancel a current or queued execution plan and stop its bond if present.
   * @param plan_id Execution plan ID, not a generation action goal ID.
   * @return Whether a matching plan was found.
   */
  bool cancelPlan(const std::string& plan_id)
  {
    std::lock_guard<std::recursive_mutex> lock(current_plan_mutex_);
    std::string bond_id_to_cancel;
    bool found = false;
    if (current_plan_.plan_id == plan_id)
    {
      setCurrentPlanStatus(fabric::plan::Status::CANCELLED);
      bond_id_to_cancel = current_plan_.bond_id;
      markPlanCompleted();
      found = true;
    }
    else
    {
      found = tryCancelQueuedPlan(plan_id, bond_id_to_cancel);
    }
    const auto bond = bond_client_cache_.find(bond_id_to_cancel);
    if (bond != bond_client_cache_.end())
    {
      bond->second->stop();
    }
    return found;
  }

  /**
   * @brief Wake execution after receiving completion for the current plan.
   * @param plan_id Execution plan ID reported complete.
   * @return Whether the ID matches the current plan.
   */
  bool completePlan(const std::string& plan_id)
  {
    std::lock_guard<std::recursive_mutex> lock(current_plan_mutex_);
    if (current_plan_.plan_id != plan_id)
    {
      return false;
    }
    markPlanCompleted();
    return true;
  }

  /**
   * @brief Copy a matching current or queued plan under the runtime's state locks.
   * @param plan_id Optional execution plan ID.
   * @param bond_id Optional bond ID; either nonempty identifier may match.
   * @return Plan snapshot, or nullopt if no match; both IDs empty selects the current plan.
   */
  std::optional<fabric::plan::Plan> getPlanStatus(const std::string& plan_id, const std::string& bond_id)
  {
    std::lock_guard<std::recursive_mutex> current_lock(current_plan_mutex_);
    if ((plan_id.empty() && bond_id.empty()) || (!plan_id.empty() && current_plan_.plan_id == plan_id) ||
        (!bond_id.empty() && current_plan_.bond_id == bond_id))
    {
      return current_plan_;
    }
    std::lock_guard<std::mutex> queue_lock(queue_mutex_);
    for (const auto& plan : plan_queue_)
    {
      if ((!plan_id.empty() && plan.plan_id == plan_id) || (!bond_id.empty() && plan.bond_id == bond_id))
      {
        return plan;
      }
    }
    return std::nullopt;
  }

  /**
   * @brief Schedule generation on a runtime-owned worker and reap completed workers.
   * @param request Task and generation options copied into the worker.
   * @param callbacks Optional cancellation, progress, and completion hooks.
   * @return False if not ready or the task is empty; no hooks run on rejection.
   * @note Accepted jobs share a serialized generation/scoring pipeline. Plugin
   *       failures become failed results; worker-creation exceptions propagate.
   */
  bool startGeneration(GenerationRequest request, GenerationCallbacks callbacks)
  {
    std::lock_guard<std::mutex> lock(generation_workers_mutex_);
    if (!ready() || request.task.empty())
    {
      return false;
    }
    for (auto worker = generation_workers_.begin(); worker != generation_workers_.end();)
    {
      if (worker->finished->load())
      {
        worker->thread.join();
        worker = generation_workers_.erase(worker);
      }
      else
      {
        ++worker;
      }
    }
    auto finished = std::make_shared<std::atomic<bool>>(false);
    generation_workers_.push_back(GenerationWorker{ std::thread(), finished });
    try
    {
      generation_workers_.back().thread = std::thread([this, request = std::move(request), callbacks = std::move(callbacks), finished]() {
        GenerationResult result;
        try
        {
          std::lock_guard<std::mutex> operation_lock(generation_operation_mutex_);
          result = generatePlan(request, callbacks);
        }
        catch (const std::exception& error)
        {
          result.reasoning = error.what();
        }
        try
        {
          if (callbacks.complete)
          {
            callbacks.complete(result);
          }
        }
        catch (const std::exception& error)
        {
          RCLCPP_WARN(node_.get_logger(), "Generation completion callback failed: %s", error.what());
        }
        finished->store(true);
      });
    }
    catch (...)
    {
      generation_workers_.pop_back();
      throw;
    }
    return true;
  }

  /**
   * @brief Read a persisted selected-plan score explanation bundle.
   * @param query Planning request, candidate, and graph identity.
   * @return Stored bundle, or nullopt when absent.
   * @throws std::runtime_error If the experience pipeline is unavailable.
   */
  std::optional<ScoreBundle> readScoreExplanation(const PlanNodeScoreExplanationQuery& query)
  {
    requireExperience();
    return core_plugin_->readSelectedPlanScoreExplanationBundle(query);
  }

  /**
   * @brief Read a persisted score explanation for one candidate node.
   * @param query Plan identity and candidate node ID.
   * @return Stored node explanation, or nullopt when absent.
   * @throws std::runtime_error If the experience pipeline is unavailable.
   */
  std::optional<PlanNodeScoreExplanationRecord> readNodeScoreExplanation(const PlanNodeScoreExplanationNodeQuery& query)
  {
    requireExperience();
    return core_plugin_->readPlanNodeScoreExplanation(query);
  }

  /**
   * @brief Parse, validate, and score supplied plan XML without execution dispatch.
   * @param request Task context; missing request identity and timestamps are populated internally.
   * @param candidate_id Candidate identity, or empty to derive one from the request ID.
   * @param xml Plan definition to score.
   * @return Candidate validation and scoring result.
   * @throws std::runtime_error If the experience pipeline is unavailable.
   * @note Shares the generation pipeline lock; plugin exceptions propagate.
   */
  PlanningResult scorePlan(PlanningRequest request, const std::string& candidate_id, const std::string& xml)
  {
    requireExperience();
    std::lock_guard<std::mutex> lock(generation_operation_mutex_);
    stampPlanningRequest(request);
    PlanningResult result;
    result.candidates.push_back(
        parseCandidatePlanForScoring(request, candidate_id.empty() ? request.planning_request_id + "/candidate_0" : candidate_id, xml));
    validatePlanningResult(request, result);
    scorer_plugin_->scorePlanningResult(request, result);
    return result;
  }

  /**
   * @brief Read an epoch with current calibration and selection-bias summaries merged in.
   * @param epoch_id Stored scoring epoch identity.
   * @return Enriched epoch, or nullopt when absent.
   * @throws std::runtime_error If the experience pipeline is unavailable.
   */
  std::optional<ScoringEpochRecord> readScoringEpoch(const std::string& epoch_id)
  {
    requireExperience();
    auto record = core_plugin_->readScoringEpoch(epoch_id);
    if (record)
    {
      record->calibration_summary = fabric::server::feedback::combineCalibrationSummary(
          record->calibration_summary, core_plugin_->summarizePlanFeedbackCalibration(record->calibration_version, record->normalizer_version,
                                                                                      record->estimator_version, record->weight_version));
      record->selection_bias_summary = fabric::server::feedback::combineSelectionBiasSummary(
          record->selection_bias_summary, core_plugin_->summarizePlanFeedbackSelectionBias(record->calibration_version, record->normalizer_version,
                                                                                           record->estimator_version, record->weight_version));
    }
    return record;
  }

  /**
   * @brief Read an epoch unchanged for callers preparing a persistence update.
   * @param epoch_id Stored scoring epoch identity.
   * @return Persisted epoch, or nullopt when absent.
   * @throws std::runtime_error If the experience pipeline is unavailable.
   */
  std::optional<ScoringEpochRecord> readRawScoringEpoch(const std::string& epoch_id)
  {
    requireExperience();
    return core_plugin_->readScoringEpoch(epoch_id);
  }

  /**
   * @brief Persist a fully prepared scoring epoch record.
   * @param record Epoch metadata, including any fields retained by the caller.
   * @return Whether the core stored the record.
   * @throws std::runtime_error If the experience pipeline is unavailable.
   */
  bool recordScoringEpoch(const ScoringEpochRecord& record)
  {
    requireExperience();
    return core_plugin_->recordScoringEpoch(record);
  }

  /**
   * @brief Persist a fully prepared plan feedback record.
   * @param record Feedback, including any prior fields retained by the caller.
   * @return Whether the core stored the record.
   * @throws std::runtime_error If the experience pipeline is unavailable.
   */
  bool recordPlanFeedback(const PlanFeedbackRecord& record)
  {
    requireExperience();
    return core_plugin_->recordPlanFeedback(record);
  }

  /**
   * @brief Signal shutdown, join owned workers, and release runtime plugins and clients.
   * @note Call from an external owner thread, never a runtime callback. Repeated
   *       calls are no-ops; owners must coordinate shutdown with other API calls.
   */
  void shutdownRuntime()
  {
    shutdown();
  }

private:
  /** @brief Reject operations requiring an unavailable experience pipeline. */
  void requireExperience() const
  {
    if (!experienceReady())
    {
      throw std::runtime_error("Fabric experience runtime is unavailable");
    }
  }

  /** @brief Fill a missing planning request ID and refresh its recorded timestamp. */
  void stampPlanningRequest(PlanningRequest& request) const
  {
    const auto now = node_.now();
    if (request.planning_request_id.empty())
    {
      request.planning_request_id = fabric::server::feedback::generatePlanningRequestId(now);
    }
    request.recorded_at_sec = static_cast<int32_t>(now.seconds());
    request.recorded_at_nanosec = static_cast<uint32_t>(now.nanoseconds() % 1000000000ULL);
  }

  /** @brief Run direct or experience generation with cooperative cancellation and progress. */
  GenerationResult generatePlan(const GenerationRequest& request, const GenerationCallbacks& callbacks)
  {
    const auto cancelled = [&]() { return shutting_down_.load() || (callbacks.cancelled && callbacks.cancelled()); };
    const auto progress = [&](GenerationStage stage, const std::string& plan_id = std::string()) {
      if (callbacks.progress && !cancelled())
      {
        callbacks.progress(GenerationProgress{ stage, plan_id });
      }
    };
    GenerationResult result;
    progress(GenerationStage::Requested);
    if (cancelled())
    {
      result.state = GenerationResult::State::Cancelled;
      return result;
    }
    if (use_experience_)
    {
      progress(GenerationStage::InformationCollected);
      PlanningRequest planning_request;
      planning_request.task_description = request.task;
      planning_request.uuid = request.uuid;
      planning_request.flush = request.flush;
      planning_request.use_optimization = false;
      stampPlanningRequest(planning_request);
      auto results = generateAndValidateCandidatePlans(planning_request);
      for (auto& planning_result : results)
      {
        scorer_plugin_->scorePlanningResult(planning_request, planning_result);
      }
      fabric::server::feedback::persistGeneratedPlansArtifact(buildUserFeedbackContext(), planning_request, results);
      progress(GenerationStage::PlansScored);
      if (cancelled())
      {
        result.state = GenerationResult::State::Cancelled;
        return result;
      }
      auto& selected = results.back();
      if (promoteIntegratedSelectionForDispatch(planning_request, selected))
      {
        progress(GenerationStage::PlanSelected);
      }
      else
      {
        clearIntegratedSelection(selected, selected.selected_candidate_plan_id.empty() ?
                                               fabric::server::planning::summarizeSelectionFailure(selected) :
                                               "selected plan blocked by dispatch_terminal_success_only");
      }
      result.plan = selected.selected_plan;
      result.reasoning = selected.selected_reasoning;
      result.plan_id = selected.selected_candidate_plan_id;
    }
    else
    {
      auto plan = generation_plugin_->generate(request.task, request.uuid, request.flush);
      plan.plan_id = generate_uuid();
      plan.status = fabric::plan::Status::QUEUED;
      if (cancelled())
      {
        result.state = GenerationResult::State::Cancelled;
        return result;
      }
      if (request.auto_queue)
      {
        if (!parsing_plugin_->check_compatibility(plan))
        {
          throw std::runtime_error("Generated plan is not compatible with the loaded parser");
        }
        enqueuePlan(plan);
        start_execution_process_thread();
      }
      result.plan = plan.plan;
      result.reasoning = plan.reasoning;
      result.plan_id = plan.plan_id;
      progress(GenerationStage::Complete, plan.plan_id);
    }
    result.state = cancelled() ? GenerationResult::State::Cancelled : GenerationResult::State::Succeeded;
    return result;
  }

  /** @brief Deliver an identified plan's status synchronously to the adapter hook. */
  void publishPlanStatus(const fabric::plan::Plan& plan)
  {
    if (!plan.plan_id.empty() && status_callback_)
    {
      status_callback_(plan);
    }
  }

  /** @brief Perform the one-time worker and plugin shutdown sequence. */
  void shutdown()
  {
    // Begin shutdown sequence
    if (shutting_down_.exchange(true))
    {
      return;
    }
    {
      // Notify all waiting threads that the system is shutting down
      std::lock_guard<std::mutex> lock(system_status_mutex_);
      system_status_cv_.notify_all();
    }

    // Shutdown LLM analysis plugin if it exists
    if (llm_analysis_plugin_)
    {
      llm_analysis_plugin_->shutdown();
    }

    // Shutdown monitor plugins
    stopMonitorPlugins();

    // Mark the current plan as completed and notify all waiting threads
    {
      std::lock_guard<std::mutex> lock(plan_mutex_);
      plan_completed_ = true;
    }
    plan_cv_.notify_all();

    // Join startup thread to ensure clean shutdown
    if (startup_thread_.joinable())
    {
      startup_thread_.join();
    }

    // Join the process thread if it is running
    if (process_thread_.joinable())
    {
      process_thread_.join();
    }

    // Join the system status process thread if it is running
    if (system_status_process_thread_.joinable())
    {
      system_status_process_thread_.join();
    }

    // Join the monitor ingestion thread if it is running
    if (monitor_ingestion_thread_.joinable())
    {
      monitor_ingestion_thread_.join();
    }

    std::vector<GenerationWorker> workers;
    {
      std::lock_guard<std::mutex> lock(generation_workers_mutex_);
      workers.swap(generation_workers_);
    }
    for (auto& worker : workers)
    {
      worker.thread.join();
    }

    // Shutdown the core plugin if it exists
    if (core_plugin_)
    {
      core_plugin_->shutdown();
    }

    // Clear all cached clients and reset all plugin pointers
    bond_client_cache_.clear();
    capability_client_.reset();
    scorer_plugin_.reset();
    estimator_plugins_.clear();
    generation_validation_plugins_.clear();
    planner_plugin_.reset();
    core_plugin_.reset();
    monitor_plugins_.clear();
    generation_plugin_.reset();
    parsing_plugin_.reset();
    plan_validation_plugins_.clear();
    llm_analysis_plugin_.reset();
  }

  /** @brief Drain queued plans through parsing, validation, capability setup, and execution. */
  void execution_process()
  {
    // Check if the fabric is shutting down before processing the next plan
    while (!shutting_down_.load())
    {
      std::unique_lock<std::recursive_mutex> current_lock(current_plan_mutex_);
      // Reset per-run state before taking the next queued plan, ensuring a clean slate for processing.
      reset();

      if (!tryDequeueNextPlan(current_plan_))
      {
        break;
      }

      RCLCPP_INFO(node_.get_logger(), "[server] A new Fabric plan processing starting with id: %s", current_plan_.plan_id.c_str());

      if (current_plan_.status == fabric::plan::Status::CANCELLED)
      {
        RCLCPP_INFO(node_.get_logger(), "[server] Skipping cancelled plan with id: %s", current_plan_.plan_id.c_str());
        continue;
      }

      // Parse the plan into fabric::plan::Plan connections using the active parsing plugin.
      try
      {
        RCLCPP_INFO(node_.get_logger(), "[server] Parsing the fabric plan.");
        setCurrentPlanStatus(fabric::plan::Status::PARSING);

        parsing_plugin_->parsePlan(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(node_.get_logger(), "[server] Fabric plan parsing failed with error: %s", e.what());
        setCurrentPlanStatus(fabric::plan::Status::PARSE_FAILED);
        continue;
      }
      RCLCPP_INFO(node_.get_logger(), "[server] Fabric plan parsing completed successfully.");

      // get the capabilities available in the system
      try
      {
        RCLCPP_INFO(node_.get_logger(), "[server] Getting capabilities available in the system");
        setCurrentPlanStatus(fabric::plan::Status::VALIDATING);

        capability_list_.clear();
        capability_client_->getRunnableSpecs(capability_list_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(node_.get_logger(), "[server] Capability information retrieval failed with error: %s", e.what());
        setCurrentPlanStatus(fabric::plan::Status::VALIDATION_FAILED);
        continue;
      }

      RCLCPP_INFO(node_.get_logger(), "[server] Capability information retrieval completed successfully.");

      // validate the plan for compatibility based on the capabilities available in the system
      try
      {
        RCLCPP_INFO(node_.get_logger(), "[server] Validating the fabric plan for compatibility.");

        for (const auto& validation_plugin : plan_validation_plugins_)
        {
          validation_plugin->validate(current_plan_, capability_list_);
        }
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(node_.get_logger(), "[server] Compatibility validation failed with error: %s", e.what());
        setCurrentPlanStatus(fabric::plan::Status::VALIDATION_FAILED);
        continue;
      }

      // Request bond from capabilities2 server
      try
      {
        RCLCPP_INFO(node_.get_logger(), "[server] Requesting bond from capabilities2 server.");
        setCurrentPlanStatus(fabric::plan::Status::BONDING);
        current_plan_.bond_id = capability_client_->request_bond();
        publishPlanStatus(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(node_.get_logger(), "[server] Capability bond request failed with error: %s", e.what());
        setCurrentPlanStatus(fabric::plan::Status::BOND_FAILED);
        continue;
      }

      // Start new bond client for the new bond id
      RCLCPP_INFO(node_.get_logger(), "[server] Establishing bond with id : %s", current_plan_.bond_id.c_str());
      bond_client_cache_[current_plan_.bond_id] = std::make_unique<BondClient>(node_.shared_from_this(), current_plan_.bond_id);
      bond_client_cache_[current_plan_.bond_id]->start();

      // Remove old bonds if any
      if (bond_client_cache_.size() > 1)
      {
        for (auto& [old_bond_id, bond_client] : bond_client_cache_)
        {
          if (old_bond_id != current_plan_.bond_id)
          {
            bond_client->stop();
            RCLCPP_INFO(node_.get_logger(), "[server] Stopping and removing old bond with id : %s", old_bond_id.c_str());
          }
        }
      }

      RCLCPP_INFO(node_.get_logger(), "[server] Bond established with id : %s", current_plan_.bond_id.c_str());

      // Request use of capabilities for the plan
      try
      {
        RCLCPP_INFO(node_.get_logger(), "[server] Requesting use of capabilities for the plan.");
        setCurrentPlanStatus(fabric::plan::Status::CAPABILITY_STARTING);

        capability_client_->use_capabilities(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(node_.get_logger(), "[server] Capability usage failed with error: %s", e.what());
        setCurrentPlanStatus(fabric::plan::Status::CAPABILITY_START_FAILED);

        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      // connect the capabilities as per the plan
      try
      {
        RCLCPP_INFO(node_.get_logger(), "[server] Connecting capabilities as per the plan.");
        setCurrentPlanStatus(fabric::plan::Status::CAPABILITY_CONNECTING);

        capability_client_->connect_capabilities(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(node_.get_logger(), "[server] Capability connection failed with error: %s", e.what());
        setCurrentPlanStatus(fabric::plan::Status::CAPABILITY_CONNECT_FAILED);

        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      RCLCPP_INFO(node_.get_logger(), "[server] Capabilities connected successfully.");

      // trigger the first capability in the plan
      try
      {
        RCLCPP_INFO(node_.get_logger(), "[server] Triggering the first capability in the plan.");
        setCurrentPlanStatus(fabric::plan::Status::RUNNING);

        capability_client_->trigger_first_node(current_plan_);
      }
      catch (const fabric::fabric_exception& e)
      {
        RCLCPP_ERROR(node_.get_logger(), "[server] Capability trigger failed with error: %s", e.what());
      }

      RCLCPP_INFO(node_.get_logger(), "[server] First capability triggered successfully.");

      // wait for the plan to complete
      current_lock.unlock();
      {
        std::unique_lock<std::mutex> lock(plan_mutex_);
        plan_cv_.wait(lock, [this]() { return plan_completed_ || shutting_down_.load(); });
      }
      current_lock.lock();

      // Check if the server is shutting down before proceeding with the plan.
      if (shutting_down_.load())
      {
        return;
      }

      // Check if the current plan has been cancelled before proceeding with the plan completion.
      if (current_plan_.status == fabric::plan::Status::CANCELLED)
      {
        RCLCPP_INFO(node_.get_logger(), "[server] Fabric plan cancelled. Releasing capabilities.");
        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      setCurrentPlanStatus(fabric::plan::Status::COMPLETED);
      RCLCPP_INFO(node_.get_logger(), "[server] Fabric processing completed. Waiting for next plan.");
    }
  }

  /** @brief Wait for capability readiness, initialize optional experience, and enable execution. */
  void wait_for_capabilities_and_experience()
  {
    try
    {
      capability_client_->wait_until_ready();
      capabilities_ready_.store(true);

      // If experience is enabled, initialize the Fabric experience runtime.
      if (use_experience_)
      {
        // Initialize the Fabric experience runtime before proceeding.
        initializeFabricExperienceRuntime();

        RCLCPP_INFO(node_.get_logger(), "[server] Capabilities2 is ready. Integrated Fabric experience services are available.");
        experience_ready_.store(true);

        // Start the system status processing thread.
        system_status_process_thread_ = std::thread(&FabricRuntime::processingSystemStatus, this);
      }

      // Mark the startup as complete after capabilities and experience initialization.
      startup_complete_.store(true);

      RCLCPP_INFO(node_.get_logger(), "[server] Capabilities2 is ready. Fabric processing is enabled.");

      // Start the main execution process thread if not already running.
      start_execution_process_thread();
    }
    catch (const std::exception& ex)
    {
      if (!shutting_down_.load())
      {
        RCLCPP_ERROR(node_.get_logger(), "[server] Waiting for capabilities2 readiness failed: %s", ex.what());
      }
    }
  }

  /** @brief Declare and read execution, experience, and telemetry-processing parameters. */
  void loadRuntimeParameters()
  {
    const std::string default_plan_file_path = ament_index_cpp::get_package_share_directory("fabric_server") + "/plans/default.xml";

    node_.declare_parameter("plan_file_path", default_plan_file_path);
    plan_file_path_ = node_.get_parameter("plan_file_path").as_string();

    node_.declare_parameter("experience.enabled", false);
    use_experience_ = node_.get_parameter("experience.enabled").as_bool();

    node_.declare_parameter<bool>("experience.rebuild", false);
    rebuild_experience_ = node_.get_parameter("experience.rebuild").as_bool();

    node_.declare_parameter<bool>("experience.consume_feedback", false);
    consume_feedback_ = node_.get_parameter("experience.consume_feedback").as_bool();

    node_.declare_parameter<std::string>("experience.user_feedback_root_dir", "");
    user_feedback_root_dir_ = node_.get_parameter("experience.user_feedback_root_dir").as_string();

    node_.declare_parameter<bool>("experience.dispatch_terminal_success_only", false);
    dispatch_terminal_success_only_ = node_.get_parameter("experience.dispatch_terminal_success_only").as_bool();

    node_.declare_parameter<int>("telemetry.retention_seconds", 300);
    const int telemetry_retention_seconds = node_.get_parameter("telemetry.retention_seconds").as_int();
    telemetry_retention_seconds_ = telemetry_retention_seconds > 0 ? telemetry_retention_seconds : 300;
    telemetry_aggregator_.configureRetentionSeconds(telemetry_retention_seconds_);

    node_.declare_parameter<double>("telemetry.monitor_poll_frequency_hz", 2.0);
    monitor_poll_frequency_hz_ = node_.get_parameter("telemetry.monitor_poll_frequency_hz").as_double();
  }

  /** @brief Load the ordered validation plugins used before execution. */
  void initializePlanValidationPlugins()
  {
    node_.declare_parameter<std::vector<std::string>>("validation_plugins", std::vector<std::string>{
                                                                                "fabric::ParameterValidation",
                                                                            });
    std::vector<std::string> validation_plugin_names = node_.get_parameter("validation_plugins").as_string_array();
    if (validation_plugin_names.empty())
    {
      throw std::runtime_error("Parameter 'validation_plugins' must contain at least one validation plugin");
    }

    for (const auto& validation_plugin_name : validation_plugin_names)
    {
      RCLCPP_INFO(node_.get_logger(), "[server] Loading validation plugin: %s", validation_plugin_name.c_str());

      auto validation_plugin = plan_validation_loader_.createSharedInstance(validation_plugin_name);
      validation_plugin->initialize(node_.shared_from_this());
      plan_validation_plugins_.push_back(validation_plugin);

      RCLCPP_INFO(node_.get_logger(), "[server] Initialized validation plugin: %s", validation_plugin_name.c_str());
    }
  }

  /** @brief Load and initialize the configured internal plan parser. */
  void initializeParsingPlugin()
  {
    node_.declare_parameter("parsing_plugin", "fabric::XMLParser");
    const std::string parsing_plugin_name = node_.get_parameter("parsing_plugin").as_string();

    RCLCPP_INFO(node_.get_logger(), "[server] Loading parsing plugin: %s", parsing_plugin_name.c_str());

    parsing_plugin_ = parsing_loader_.createSharedInstance(parsing_plugin_name);
    parsing_plugin_->initialize(node_.shared_from_this());

    RCLCPP_INFO(node_.get_logger(), "[server] Initialized parsing plugin: %s", parsing_plugin_name.c_str());
  }

  /** @brief Load the direct-generation plugin and provide its analysis dependency. */
  void initializeGenerationPlugin()
  {
    node_.declare_parameter("generation_plugin", "fabric::PromptToolsGenerator");
    const std::string generation_plugin_name = node_.get_parameter("generation_plugin").as_string();

    RCLCPP_INFO(node_.get_logger(), "[server] Loading generation plugin: %s", generation_plugin_name.c_str());

    generation_plugin_ = generation_loader_.createSharedInstance(generation_plugin_name);
    generation_plugin_->configureAnalysis(llm_analysis_plugin_);
    generation_plugin_->initialize(node_.shared_from_this());

    RCLCPP_INFO(node_.get_logger(), "[server] Initialized generation plugin: %s", generation_plugin_name.c_str());
  }

  /** @brief Load the analysis plugin shared by generation, monitors, and experience. */
  void initializeLlmAnalysisPlugin()
  {
    node_.declare_parameter("llm_analysis_plugin", "fabric::PromptToolsLlmAnalysis");
    const std::string llm_analysis_plugin_name = node_.get_parameter("llm_analysis_plugin").as_string();

    RCLCPP_INFO(node_.get_logger(), "[server] Loading llm analysis plugin: %s", llm_analysis_plugin_name.c_str());

    llm_analysis_plugin_ = llm_analysis_loader_.createSharedInstance(llm_analysis_plugin_name);
    llm_analysis_plugin_->initialize(node_.shared_from_this());

    RCLCPP_INFO(node_.get_logger(), "[server] Initialized llm analysis plugin: %s", llm_analysis_plugin_name.c_str());
  }

  /** @brief Load configured telemetry monitors without starting their collection loops. */
  void initializeMonitorPlugins()
  {
    node_.declare_parameter<std::vector<std::string>>("telemetry.monitor_plugins", std::vector<std::string>{});
    monitor_plugin_names_ = node_.get_parameter("telemetry.monitor_plugins").as_string_array();

    for (const auto& monitor_plugin_name : monitor_plugin_names_)
    {
      RCLCPP_INFO(node_.get_logger(), "[server] Loading monitor plugin: %s", monitor_plugin_name.c_str());

      auto monitor_plugin = monitor_loader_.createSharedInstance(monitor_plugin_name);
      monitor_plugin->configureAnalysis(llm_analysis_plugin_);
      monitor_plugin->initialize(node_.shared_from_this());
      monitor_plugins_.push_back(monitor_plugin);

      RCLCPP_INFO(node_.get_logger(), "[server] Initialized monitor plugin: %s", monitor_plugin_name.c_str());
    }
  }

  /** @brief Load ordered candidate validators for the experience planning pipeline. */
  void initializeGenerationValidationPlugins()
  {
    node_.declare_parameter<std::vector<std::string>>(fabric::server::experience_runtime::validationParameterName(use_experience_),
                                                      std::vector<std::string>{ "fabric::ExtendedCompatibilityValidation",
                                                                                "fabric::DependencyValidation", "fabric::DataflowValidation",
                                                                                "fabric::CompatibilityValidation", "fabric::ParameterValidation" });
    node_.get_parameter(fabric::server::experience_runtime::validationParameterName(use_experience_), validation_plugin_names_);

    generation_validation_plugins_.clear();

    // Load and initialize each validation plugin specified in the configuration.
    for (const auto& plugin_name : validation_plugin_names_)
    {
      try
      {
        auto plugin = validation_loader_.createSharedInstance(plugin_name);
        plugin->initialize(node_.shared_from_this());
        generation_validation_plugins_.push_back(plugin);

        RCLCPP_INFO(node_.get_logger(), "Loaded validation plugin: %s", plugin_name.c_str());
      }
      catch (const pluginlib::PluginlibException& ex)
      {
        RCLCPP_WARN(node_.get_logger(), "Failed to load validation plugin '%s': %s", plugin_name.c_str(), ex.what());
      }
    }
  }

  /** @brief Resolve configured estimator types and initialize feature-producing plugins. */
  void initializeEstimatorPlugins()
  {
    const std::vector<std::string> default_estimators = { "fabric::SuccessProbabilityPlugin", "fabric::RuntimePlugin",  "fabric::EnergyPlugin",
                                                          "fabric::FaultBurdenPlugin",        "fabric::ResourcePlugin", "fabric::UncertaintyPlugin" };

    const std::vector<std::string> default_estimator_names = { "success", "time", "energy", "fault", "resource", "uncertainty" };

    const std::unordered_map<std::string, std::string> default_estimator_types = {
      { "success", "fabric::SuccessProbabilityPlugin" }, { "time", "fabric::RuntimePlugin" },      { "energy", "fabric::EnergyPlugin" },
      { "fault", "fabric::FaultBurdenPlugin" },          { "resource", "fabric::ResourcePlugin" }, { "uncertainty", "fabric::UncertaintyPlugin" },
    };

    node_.declare_parameter<std::vector<std::string>>(fabric::server::experience_runtime::parameterName("RapfsScorerPlugin.estimator_names"),
                                                      default_estimator_names);
    std::vector<std::string> configured_estimator_names;
    node_.get_parameter(fabric::server::experience_runtime::parameterName("RapfsScorerPlugin.estimator_names"), configured_estimator_names);

    // Determine the list of configured estimator plugins based on the configured estimator names.
    std::vector<std::string> configured_plugins;
    for (const auto& estimator_name : configured_estimator_names)
    {
      const auto default_type_it = default_estimator_types.find(estimator_name);
      const std::string default_type = default_type_it != default_estimator_types.end() ? default_type_it->second : std::string();
      const std::string type_key = fabric::server::experience_runtime::parameterName("RapfsScorerPlugin.estimators." + estimator_name + ".type");
      node_.declare_parameter<std::string>(type_key, default_type);
      const std::string plugin_type = node_.get_parameter(type_key).as_string();
      if (!plugin_type.empty())
      {
        configured_plugins.push_back(plugin_type);
      }
    }

    // Declare and get the list of estimator plugins from the parameter server.
    node_.declare_parameter<std::vector<std::string>>(fabric::server::experience_runtime::parameterName("estimator_plugins"), default_estimators);
    node_.get_parameter(fabric::server::experience_runtime::parameterName("estimator_plugins"), estimator_plugin_names_);

    if (!configured_plugins.empty())
    {
      estimator_plugin_names_ = configured_plugins;
    }

    // Load and initialize the estimator plugins.
    estimator_plugins_.clear();
    for (const auto& plugin_name : estimator_plugin_names_)
    {
      try
      {
        auto plugin = estimator_loader_.createSharedInstance(plugin_name);
        plugin->initialize(node_.shared_from_this());
        estimator_plugins_.push_back(plugin);
        RCLCPP_INFO(node_.get_logger(), "Loaded estimator plugin: %s", plugin_name.c_str());
      }
      catch (const pluginlib::PluginlibException& ex)
      {
        RCLCPP_WARN(node_.get_logger(), "Failed to load estimator plugin '%s': %s", plugin_name.c_str(), ex.what());
      }
    }
  }

  /** @brief Load the scorer and connect it to the core and active estimators. */
  void initializeScorerPlugin()
  {
    node_.declare_parameter<std::string>(fabric::server::experience_runtime::parameterName("scorer_plugin"), "fabric::RapfsScorerPlugin");
    node_.get_parameter(fabric::server::experience_runtime::parameterName("scorer_plugin"), scorer_plugin_name_);

    scorer_plugin_ = scorer_loader_.createSharedInstance(scorer_plugin_name_);
    scorer_plugin_->initialize(node_.shared_from_this());
    scorer_plugin_->configure(core_plugin_, &estimator_plugins_);
    RCLCPP_INFO(node_.get_logger(), "Loaded scorer plugin: %s", scorer_plugin_name_.c_str());
  }

  /** @brief Read retry limits and validation-finding policies for planning refinement. */
  void initializePlanningRefinementConfig()
  {
    const std::string planner_parameter_namespace = fabric::server::experience_runtime::pluginParameterNamespace(planner_plugin_name_);
    const std::string max_attempts_key = planner_parameter_namespace + ".refinement.max_attempts";
    const std::string retry_on_penalty_key = planner_parameter_namespace + ".refinement.retry_on_penalty_findings";
    const std::string retry_on_warning_key = planner_parameter_namespace + ".refinement.retry_on_warning_findings";

    node_.declare_parameter<int>(max_attempts_key, planning_refinement_max_attempts_);
    node_.declare_parameter<bool>(retry_on_penalty_key, planning_refinement_retry_on_penalty_findings_);
    node_.declare_parameter<bool>(retry_on_warning_key, planning_refinement_retry_on_warning_findings_);

    planning_refinement_max_attempts_ = std::max(0, static_cast<int>(node_.get_parameter(max_attempts_key).as_int()));
    planning_refinement_retry_on_penalty_findings_ = node_.get_parameter(retry_on_penalty_key).as_bool();
    planning_refinement_retry_on_warning_findings_ = node_.get_parameter(retry_on_warning_key).as_bool();
  }

  /** @brief Load and initialize the configured experience planner. */
  void initializePlannerPlugin()
  {
    node_.declare_parameter<std::string>(fabric::server::experience_runtime::parameterName("planner_plugin"), "fabric::PlanningPlugin");
    node_.get_parameter(fabric::server::experience_runtime::parameterName("planner_plugin"), planner_plugin_name_);

    planner_plugin_ = planner_loader_.createSharedInstance(planner_plugin_name_);
    planner_plugin_->initialize(node_.shared_from_this());
    RCLCPP_INFO(node_.get_logger(), "Loaded planner plugin: %s", planner_plugin_name_.c_str());
  }

  /** @brief Build or restore the experience core and initialize its planning/scoring pipeline. */
  void initializeFabricExperienceRuntime()
  {
    // If the experience runtime is not being used, skip initialization.
    if (!use_experience_)
    {
      return;
    }

    // Declare and retrieve the core plugin parameter.
    node_.declare_parameter("experience.core_plugin", "fabric::CoreGraphRag");
    core_plugin_name = node_.get_parameter("experience.core_plugin").as_string();

    RCLCPP_INFO(node_.get_logger(), "[server] Loading knowledge core plugin: %s", core_plugin_name.c_str());

    const bool should_attempt_persisted_load = !rebuild_experience_ && fabric::server::experience_runtime::hasPersistedCoreDb(core_db_path_);

    // Determine if we should attempt to load the persisted core database.
    if (rebuild_experience_)
    {
      fabric::server::experience_runtime::removePersistedCoreDbIfSupported(node_.get_logger(), core_plugin_name, core_db_path_);
    }

    // Create and initialize the core plugin instance.
    core_plugin_ = core_loader_.createSharedInstance(core_plugin_name);
    core_plugin_->configureAnalysis(llm_analysis_plugin_);
    core_plugin_->initialize(node_.shared_from_this());

    RCLCPP_INFO(node_.get_logger(), "[server] Initialized knowledge core plugin: %s", core_plugin_name.c_str());

    bool loaded_from_db = false;

    // Attempt to load the core plugin from the persisted database if available.
    if (should_attempt_persisted_load)
    {
      RCLCPP_INFO(node_.get_logger(), "Reusing persisted core database; skipping startup capability metadata collection.");
      try
      {
        RCLCPP_INFO(node_.get_logger(), "Attempting to load core plugin from persisted database.");

        core_plugin_->load(capabilities_);
        loaded_from_db = true;

        RCLCPP_INFO(node_.get_logger(), "Core plugin loaded from persisted database.");
      }
      catch (const std::exception& ex)
      {
        RCLCPP_WARN(node_.get_logger(), "Failed to load core plugin from persisted database: %s", ex.what());
      }
    }

    // If the core plugin was not loaded from the persisted database, build it with the collected capabilities.
    if (!loaded_from_db)
    {
      RCLCPP_INFO(node_.get_logger(), "Core plugin not loaded from persisted database; collecting capabilities and building plugin.");

      collect_capabilities_if_needed();
      core_plugin_->build(capabilities_);

      RCLCPP_INFO(node_.get_logger(), "Core plugin built with capability information.");
    }

    // Load and initialize the remaining plugins and configurations.
    initializePlannerPlugin();
    initializeGenerationValidationPlugins();
    initializeEstimatorPlugins();
    initializeScorerPlugin();
    initializePlanningRefinementConfig();

    // Consume any persisted user feedback to initialize the feedback context.
    fabric::server::feedback::consumePersistedUserFeedback(buildUserFeedbackContext());
  }

  /** @brief Create the ROS-backed client used for capability discovery and execution. */
  void initializeCapabilityClient()
  {
    capability_client_ = std::make_shared<CapabilityClient>();
    capability_client_->initialize(node_.shared_from_this());
  }

  /** @brief Load the configured starter plan, assign an execution ID, and enqueue it. */
  void queueStarterPlan()
  {
    // Load the starter plan from the specified plan file.
    fabric::plan::Plan starter_plan;
    if (!parsing_plugin_->load_file(plan_file_path_, starter_plan))
    {
      RCLCPP_ERROR(node_.get_logger(), "[server] Failed to load default plan from file: %s", plan_file_path_.c_str());
      throw fabric::fabric_exception("Failed to load default plan");
    }

    // Assign a unique plan ID and set the initial status to QUEUED before enqueuing the plan.
    starter_plan.plan_id = generate_uuid();
    starter_plan.status = fabric::plan::Status::QUEUED;

    // Enqueue the starter plan for execution.
    enqueuePlan(starter_plan);
  }

  /** @brief Start telemetry monitors and the startup and monitor-ingestion workers. */
  void startBackgroundThreads()
  {
    // Start the monitor plugins and background threads for telemetry processing.
    startMonitorPlugins();

    // Start the background thread for handling startup tasks.
    startup_thread_ = std::thread(&FabricRuntime::wait_for_capabilities_and_experience, this);
    monitor_ingestion_thread_ = std::thread(&FabricRuntime::processMonitorTelemetry, this);
  }

  /** @brief Start each configured telemetry monitor. */
  void startMonitorPlugins()
  {
    // Iterate through all configured monitor plugins and start them.
    for (const auto& monitor_plugin : monitor_plugins_)
    {
      monitor_plugin->start();
      RCLCPP_INFO(node_.get_logger(), "[server] Started monitor plugin: %s", monitor_plugin->getName().c_str());
    }
  }

  /** @brief Stop monitors in reverse order, logging individual shutdown failures. */
  void stopMonitorPlugins()
  {
    // Iterate through all configured monitor plugins in reverse order and stop them.
    for (auto it = monitor_plugins_.rbegin(); it != monitor_plugins_.rend(); ++it)
    {
      try
      {
        // Stop the current monitor plugin.
        (*it)->stop();
        RCLCPP_INFO(node_.get_logger(), "[server] Stopped monitor plugin: %s", (*it)->getName().c_str());
      }
      catch (const std::exception& ex)
      {
        RCLCPP_WARN(node_.get_logger(), "[server] Failed to stop monitor plugin %s: %s", (*it)->getName().c_str(), ex.what());
      }
    }
  }

  /**
   * @brief Route typed monitor output to telemetry aggregation and experience feedback.
   * @param monitor_name Source monitor identity used in aggregated records.
   * @param data Battery, resource, component-status, or system-status payload.
   * @throws std::runtime_error If the payload type is unsupported.
   */
  void ingestMonitorPayload(const std::string& monitor_name, const std::any& data)
  {
    // Handle telemetry data based on its type.
    if (const auto* battery = std::any_cast<fabric::monitor::BatteryData>(&data))
    {
      // Update the battery percentage from the telemetry data.
      battery_percentage_ = battery->percentage;
      for (const auto& warning : battery->warnings)
      {
        // Remember each warning from the battery telemetry data.
        telemetry_aggregator_.rememberTelemetryRecord(
            TelemetryRecord{
                warning.timestamp,
                "battery",
                warning.description,
                fabric::server::telemetry::labelForMonitoringStatus(warning.status),
            },
            node_.now().nanoseconds());
      }
      return;
    }

    // Handle resource snapshot telemetry data.
    if (const auto* snapshots = std::any_cast<std::vector<capabilities2_msgs::msg::LaunchResourceList>>(&data))
    {
      telemetry_aggregator_.ingestResourceSnapshots(*snapshots);
      return;
    }

    // Handle monitor status telemetry data.
    try
    {
      telemetry_aggregator_.ingestMonitorStatuses(monitor_name, std::any_cast<std::map<std::string, fabric::ComponentStatus>>(data),
                                                  node_.now().nanoseconds());
      return;
    }
    catch (const std::bad_any_cast&)
    {
    }

    // Handle system status telemetry data.
    try
    {
      // Ingest the system status telemetry data and update the capability statuses.
      auto capability_statuses = telemetry_aggregator_.ingestSystemStatuses(std::any_cast<std::vector<SystemStatus>>(data), node_.now().nanoseconds(),
                                                                            battery_percentage_ >= 0.0, battery_percentage_);
      if (use_experience_ && !capability_statuses.empty())
      {
        // Enqueue each capability status for further processing.
        for (auto& status : capability_statuses)
        {
          fabric::server::status::enqueueSystemStatus(system_status_mutex_, system_status_cv_, system_status_records_, std::move(status));
        }
      }
      return;
    }
    catch (const std::bad_any_cast&)
    {
    }

    throw std::runtime_error("unsupported telemetry payload");
  }

  /** @brief Poll monitors and ingest their data at the configured frequency until shutdown. */
  void processMonitorTelemetry()
  {
    // Determine the safe frequency for processing monitor telemetry data.
    const double safe_frequency = monitor_poll_frequency_hz_ > 0.0 ? monitor_poll_frequency_hz_ : 2.0;
    const auto period = std::chrono::milliseconds(static_cast<int>(1000.0 / safe_frequency));

    while (!shutting_down_.load() && rclcpp::ok())
    {
      // Iterate over all monitor plugins and ingest their telemetry data.
      for (const auto& monitor_plugin : monitor_plugins_)
      {
        try
        {
          // Ingest the telemetry payload from the monitor plugin.
          ingestMonitorPayload(monitor_plugin->getName(), monitor_plugin->getData());
        }
        catch (const std::exception& ex)
        {
          // Log a warning if telemetry ingestion fails for the monitor plugin.
          if (!shutting_down_.load())
          {
            RCLCPP_WARN(node_.get_logger(), "[server] Monitor plugin %s telemetry ingestion failed: %s", monitor_plugin->getName().c_str(),
                        ex.what());
          }
        }
      }

      // Sleep for the determined period before processing the next batch of telemetry data.
      std::this_thread::sleep_for(period);
    }
  }

  /** @brief Start a single execution worker when readiness and queued work permit. */
  void start_execution_process_thread()
  {
    // Check if the capabilities are ready and the server is not shutting down.
    if (!capabilities_ready_.load() || shutting_down_.load())
    {
      return;
    }

    // If experience is enabled and not ready, do not start the execution process thread.
    if (use_experience_ && !experience_ready_.load())
    {
      return;
    }

    // Check if there are any queued plans to process.
    if (!hasQueuedPlans())
    {
      return;
    }

    // Acquire the lock for the process thread to ensure only one thread is running at a time.
    std::lock_guard<std::mutex> process_lock(process_thread_mutex_);
    if (process_thread_running_.load())
    {
      return;
    }

    // If the process thread is joinable, join it before starting a new one.
    if (process_thread_.joinable())
    {
      process_thread_.join();
    }

    // Start the execution process thread.
    process_thread_running_.store(true);
    process_thread_ = std::thread([this]() {
      execution_process();
      process_thread_running_.store(false);
    });
  }

  /** @brief Bind persistence callbacks and configuration for user-feedback helpers. */
  fabric::server::feedback::UserFeedbackContext buildUserFeedbackContext()
  {
    // Construct and return the user feedback context with the necessary dependencies and callbacks.
    return fabric::server::feedback::UserFeedbackContext{
      node_.get_logger(),
      user_feedback_root_dir_,
      &user_feedback_file_mutex_,
      consume_feedback_,
      {
          [this]() { return node_.now(); },
          [this](const fabric::server::feedback::PlanNodeScoreExplanationQuery& query) {
            return core_plugin_->readSelectedPlanScoreExplanationBundle(query);
          },
          [this](const fabric::server::feedback::PlanFeedbackRecord& feedback) { return core_plugin_->recordPlanFeedback(feedback); },
          [this](const fabric::server::feedback::CandidatePlanSkillAttribution& attribution) {
            return core_plugin_->recordCandidatePlanSkillAttribution(attribution);
          },
          [this](const fabric::server::feedback::ScoringEpochRecord& record) { return core_plugin_->recordScoringEpoch(record); },
      }
    };
  }

  /** @brief Generate candidates and apply validation-driven refinement up to the retry limit. */
  std::vector<PlanningResult> generateAndValidateCandidatePlans(const PlanningRequest& request)
  {
    // Generate the initial planning result using the planner plugin.
    PlanningResult planning_result = planner_plugin_->plan(request, capabilities_, *core_plugin_, *parsing_plugin_);
    std::vector<PlanningResult> generation_results;

    validatePlanningResult(request, planning_result);
    generation_results.push_back(planning_result);

    for (int refinement_attempt = 1; refinement_attempt <= planning_refinement_max_attempts_ && shouldRefinePlanningResult(planning_result);
         ++refinement_attempt)
    {
      const PlanningRefinementRequest refinement_request{
        request,
        fabric::server::planning::selectedOrFirstPlan(planning_result),
        fabric::server::planning::buildValidationCritique(planning_result),
        refinement_attempt,
      };
      planning_result = planner_plugin_->refinePlan(refinement_request, capabilities_, *core_plugin_, *parsing_plugin_);
      validatePlanningResult(request, planning_result);
      generation_results.push_back(planning_result);
    }

    return generation_results;
  }

  /** @brief Apply selection policy and persist attribution and pending execution-feedback state. */
  bool promoteIntegratedSelectionForDispatch(const PlanningRequest& request, PlanningResult& planning_result)
  {
    if (planning_result.selected_candidate_plan_id.empty())
    {
      return false;
    }

    if (dispatch_terminal_success_only_ && !selectedPlanCanDispatch(planning_result))
    {
      return false;
    }

    markSelectedCandidate(request, planning_result);
    queuePendingSelectedPlan(request, planning_result);
    fabric::server::feedback::persistSelectedPlanUserFeedback(buildUserFeedbackContext(), request, planning_result);
    return true;
  }

  /** @brief Clear selected plan data and retain the explanation for selection failure. */
  void clearIntegratedSelection(PlanningResult& planning_result, const std::string& selected_reasoning)
  {
    planning_result.selected_reasoning = selected_reasoning;
    planning_result.selected_candidate_plan_id.clear();
    planning_result.selected_plan.clear();
  }

  /** @brief Serialize queued execution evidence into the experience core and terminal feedback. */
  void processingSystemStatus()
  {
    RCLCPP_INFO(node_.get_logger(), "Fabric telemetry feedback processing started.");

    // Main processing loop
    while (rclcpp::ok())
    {
      SystemStatus status;

      if (!fabric::server::status::waitPopSystemStatus(system_status_mutex_, system_status_cv_, system_status_records_, shutting_down_, status))
      {
        return;
      }

      telemetry_aggregator_.maybeMergeExecutionStatus(status);
      core_plugin_->update(status);
      const auto shutdown_safety = fabric::server::status::assessShutdownSafety(status);
      RCLCPP_INFO(node_.get_logger(),
                  "Injected Fabric system status into Fabric scoring core: %s kill_state=%s active_capabilities=%zu incomplete_capabilities=%zu "
                  "reason=%s",
                  fabric::server::status::summarizeSystemStatus(status).c_str(), fabric::server::status::shutdownSafetyLabel(shutdown_safety.state),
                  shutdown_safety.active_capabilities, shutdown_safety.incomplete_capabilities, shutdown_safety.reason.c_str());
      persistTerminalPlanFeedback(status);
    }
    RCLCPP_INFO(node_.get_logger(), "Fabric telemetry feedback processing complete.");
  }

  /** @brief Append a plan under the queue lock and emit its initial status. */
  void enqueuePlan(const fabric::plan::Plan& plan)
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      plan_queue_.push_back(plan);
    }

    publishPlanStatus(plan);
  }

  /** @brief Check for queued execution work under the queue lock. */
  bool hasQueuedPlans()
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return !plan_queue_.empty();
  }

  /** @brief Copy and remove the next queued plan, returning false when the queue is empty. */
  bool tryDequeueNextPlan(fabric::plan::Plan& plan)
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (plan_queue_.empty())
    {
      return false;
    }

    plan = plan_queue_.front();
    plan_queue_.pop_front();
    return true;
  }

  /** @brief Mark a queued plan cancelled and return its bond ID if a matching plan exists. */
  bool tryCancelQueuedPlan(const std::string& plan_id, std::string& bond_id_to_cancel)
  {
    std::optional<fabric::plan::Plan> cancelled_plan;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      for (auto& plan : plan_queue_)
      {
        if (plan.plan_id == plan_id)
        {
          plan.status = fabric::plan::Status::CANCELLED;
          bond_id_to_cancel = plan.bond_id;
          cancelled_plan = plan;
          break;
        }
      }
    }

    if (cancelled_plan.has_value())
    {
      publishPlanStatus(*cancelled_plan);
      return true;
    }

    return false;
  }

  /** @brief Set the execution completion flag and wake the waiting execution worker. */
  void markPlanCompleted()
  {
    {
      std::lock_guard<std::mutex> lock(plan_mutex_);
      plan_completed_ = true;
    }
    plan_cv_.notify_all();
  }

  /** @brief Generate a random UUID string for an execution plan. */
  static std::string generate_uuid()
  {
    uuid_t uuid;
    uuid_generate_random(uuid);
    char uuid_str[40];
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);
  }

  /** @brief Clear per-execution state before dequeuing another plan. */
  void reset()
  {
    current_plan_ = fabric::plan::Plan();
    capability_list_.clear();
    bond_id_.clear();
    {
      std::lock_guard<std::mutex> lock(plan_mutex_);
      plan_completed_ = false;
    }
  }

  /** @brief Update the current plan's status and synchronously notify the adapter. */
  void setCurrentPlanStatus(const fabric::plan::Status status)
  {
    current_plan_.status = status;
    publishPlanStatus(current_plan_);
  }

  /** @brief Run ordered candidate validators, stopping at each candidate's first rejection. */
  void validatePlanningResult(const PlanningRequest& request, PlanningResult& planning_result)
  {
    for (auto& candidate : planning_result.candidates)
    {
      candidate.validation = CandidateValidationSummary{};
      for (const auto& validation_plugin : generation_validation_plugins_)
      {
        validation_plugin->validate(request, capabilities_, candidate);
        if (candidate.validation.rejected)
        {
          break;
        }
      }
    }
  }

  /** @brief Populate the capability metadata cache once under its mutex. */
  void collect_capabilities_if_needed()
  {
    std::lock_guard<std::mutex> lock(capabilities_mutex_);
    if (!capabilities_.empty())
    {
      return;
    }

    std::vector<fabric::capability::Descriptor> capabilities;
    capability_client_->getRunnableSpecs(capabilities);
    capabilities_ = std::move(capabilities);
    RCLCPP_INFO(node_.get_logger(), "Received runnable capability metadata from capabilities2.");
  }

  /** @brief Evaluate whether validation findings satisfy the configured refinement policy. */
  bool shouldRefinePlanningResult(const PlanningResult& planning_result) const
  {
    return fabric::server::planning::shouldRefinePlanningResult(planning_result, planning_refinement_retry_on_penalty_findings_,
                                                                planning_refinement_retry_on_warning_findings_);
  }

  /** @brief Check whether the selected candidate satisfies terminal-success dispatch requirements. */
  bool selectedPlanCanDispatch(const PlanningResult& planning_result) const
  {
    return fabric::server::planning::selectedPlanCanDispatch(planning_result);
  }

  /** @brief Persist selected-candidate attribution and score explanation artifacts. */
  void markSelectedCandidate(const PlanningRequest& request, const PlanningResult& planning_result)
  {
    fabric::server::feedback::recordSelectedCandidateArtifacts(*core_plugin_, request, planning_result);
  }

  /** @brief Track a selected candidate until telemetry supplies its execution plan identity. */
  void queuePendingSelectedPlan(const PlanningRequest& request, const PlanningResult& planning_result)
  {
    fabric::server::planning::queuePendingSelectedPlan(planning_state_mutex_, pending_generated_plans_, request, planning_result);
  }

  /** @brief Parse supplied XML into a candidate with core-backed skill metadata for scoring. */
  CandidatePlan parseCandidatePlanForScoring(const PlanningRequest& request, const std::string& candidate_plan_id, const std::string& plan_xml) const
  {
    return fabric::server::planning::parseCandidatePlanForScoring(*core_plugin_, *parsing_plugin_, request, candidate_plan_id, plan_xml);
  }

  /** @brief Finalize terminal plan feedback once and release its aggregated execution evidence. */
  void persistTerminalPlanFeedback(const SystemStatus& status)
  {
    if (fabric::server::feedback::finalizeTerminalPlanFeedback(*core_plugin_, planning_state_mutex_, pending_generated_plans_,
                                                               finalized_plan_feedback_, status))
    {
      telemetry_aggregator_.eraseExecutionStatus(status.plan_id);
      telemetry_aggregator_.eraseCapabilityBatteryUsage(status.plan_id);
    }
  }

  /** @brief Joinable generation worker and its atomically published completion flag. */
  struct GenerationWorker
  {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> finished;
  };

  rclcpp::Node& node_;
  StatusCallback status_callback_;
  std::mutex generation_workers_mutex_;
  std::mutex generation_operation_mutex_;
  std::vector<GenerationWorker> generation_workers_;
  std::recursive_mutex current_plan_mutex_;
  /** Vector of plans */
  std::deque<fabric::plan::Plan> plan_queue_;

  /** Protect queued plan access across callbacks and processing thread */
  std::mutex queue_mutex_;

  /** Current plan being processed */
  fabric::plan::Plan current_plan_;

  /** Current plan related synchronization */
  bool plan_completed_{ false };
  std::mutex plan_mutex_;
  std::condition_variable plan_cv_;

  /** Deferred startup and process thread lifecycle */
  std::thread startup_thread_;
  std::thread process_thread_;
  std::mutex process_thread_mutex_;
  std::atomic<bool> capabilities_ready_{ false };
  std::atomic<bool> experience_ready_{ false };

  bool use_experience_{ false };
  bool rebuild_experience_{ false };

  std::atomic<bool> process_thread_running_{ false };
  std::atomic<bool> startup_complete_{ false };
  std::atomic<bool> shutting_down_{ false };

  /** Capability client to interact with capability server */
  std::shared_ptr<CapabilityClient> capability_client_;

  /** Capability List */
  std::vector<fabric::capability::Descriptor> capability_list_;

  /** Plugin loaders for each module package */
  pluginlib::ClassLoader<fabric::ValidationBase> plan_validation_loader_;
  pluginlib::ClassLoader<fabric::ParserBase> parsing_loader_;
  pluginlib::ClassLoader<fabric::GenerationBase> generation_loader_;
  pluginlib::ClassLoader<fabric::LlmAnalysisBase> llm_analysis_loader_;
  pluginlib::ClassLoader<fabric::MonitorBase> monitor_loader_;

  /** shared pointer for parsing plugin */
  std::shared_ptr<fabric::ParserBase> parsing_plugin_;

  /** ordered validation plugins applied to each parsed plan */
  std::vector<std::shared_ptr<fabric::ValidationBase>> plan_validation_plugins_;

  /** ordered validation plugins applied to each candidate plan generation*/
  std::vector<std::shared_ptr<fabric::ValidationBase>> generation_validation_plugins_;

  /** shared pointer for generation plugin */
  std::shared_ptr<fabric::GenerationBase> generation_plugin_;

  /** shared pointer for llm analysis plugin */
  std::shared_ptr<fabric::LlmAnalysisBase> llm_analysis_plugin_;

  /**
   * @brief Plugin loader for the core knowledge management plugin.
   */
  pluginlib::ClassLoader<fabric::CoreBase> core_loader_;

  /**
   * @brief Plugin loader for planner plugins.
   */
  pluginlib::ClassLoader<fabric::PlannerBase> planner_loader_;

  /**
   * @brief Plugin loader for candidate-plan scorer plugins.
   */
  pluginlib::ClassLoader<fabric::ScorerBase> scorer_loader_;

  /**
   * @brief Plugin loader for candidate-plan validation plugins.
   */
  pluginlib::ClassLoader<fabric::ValidationBase> validation_loader_;

  /**
   * @brief Plugin loader for capability feature estimators.
   */
  pluginlib::ClassLoader<fabric::EstimateBase> estimator_loader_;

  /**
   * @brief Shared pointer to the core knowledge management plugin.
   */
  std::shared_ptr<fabric::CoreBase> core_plugin_;
  std::vector<std::shared_ptr<fabric::MonitorBase>> monitor_plugins_;

  /**
   * @brief Shared pointer to the active planner plugin.
   */
  std::shared_ptr<fabric::PlannerBase> planner_plugin_;

  /**
   * @brief Shared pointer to the active candidate-plan scorer plugin.
   */
  std::shared_ptr<fabric::ScorerBase> scorer_plugin_;

  /**
   * @brief Loaded estimator plugins used to compute RAPFS node features.
   */
  std::vector<std::shared_ptr<fabric::EstimateBase>> estimator_plugins_;

  /**
   * @brief Name of the core plugin to load.
   */
  std::string core_plugin_name;
  std::vector<std::string> monitor_plugin_names_;

  /**
   * @brief Configured validation plugin class names.
   */
  std::vector<std::string> validation_plugin_names_;

  /**
   * @brief Name of the planner plugin to load.
   */
  std::string planner_plugin_name_;

  /**
   * @brief Name of the scorer plugin to load.
   */
  std::string scorer_plugin_name_;

  /**
   * @brief Names of estimator plugins requested by ROS parameter.
   */
  std::vector<std::string> estimator_plugin_names_;

  /** Bond id */
  std::string bond_id_;

  /** Manages bond between capabilities server and this client */
  std::map<std::string, std::shared_ptr<BondClient>> bond_client_cache_;

  /** File Path link */
  std::string plan_file_path_;

  /** Experience-side metadata cache and feedback synchronization. */
  std::mutex capabilities_mutex_;
  std::vector<fabric::capability::Descriptor> capabilities_;
  std::mutex planning_state_mutex_;
  std::vector<PendingGeneratedPlan> pending_generated_plans_;
  std::unordered_set<std::string> finalized_plan_feedback_;
  std::mutex user_feedback_file_mutex_;
  std::filesystem::path core_db_path_{ "core_graphrag.sqlite3" };
  int planning_refinement_max_attempts_{ 0 };
  bool planning_refinement_retry_on_penalty_findings_{ true };
  bool planning_refinement_retry_on_warning_findings_{ false };
  std::string user_feedback_root_dir_;
  bool consume_feedback_{ false };
  bool dispatch_terminal_success_only_{ false };
  double battery_percentage_{ -1.0 };
  double monitor_poll_frequency_hz_{ 2.0 };
  int telemetry_retention_seconds_{ 300 };
  fabric::server::telemetry::Aggregator telemetry_aggregator_;

  /** Fabric-owned runtime telemetry publication and internal feedback queue. */
  std::thread system_status_process_thread_;
  std::thread monitor_ingestion_thread_;
  std::mutex system_status_mutex_;
  std::condition_variable system_status_cv_;
  std::deque<SystemStatus> system_status_records_;
};

}  // namespace fabric
