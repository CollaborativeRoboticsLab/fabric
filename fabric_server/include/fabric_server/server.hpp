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
#include "rclcpp_action/rclcpp_action.hpp"
#include <pluginlib/class_loader.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
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
#include <fabric_server/utils/score_services.hpp>
#include <fabric_server/utils/score_explanation_adapter.hpp>
#include <fabric_server/utils/status.hpp>
#include <fabric_server/utils/telemetry.hpp>

#include <fabric_msgs/srv/submit_fabric_plan.hpp>
#include <fabric_msgs/srv/cancel_fabric_plan.hpp>
#include <fabric_msgs/srv/complete_fabric.hpp>
#include <fabric_msgs/srv/get_plan_status.hpp>
#include <fabric_msgs/srv/parse_plan.hpp>
#include <fabric_msgs/srv/get_scoring_epoch.hpp>
#include <fabric_msgs/srv/get_plan_score_explanation.hpp>
#include <fabric_msgs/srv/record_plan_feedback.hpp>
#include <fabric_msgs/srv/record_scoring_epoch.hpp>
#include <fabric_msgs/srv/score_plan.hpp>

#include <fabric_msgs/msg/fabric_status.hpp>

#include <fabric_msgs/action/generate_plan.hpp>

namespace fabric
{

/**
 * @brief Class representing the main server of the fabric.
 *
 */
class Fabric : public rclcpp::Node
{
public:
  using SubmitFabricPlan = fabric_msgs::srv::SubmitFabricPlan;
  using CancelFabricPlan = fabric_msgs::srv::CancelFabricPlan;
  using CompleteFabric = fabric_msgs::srv::CompleteFabric;
  using GetPlanStatus = fabric_msgs::srv::GetPlanStatus;
  using ParsePlan = fabric_msgs::srv::ParsePlan;
  using GetScoringEpoch = fabric_msgs::srv::GetScoringEpoch;
  using GetPlanScoreExplanation = fabric_msgs::srv::GetPlanScoreExplanation;
  using RecordPlanFeedback = fabric_msgs::srv::RecordPlanFeedback;
  using RecordScoringEpoch = fabric_msgs::srv::RecordScoringEpoch;
  using ScorePlan = fabric_msgs::srv::ScorePlan;
  using GeneratePlan = fabric_msgs::action::GeneratePlan;
  using GoalHandleGeneratePlan = rclcpp_action::ServerGoalHandle<GeneratePlan>;
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
  using BatteryState = sensor_msgs::msg::BatteryState;

  struct IntegratedGenerationRun
  {
    PlanningRequest request;
    std::vector<PlanningResult> generation_results;
    PlanningResult selected_result;
  };

  using TelemetryRecord = fabric::server::status::TelemetryRecord;

  /**
   * @brief Construct a new Fabric object
   *
   * @param options Node options for the Fabric node
   */
  Fabric(const rclcpp::NodeOptions & options = rclcpp::NodeOptions())
  : Node("Fabric", options)
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
    try {
      // Only call setup if this object is already owned by a shared_ptr
      if (shared_from_this()) {
        initialize();
      }
    } catch (const std::bad_weak_ptr &) {
      // Not yet safe — probably standalone without make_shared
    }
  }

  ~Fabric()
  {
    shutdownRuntime();
  }

  void shutdownRuntime()
  {
    if (shutting_down_.exchange(true)) {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(system_status_mutex_);
      system_status_cv_.notify_all();
    }
    if (llm_analysis_plugin_) {
      llm_analysis_plugin_->shutdown();
    }
    stopMonitorPlugins();

    {
      std::lock_guard<std::mutex> lock(plan_mutex_);
      plan_completed_ = true;
    }
    plan_cv_.notify_all();

    if (startup_thread_.joinable()) {
      startup_thread_.join();
    }

    if (process_thread_.joinable()) {
      process_thread_.join();
    }

    if (system_status_process_thread_.joinable()) {
      system_status_process_thread_.join();
    }

    if (monitor_ingestion_thread_.joinable()) {
      monitor_ingestion_thread_.join();
    }

    if (core_plugin_) {
      core_plugin_->shutdown();
    }

    bond_client_cache_.clear();
    capability_client_.reset();
    scorer_plugin_.reset();
    estimator_plugins_.clear();
    validation_plugins_.clear();
    planner_plugin_.reset();
    core_plugin_.reset();
    monitor_plugins_.clear();
    generation_plugin_.reset();
    parsing_plugin_.reset();
    plan_validation_plugins_.clear();
    llm_analysis_plugin_.reset();
  }

  /**
   * @brief Initialize the Fabric node, action server and event client.
   *
   */
  void initialize()
  {
    loadRuntimeParameters();
    createFabricServices();
    createFabricExperienceServices();
    createTelemetryInterfaces();
    createGeneratePlanAction();
    initializeBootstrapValidationPlugins();
    initializeParsingPlugin();
    initializeLlmAnalysisPlugin();
    initializeMonitorPlugins();
    initializeGenerationPlugin();
    initializeCapabilityClient();
    queueStarterPlan();

    RCLCPP_INFO(this->get_logger(),
        "[server] Fabric node initialized. Waiting for capabilities2 readiness.");
    startBackgroundThreads();
  }

protected:
  /**
   * @brief The main process thread of the fabric.
   *
   */
  void process()
  {
    while (!shutting_down_.load()) {
      // Reset per-run state before taking the next queued plan.
      reset();

      if (!tryDequeueNextPlan(current_plan_)) {
        break;
      }

      RCLCPP_INFO(this->get_logger(), "[server] A new Fabric plan processing starting");

      if (current_plan_.status == fabric::plan::Status::CANCELLED) {
        RCLCPP_INFO(this->get_logger(), "[server] Skipping cancelled plan with id: %s",
            current_plan_.plan_id.c_str());
        continue;
      }

      // Parse the plan into fabric::plan::Plan connections using the active parsing plugin.
      try {
        RCLCPP_INFO(this->get_logger(), "[server] Parsing the fabric plan.");
        setCurrentPlanStatus(fabric::plan::Status::PARSING);

        parsing_plugin_->parsePlan(current_plan_);
      } catch (const fabric::fabric_exception & e) {
        RCLCPP_ERROR(this->get_logger(), "[server] Fabric plan parsing failed with error: %s",
            e.what());
        setCurrentPlanStatus(fabric::plan::Status::PARSE_FAILED);
        continue;
      }
      RCLCPP_INFO(this->get_logger(), "[server] Fabric plan parsing completed successfully.");

      // get the capabilities available in the system
      try {
        RCLCPP_INFO(this->get_logger(), "[server] Getting capabilities available in the system");
        setCurrentPlanStatus(fabric::plan::Status::VALIDATING);

        capability_client_->getInterfaces(capability_list_);
        capability_client_->getSemanticInterfaces(capability_list_);
        capability_client_->getProviders(capability_list_);
        capability_client_->getRunnableSpecs(capability_list_);
      } catch (const fabric::fabric_exception & e) {
        RCLCPP_ERROR(this->get_logger(),
            "[server] Capability information retrieval failed with error: %s", e.what());
        setCurrentPlanStatus(fabric::plan::Status::VALIDATION_FAILED);
        continue;
      }

      RCLCPP_INFO(this->get_logger(),
          "[server] Capability information retrieval completed successfully.");

      // validate the plan for compatibility based on the capabilities available in the system
      try {
        RCLCPP_INFO(this->get_logger(), "[server] Validating the fabric plan for compatibility.");

        for (const auto & validation_plugin : plan_validation_plugins_) {
          validation_plugin->validate(current_plan_, capability_list_);
        }
      } catch (const fabric::fabric_exception & e) {
        RCLCPP_ERROR(this->get_logger(), "[server] Compatibility validation failed with error: %s",
            e.what());
        setCurrentPlanStatus(fabric::plan::Status::VALIDATION_FAILED);
        continue;
      }

      // Request bond from capabilities2 server
      try {
        RCLCPP_INFO(this->get_logger(), "[server] Requesting bond from capabilities2 server.");
        setCurrentPlanStatus(fabric::plan::Status::BONDING);

        current_plan_.bond_id = capability_client_->request_bond();
        publishPlanStatus(current_plan_);
      } catch (const fabric::fabric_exception & e) {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability bond request failed with error: %s",
            e.what());
        setCurrentPlanStatus(fabric::plan::Status::BOND_FAILED);
        continue;
      }

      // Start new bond client for the new bond id
      RCLCPP_INFO(this->get_logger(), "[server] Establishing bond with id : %s",
          current_plan_.bond_id.c_str());
      bond_client_cache_[current_plan_.bond_id] = std::make_unique<BondClient>(shared_from_this(),
          current_plan_.bond_id);
      bond_client_cache_[current_plan_.bond_id]->start();

      // Remove old bonds if any
      if (bond_client_cache_.size() > 1) {
        for (auto & [old_bond_id, bond_client] : bond_client_cache_) {
          if (old_bond_id != current_plan_.bond_id) {
            bond_client->stop();
            RCLCPP_INFO(this->get_logger(), "[server] Stopping and removing old bond with id : %s",
                old_bond_id.c_str());
          }
        }
      }

      RCLCPP_INFO(this->get_logger(), "[server] Bond established with id : %s",
          current_plan_.bond_id.c_str());

      // Request use of capabilities for the plan
      try {
        RCLCPP_INFO(this->get_logger(), "[server] Requesting use of capabilities for the plan.");
        setCurrentPlanStatus(fabric::plan::Status::CAPABILITY_STARTING);

        capability_client_->use_capabilities(current_plan_);
      } catch (const fabric::fabric_exception & e) {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability usage failed with error: %s",
            e.what());
        setCurrentPlanStatus(fabric::plan::Status::CAPABILITY_START_FAILED);

        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      // connect the capabilities as per the plan
      try {
        RCLCPP_INFO(this->get_logger(), "[server] Connecting capabilities as per the plan.");
        setCurrentPlanStatus(fabric::plan::Status::CAPABILITY_CONNECTING);

        capability_client_->connect_capabilities(current_plan_);
      } catch (const fabric::fabric_exception & e) {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability connection failed with error: %s",
            e.what());
        setCurrentPlanStatus(fabric::plan::Status::CAPABILITY_CONNECT_FAILED);

        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      RCLCPP_INFO(this->get_logger(), "[server] Capabilities connected successfully.");

      // trigger the first capability in the plan
      try {
        RCLCPP_INFO(this->get_logger(), "[server] Triggering the first capability in the plan.");
        setCurrentPlanStatus(fabric::plan::Status::RUNNING);

        capability_client_->trigger_first_node(current_plan_);
      } catch (const fabric::fabric_exception & e) {
        RCLCPP_ERROR(this->get_logger(), "[server] Capability trigger failed with error: %s",
            e.what());
      }

      RCLCPP_INFO(this->get_logger(), "[server] First capability triggered successfully.");

      // wait for the plan to complete
      {
        std::unique_lock<std::mutex> lock(plan_mutex_);
        plan_cv_.wait(lock, [this]() {return plan_completed_ || shutting_down_.load();});
      }

      if (shutting_down_.load()) {
        return;
      }

      if (current_plan_.status == fabric::plan::Status::CANCELLED) {
        RCLCPP_INFO(this->get_logger(), "[server] Fabric plan cancelled. Releasing capabilities.");
        capability_client_->free_capabilities(current_plan_);
        continue;
      }

      setCurrentPlanStatus(fabric::plan::Status::COMPLETED);
      RCLCPP_INFO(this->get_logger(),
          "[server] Fabric processing completed. Waiting for next plan.");
    }
  }

  void wait_for_capabilities_ready()
  {
    try {
      capability_client_->wait_until_ready();
      capabilities_ready_.store(true);
      if (use_experience_) {
        initializeFabricExperienceRuntime();
        RCLCPP_INFO(this->get_logger(),
            "[server] Capabilities2 is ready. Integrated Fabric experience services are available.");
        experience_ready_.store(true);
        system_status_process_thread_ = std::thread(&Fabric::processingSystemStatus, this);
      }

      startup_complete_.store(true);

      RCLCPP_INFO(this->get_logger(),
          "[server] Capabilities2 is ready. Fabric processing is enabled.");
      maybe_start_process_thread();
    } catch (const std::exception & ex) {
      if (!shutting_down_.load()) {
        RCLCPP_ERROR(this->get_logger(), "[server] Waiting for capabilities2 readiness failed: %s",
            ex.what());
      }
    }
  }

  void loadRuntimeParameters()
  {
    const std::string default_plan_file_path =
      ament_index_cpp::get_package_share_directory("fabric_server") + "/plans/default.xml";

    this->declare_parameter("plan_file_path", default_plan_file_path);
    plan_file_path_ = this->get_parameter("plan_file_path").as_string();

    this->declare_parameter("experience.enabled", false);
    use_experience_ = this->get_parameter("experience.enabled").as_bool();

    this->declare_parameter<bool>("experience.rebuild", false);
    rebuild_experience_ = this->get_parameter("experience.rebuild").as_bool();

    this->declare_parameter<bool>("experience.consume_feedback", false);
    consume_feedback_ = this->get_parameter("experience.consume_feedback").as_bool();

    this->declare_parameter<std::string>("experience.user_feedback_root_dir", "");
    user_feedback_root_dir_ = this->get_parameter("experience.user_feedback_root_dir").as_string();

    this->declare_parameter<bool>("experience.dispatch_terminal_success_only", false);
    dispatch_terminal_success_only_ =
      this->get_parameter("experience.dispatch_terminal_success_only").as_bool();

    this->declare_parameter<std::string>("telemetry.fabric_status_topic", "/fabric/status");
    fabric_status_topic_ = this->get_parameter("telemetry.fabric_status_topic").as_string();

    this->declare_parameter<int>("telemetry.retention_seconds", 300);
    const int telemetry_retention_seconds =
      this->get_parameter("telemetry.retention_seconds").as_int();
    telemetry_retention_seconds_ = telemetry_retention_seconds >
      0 ? telemetry_retention_seconds : 300;
    telemetry_aggregator_.configureRetentionSeconds(telemetry_retention_seconds_);

    this->declare_parameter<double>("telemetry.monitor_poll_frequency_hz", 2.0);
    monitor_poll_frequency_hz_ = this->get_parameter(
      "telemetry.monitor_poll_frequency_hz").as_double();

    this->declare_parameter<bool>("telemetry.enable_battery_monitoring", true);
    enable_battery_monitoring_ = this->get_parameter(
      "telemetry.enable_battery_monitoring").as_bool();

    this->declare_parameter<std::string>("telemetry.battery_topic", "/battery_state");
    battery_topic_ = this->get_parameter("telemetry.battery_topic").as_string();
  }

  void createTelemetryInterfaces()
  {
    fabric_status_pub_ = this->create_publisher<fabric_msgs::msg::FabricStatus>(
      fabric_status_topic_, rclcpp::QoS(rclcpp::KeepLast(10)).transient_local().reliable());

    if (enable_battery_monitoring_) {
      const auto qos = rclcpp::QoS(rclcpp::KeepLast(10)).best_effort();
      battery_sub_ = this->create_subscription<BatteryState>(
        battery_topic_, qos, std::bind(&Fabric::batteryCallback, this, std::placeholders::_1));
    }
  }

  void createFabricServices()
  {
    submit_plan_server_ = this->create_service<SubmitFabricPlan>(
        "/fabric/plan/submit",
        std::bind(&Fabric::submitPlanCallback, this, std::placeholders::_1, std::placeholders::_2));

    cancel_server_ = this->create_service<CancelFabricPlan>(
        "/fabric/plan/cancel",
        std::bind(&Fabric::cancelPlanCallback, this, std::placeholders::_1, std::placeholders::_2));

    completion_server_ = this->create_service<CompleteFabric>(
        "/fabric/plan/set_completion",
        std::bind(&Fabric::setCompleteCallback, this, std::placeholders::_1,
        std::placeholders::_2));

    get_plan_status_server_ = this->create_service<GetPlanStatus>(
        "/fabric/plan/get_status",
        std::bind(&Fabric::getPlanStatusCallback, this, std::placeholders::_1,
        std::placeholders::_2));

    parse_plan_server_ = this->create_service<ParsePlan>("/fabric/plan/parse",
                                                         std::bind(&Fabric::parsePlanCallback, this,
        std::placeholders::_1, std::placeholders::_2));
  }

  void createFabricExperienceServices()
  {
    get_plan_score_explanation_service_ = this->create_service<GetPlanScoreExplanation>(
        "/fabric/experience/score_explanation",
        std::bind(&Fabric::handleGetPlanScoreExplanation, this, std::placeholders::_1,
        std::placeholders::_2));

    score_plan_service_ = this->create_service<ScorePlan>("/fabric/experience/score",
                                                          std::bind(&Fabric::handleScorePlan, this,
        std::placeholders::_1, std::placeholders::_2));

    get_scoring_epoch_service_ = this->create_service<GetScoringEpoch>(
        "/fabric/experience/get_scoring_epoch",
        std::bind(&Fabric::handleGetScoringEpoch, this, std::placeholders::_1,
        std::placeholders::_2));

    record_plan_feedback_service_ = this->create_service<RecordPlanFeedback>(
        "/fabric/experience/record_plan_feedback",
        std::bind(&Fabric::handleRecordPlanFeedback, this, std::placeholders::_1,
        std::placeholders::_2));

    record_scoring_epoch_service_ = this->create_service<RecordScoringEpoch>(
        "/fabric/experience/record_scoring_epoch",
        std::bind(&Fabric::handleRecordScoringEpoch, this, std::placeholders::_1,
        std::placeholders::_2));
  }

  void createGeneratePlanAction()
  {
    generate_plan_server_ = rclcpp_action::create_server<GeneratePlan>(
        shared_from_this(), "/fabric/plan/generate",
        std::bind(&Fabric::handleGeneratePlanGoal, this, std::placeholders::_1,
        std::placeholders::_2),
        std::bind(&Fabric::handleGeneratePlanCancel, this, std::placeholders::_1),
        std::bind(&Fabric::handleGeneratePlanAccepted, this, std::placeholders::_1));
  }

  void initializeBootstrapValidationPlugins()
  {
    this->declare_parameter<std::vector<std::string>>("validation_plugins",
        std::vector<std::string>{
        "fabric::CompatibilityValidation",
        "fabric::ParameterValidation",
                                                                            });
    std::vector<std::string> validation_plugin_names =
      this->get_parameter("validation_plugins").as_string_array();
    if (validation_plugin_names.empty()) {
      throw std::runtime_error(
          "Parameter 'validation_plugins' must contain at least one validation plugin");
    }

    for (const auto & validation_plugin_name : validation_plugin_names) {
      RCLCPP_INFO(this->get_logger(), "[server] Loading validation plugin: %s",
          validation_plugin_name.c_str());

      auto validation_plugin = plan_validation_loader_.createSharedInstance(validation_plugin_name);
      validation_plugin->initialize(shared_from_this());
      plan_validation_plugins_.push_back(validation_plugin);

      RCLCPP_INFO(this->get_logger(), "[server] Initialized validation plugin: %s",
          validation_plugin_name.c_str());
    }
  }

  void initializeParsingPlugin()
  {
    this->declare_parameter("parsing_plugin", "fabric::XMLParser");
    const std::string parsing_plugin_name = this->get_parameter("parsing_plugin").as_string();

    RCLCPP_INFO(this->get_logger(), "[server] Loading parsing plugin: %s",
        parsing_plugin_name.c_str());

    parsing_plugin_ = parsing_loader_.createSharedInstance(parsing_plugin_name);
    parsing_plugin_->initialize(shared_from_this());

    RCLCPP_INFO(this->get_logger(), "[server] Initialized parsing plugin: %s",
        parsing_plugin_name.c_str());
  }

  void initializeGenerationPlugin()
  {
    this->declare_parameter("generation_plugin", "fabric::PromptToolsGenerator");
    const std::string generation_plugin_name = this->get_parameter("generation_plugin").as_string();

    RCLCPP_INFO(this->get_logger(), "[server] Loading generation plugin: %s",
        generation_plugin_name.c_str());

    generation_plugin_ = generation_loader_.createSharedInstance(generation_plugin_name);
    generation_plugin_->configureAnalysis(llm_analysis_plugin_);
    generation_plugin_->initialize(shared_from_this());

    RCLCPP_INFO(this->get_logger(), "[server] Initialized generation plugin: %s",
        generation_plugin_name.c_str());
  }

  void initializeLlmAnalysisPlugin()
  {
    this->declare_parameter("llm_analysis_plugin", "fabric::PromptToolsLlmAnalysis");
    const std::string llm_analysis_plugin_name =
      this->get_parameter("llm_analysis_plugin").as_string();

    RCLCPP_INFO(this->get_logger(), "[server] Loading llm analysis plugin: %s",
        llm_analysis_plugin_name.c_str());

    llm_analysis_plugin_ = llm_analysis_loader_.createSharedInstance(llm_analysis_plugin_name);
    llm_analysis_plugin_->initialize(shared_from_this());

    RCLCPP_INFO(this->get_logger(), "[server] Initialized llm analysis plugin: %s",
        llm_analysis_plugin_name.c_str());
  }

  void initializeMonitorPlugins()
  {
    this->declare_parameter<std::vector<std::string>>(
      "telemetry.monitor_plugins", std::vector<std::string>{});
    monitor_plugin_names_ = this->get_parameter("telemetry.monitor_plugins").as_string_array();

    for (const auto & monitor_plugin_name : monitor_plugin_names_) {
      RCLCPP_INFO(this->get_logger(), "[server] Loading monitor plugin: %s",
        monitor_plugin_name.c_str());

      auto monitor_plugin = monitor_loader_.createSharedInstance(monitor_plugin_name);
      monitor_plugin->configureAnalysis(llm_analysis_plugin_);
      monitor_plugin->initialize(shared_from_this());
      monitor_plugins_.push_back(monitor_plugin);

      RCLCPP_INFO(this->get_logger(), "[server] Initialized monitor plugin: %s",
        monitor_plugin_name.c_str());
    }
  }

  void initializeFabricExperienceRuntime()
  {
    if (!use_experience_) {
      return;
    }

    this->declare_parameter("experience.core_plugin", "fabric::CoreGraphRag");
    core_plugin_name = this->get_parameter("experience.core_plugin").as_string();

    RCLCPP_INFO(this->get_logger(), "[server] Loading knowledge core plugin: %s",
        core_plugin_name.c_str());

    const bool should_attempt_persisted_load = !rebuild_experience_ &&
      fabric::server::experience_runtime::hasPersistedCoreDb(core_db_path_);

    if (rebuild_experience_) {
      fabric::server::experience_runtime::removePersistedCoreDbIfSupported(
        this->get_logger(), core_plugin_name, core_db_path_);
    }

    core_plugin_ = core_loader_.createSharedInstance(core_plugin_name);
    core_plugin_->configureAnalysis(llm_analysis_plugin_);
    core_plugin_->initialize(shared_from_this());

    RCLCPP_INFO(this->get_logger(), "[server] Initialized knowledge core plugin: %s",
        core_plugin_name.c_str());

    bool loaded_from_db = false;
    if (should_attempt_persisted_load) {
      RCLCPP_INFO(this->get_logger(),
          "Reusing persisted core database; skipping startup capability metadata collection.");
      try {
        core_plugin_->load(capabilities_);
        loaded_from_db = true;
        RCLCPP_INFO(this->get_logger(), "Core plugin loaded from persisted database.");
      } catch (const std::exception & ex) {
        RCLCPP_WARN(this->get_logger(), "Failed to load core plugin from persisted database: %s",
            ex.what());
      }
    }

    if (!loaded_from_db) {
      collect_capabilities_if_needed();
      core_plugin_->build(capabilities_);
      RCLCPP_INFO(this->get_logger(), "Core plugin built with capability information.");
    }

    loadPlannerPlugin();
    loadValidationPlugins();
    loadPlanningRefinementConfig();
    loadEstimatorPlugins();
    loadScorerPlugin();
    fabric::server::feedback::consumePersistedUserFeedback(buildUserFeedbackContext());
  }

  void initializeCapabilityClient()
  {
    capability_client_ = std::make_shared<CapabilityClient>();
    capability_client_->initialize(this->shared_from_this());
  }

  void queueStarterPlan()
  {
    fabric::plan::Plan starter_plan;
    if (!parsing_plugin_->load_file(plan_file_path_, starter_plan)) {
      RCLCPP_ERROR(this->get_logger(), "[server] Failed to load default plan from file: %s",
          plan_file_path_.c_str());
      throw fabric::fabric_exception("Failed to load default plan");
    }
    starter_plan.plan_id = generate_uuid();
    starter_plan.status = fabric::plan::Status::QUEUED;

    enqueuePlan(starter_plan);
  }

  void startBackgroundThreads()
  {
    startMonitorPlugins();
    startup_thread_ = std::thread(&Fabric::wait_for_capabilities_ready, this);
    monitor_ingestion_thread_ = std::thread(&Fabric::processMonitorTelemetry, this);
  }

  void startMonitorPlugins()
  {
    for (const auto & monitor_plugin : monitor_plugins_) {
      monitor_plugin->start();
      RCLCPP_INFO(this->get_logger(), "[server] Started monitor plugin: %s",
        monitor_plugin->getName().c_str());
    }
  }

  void stopMonitorPlugins()
  {
    for (auto it = monitor_plugins_.rbegin(); it != monitor_plugins_.rend(); ++it) {
      try {
        (*it)->stop();
        RCLCPP_INFO(this->get_logger(), "[server] Stopped monitor plugin: %s",
          (*it)->getName().c_str());
      } catch (const std::exception & ex) {
        RCLCPP_WARN(this->get_logger(), "[server] Failed to stop monitor plugin %s: %s",
          (*it)->getName().c_str(), ex.what());
      }
    }
  }

  void ingestMonitorPayload(const std::string & monitor_name, const std::any & data)
  {
    if (const auto * snapshots =
      std::any_cast<std::vector<capabilities2_msgs::msg::LaunchResourceList>>(&data))
    {
      telemetry_aggregator_.ingestResourceSnapshots(*snapshots);
      return;
    }

    try {
      telemetry_aggregator_.ingestMonitorStatuses(
        monitor_name,
        std::any_cast<std::map<std::string, fabric::ComponentStatus>>(data),
        this->now().nanoseconds());
      return;
    } catch (const std::bad_any_cast &) {
    }

    try {
      BatteryState::SharedPtr battery_snapshot;
      if (enable_battery_monitoring_) {
        std::lock_guard<std::mutex> battery_lock(battery_mutex_);
        battery_snapshot = last_battery_msg_;
      }

      auto capability_statuses = telemetry_aggregator_.ingestSystemStatuses(
        std::any_cast<std::vector<SystemStatus>>(data),
        this->now().nanoseconds(),
        enable_battery_monitoring_,
        battery_snapshot ? battery_snapshot->percentage : -1.0);
      if (use_experience_ && !capability_statuses.empty()) {
        for (auto & status : capability_statuses) {
          fabric::server::status::enqueueSystemStatus(
            system_status_mutex_, system_status_cv_, system_status_records_, std::move(status));
        }
      }
      return;
    } catch (const std::bad_any_cast &) {
    }

    throw std::runtime_error("unsupported telemetry payload");
  }

  void processMonitorTelemetry()
  {
    const double safe_frequency = monitor_poll_frequency_hz_ > 0.0 ? monitor_poll_frequency_hz_ :
      2.0;
    const auto period = std::chrono::milliseconds(static_cast<int>(1000.0 / safe_frequency));

    while (!shutting_down_.load() && rclcpp::ok()) {
      for (const auto & monitor_plugin : monitor_plugins_) {
        try {
          ingestMonitorPayload(monitor_plugin->getName(), monitor_plugin->getData());
        } catch (const std::exception & ex) {
          if (!shutting_down_.load()) {
            RCLCPP_WARN(this->get_logger(),
              "[server] Monitor plugin %s telemetry ingestion failed: %s",
              monitor_plugin->getName().c_str(), ex.what());
          }
        }
      }

      std::this_thread::sleep_for(period);
    }
  }

  void maybe_start_process_thread()
  {
    if (!capabilities_ready_.load() || shutting_down_.load()) {
      return;
    }

    if (use_experience_ && !experience_ready_.load()) {
      return;
    }

    if (!hasQueuedPlans()) {
      return;
    }

    std::lock_guard<std::mutex> process_lock(process_thread_mutex_);
    if (process_thread_running_.load()) {
      return;
    }

    if (process_thread_.joinable()) {
      process_thread_.join();
    }

    process_thread_running_.store(true);
    process_thread_ = std::thread([this]() {
          process();
          process_thread_running_.store(false);
    });
  }

  fabric::server::feedback::UserFeedbackContext buildUserFeedbackContext()
  {
    return fabric::server::feedback::UserFeedbackContext{
      this->get_logger(),
      user_feedback_root_dir_,
      &user_feedback_file_mutex_,
      consume_feedback_,
      {
        [this]() {return this->now();},
        [this](const fabric::server::feedback::PlanNodeScoreExplanationQuery & query) {
          return core_plugin_->readSelectedPlanScoreExplanationBundle(query);
        },
        [this](const fabric::server::feedback::PlanFeedbackRecord & feedback) {
          return core_plugin_->recordPlanFeedback(feedback);
        },
        [this](const fabric::server::feedback::CandidatePlanSkillAttribution & attribution) {
          return core_plugin_->recordCandidatePlanSkillAttribution(attribution);
        },
        [this](const fabric::server::feedback::ScoringEpochRecord & record) {
          return core_plugin_->recordScoringEpoch(record);
        },
      }
    };
  }

  /**
   * @brief Callback function to set a new fabric plan.
   */
  rclcpp_action::GoalResponse handleGeneratePlanGoal(
    const rclcpp_action::GoalUUID & uuid,
    std::shared_ptr<const GeneratePlan::Goal> goal)
  {
    (void)uuid;

    if (!startup_complete_.load()) {
      RCLCPP_WARN(this->get_logger(), "[server] Rejecting generation request before startup completes");
      return rclcpp_action::GoalResponse::REJECT;
    }

    if (goal->task.empty()) {
      RCLCPP_WARN(this->get_logger(), "[server] Rejecting generation request with empty task");
      return rclcpp_action::GoalResponse::REJECT;
    }

    RCLCPP_INFO(this->get_logger(), "[server] Accepted generation request");
    return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
  }

  rclcpp_action::CancelResponse handleGeneratePlanCancel(
    const std::shared_ptr<GoalHandleGeneratePlan> goal_handle)
  {
    (void)goal_handle;
    RCLCPP_INFO(this->get_logger(), "[server] Generation cancellation requested");
    return rclcpp_action::CancelResponse::ACCEPT;
  }

  void handleGeneratePlanAccepted(const std::shared_ptr<GoalHandleGeneratePlan> goal_handle)
  {
    std::thread{std::bind(&Fabric::executePlanGeneration, this, std::placeholders::_1),
      goal_handle}.detach();
  }

  void executePlanGeneration(const std::shared_ptr<GoalHandleGeneratePlan> goal_handle)
  {
    const auto goal = goal_handle->get_goal();
    auto feedback = std::make_shared<GeneratePlan::Feedback>();
    auto result = std::make_shared<GeneratePlan::Result>();

    // Update feedback to indicate that plan generation has been requested
    publishGenerationFeedback(goal_handle, feedback,
        fabric_msgs::msg::FabricStatus::GENERATION_REQUESTED);

    // Check if the goal has been canceled before proceeding
    if (goal_handle->is_canceling()) {
      goal_handle->canceled(result);
      return;
    }

    if (use_experience_) {
      handleIntegratedPlanGeneration(goal_handle, goal, feedback);
      return;
    }
    handleDirectPlanGeneration(goal_handle, goal, feedback, result);
  }

  void publishGenerationFeedback(
    const std::shared_ptr<GoalHandleGeneratePlan> & goal_handle,
    const std::shared_ptr<GeneratePlan::Feedback> & feedback,
    const uint8_t status_code, const std::string & plan_id = std::string())
  {
    feedback->status.code = status_code;
    feedback->status.plan_id = plan_id;
    goal_handle->publish_feedback(feedback);
  }

  PlanningRequest buildPlanningRequest(
    const std::shared_ptr<const GeneratePlan::Goal> & goal,
    const rclcpp::Time & now) const
  {
    PlanningRequest request;
    request.planning_request_id = fabric::server::feedback::generatePlanningRequestId(now);
    request.task_description = goal->task;
    request.uuid = goal->uuid;
    request.use_optimization = false;
    request.flush = goal->flush;
    request.recorded_at_sec = static_cast<int32_t>(now.seconds());
    request.recorded_at_nanosec = now.nanoseconds() % 1000000000ULL;
    return request;
  }

  std::vector<PlanningResult> generateAndValidateCandidatePlans(const PlanningRequest & request)
  {
    PlanningResult planning_result = planner_plugin_->plan(request, capabilities_, *core_plugin_,
      *parsing_plugin_);
    std::vector<PlanningResult> generation_results;

    validatePlanningResult(request, planning_result);
    generation_results.push_back(planning_result);

    for (int refinement_attempt = 1;
      refinement_attempt <= planning_refinement_max_attempts_ &&
      shouldRefinePlanningResult(planning_result);
      ++refinement_attempt)
    {
      const PlanningRefinementRequest refinement_request{
        request,
        fabric::server::planning::selectedOrFirstPlan(planning_result),
        fabric::server::planning::buildValidationCritique(planning_result),
        refinement_attempt,
      };
      planning_result = planner_plugin_->refinePlan(refinement_request, capabilities_,
          *core_plugin_, *parsing_plugin_);
      validatePlanningResult(request, planning_result);
      generation_results.push_back(planning_result);
    }

    return generation_results;
  }

  IntegratedGenerationRun runIntegratedGeneration(
    const std::shared_ptr<const GeneratePlan::Goal> & goal)
  {
    const auto now = this->now();
    IntegratedGenerationRun run;
    run.request = buildPlanningRequest(goal, now);
    run.generation_results = generateAndValidateCandidatePlans(run.request);
    for (auto & generation_result : run.generation_results) {
      scorer_plugin_->scorePlanningResult(run.request, generation_result);
    }

    fabric::server::feedback::persistGeneratedPlansArtifact(
      buildUserFeedbackContext(),
      run.request,
      run.generation_results);
    run.selected_result = run.generation_results.back();
    return run;
  }

  bool promoteIntegratedSelectionForDispatch(
    const PlanningRequest & request,
    PlanningResult & planning_result)
  {
    if (planning_result.selected_candidate_plan_id.empty()) {
      return false;
    }

    if (dispatch_terminal_success_only_ && !selectedPlanCanDispatch(planning_result)) {
      return false;
    }

    markSelectedCandidate(request, planning_result);
    queuePendingSelectedPlan(request, planning_result);
    fabric::server::feedback::persistSelectedPlanUserFeedback(
      buildUserFeedbackContext(),
      request,
      planning_result);
    return true;
  }

  void clearIntegratedSelection(
    PlanningResult & planning_result,
    const std::string & selected_reasoning)
  {
    planning_result.selected_reasoning = selected_reasoning;
    planning_result.selected_candidate_plan_id.clear();
    planning_result.selected_plan.clear();
  }

  void finalizeIntegratedPlanSelection(
    const std::shared_ptr<GoalHandleGeneratePlan> & goal_handle,
    const std::shared_ptr<GeneratePlan::Feedback> & feedback, const PlanningRequest & request,
    PlanningResult & planning_result)
  {
    if (promoteIntegratedSelectionForDispatch(request, planning_result)) {
      publishGenerationFeedback(goal_handle, feedback,
          fabric_msgs::msg::FabricStatus::PLAN_SELECTED);
      return;
    }

    clearIntegratedSelection(
      planning_result,
      planning_result.selected_candidate_plan_id.empty() ?
        fabric::server::planning::summarizeSelectionFailure(planning_result) :
        "selected plan blocked by dispatch_terminal_success_only");
  }

  void handleIntegratedPlanGeneration(
    const std::shared_ptr<GoalHandleGeneratePlan> & goal_handle,
    const std::shared_ptr<const GeneratePlan::Goal> & goal,
    const std::shared_ptr<GeneratePlan::Feedback> & feedback)
  {
    publishGenerationFeedback(goal_handle, feedback,
        fabric_msgs::msg::FabricStatus::INFORMATION_COLLECTED);

    auto run = runIntegratedGeneration(goal);

    publishGenerationFeedback(goal_handle, feedback, fabric_msgs::msg::FabricStatus::PLANS_SCORED);

    finalizeIntegratedPlanSelection(goal_handle, feedback, run.request, run.selected_result);

    completeIntegratedPlanGeneration(goal_handle, goal, run.selected_result);
  }

  void completeIntegratedPlanGeneration(
    const std::shared_ptr<GoalHandleGeneratePlan> & goal_handle,
    const std::shared_ptr<const GeneratePlan::Goal> & goal,
    const PlanningResult & planning_result)
  {
    auto result = std::make_shared<GeneratePlan::Result>();
    result->header = goal->header;
    result->plan = planning_result.selected_plan;
    result->reasoning = planning_result.selected_reasoning;
    result->plan_id = planning_result.selected_candidate_plan_id;
    goal_handle->succeed(result);
  }

  bool queueGeneratedPlanIfRequested(
    const std::shared_ptr<const GeneratePlan::Goal> & goal,
    const fabric::plan::Plan & generated_plan,
    const std::shared_ptr<GoalHandleGeneratePlan> & goal_handle,
    const std::shared_ptr<GeneratePlan::Result> & result)
  {
    if (!goal->auto_queue) {
      return true;
    }

    if (!parsing_plugin_->check_compatibility(generated_plan)) {
      RCLCPP_ERROR(this->get_logger(),
          "[server] Generated plan is not compatible with the loaded parser");
      goal_handle->abort(result);
      return false;
    }

    enqueuePlan(generated_plan);
    maybe_start_process_thread();
    RCLCPP_INFO(this->get_logger(), "[server] Generated plan queued with id: %s",
        generated_plan.plan_id.c_str());
    return true;
  }

  void handleDirectPlanGeneration(
    const std::shared_ptr<GoalHandleGeneratePlan> & goal_handle,
    const std::shared_ptr<const GeneratePlan::Goal> & goal,
    const std::shared_ptr<GeneratePlan::Feedback> & feedback,
    const std::shared_ptr<GeneratePlan::Result> & result)
  {
    fabric::plan::Plan generated_plan;
    try {
      generated_plan = generation_plugin_->generate(goal->task, goal->uuid, goal->flush);
      generated_plan.plan_id = generate_uuid();
      generated_plan.status = fabric::plan::Status::QUEUED;
    } catch (const std::exception & e) {
      RCLCPP_ERROR(this->get_logger(), "[server] Plan generation failed with error: %s", e.what());
      goal_handle->abort(result);
      return;
    }

    if (goal_handle->is_canceling()) {
      goal_handle->canceled(result);
      return;
    }

    if (!queueGeneratedPlanIfRequested(goal, generated_plan, goal_handle, result)) {
      return;
    }

    publishGenerationFeedback(goal_handle, feedback,
        fabric_msgs::msg::FabricStatus::GENERATION_COMPLETE, generated_plan.plan_id);

    result->plan = generated_plan.plan;
    result->reasoning = generated_plan.reasoning;
    result->plan_id = generated_plan.plan_id;
    goal_handle->succeed(result);
  }

  void submitPlanCallback(
    const std::shared_ptr<SubmitFabricPlan::Request> request,
    std::shared_ptr<SubmitFabricPlan::Response> response)
  {
    RCLCPP_INFO(this->get_logger(),
        "[server] Received the request with a plan and extended metadata");

    fabric::plan::Plan new_plan;
    new_plan.plan = request->plan;
    new_plan.plan_id = generate_uuid();
    new_plan.status = fabric::plan::Status::QUEUED;
    new_plan.metadata.planning_request_id = request->metadata.planning_request_id;
    new_plan.metadata.candidate_plan_id = request->metadata.candidate_plan_id;
    new_plan.metadata.graph_hash = request->metadata.graph_hash;

    if (!parsing_plugin_->check_compatibility(new_plan)) {
      RCLCPP_ERROR(this->get_logger(),
          "[server] Plan received via submit request not compatible with the loaded parser.");
      response->plan_id = "";
      response->error = "[server] Plan is not compatible with the loaded parser.";
      return;
    }

    enqueuePlan(new_plan);
    maybe_start_process_thread();
    response->plan_id = new_plan.plan_id;
    response->error.clear();
  }

  void parsePlanCallback(
    const std::shared_ptr<ParsePlan::Request> request,
    std::shared_ptr<ParsePlan::Response> response)
  {
    fabric::plan::Plan plan;
    plan.plan = request->plan;
    plan.plan_id = "fabric_parse_request";

    try {
      parsing_plugin_->parsePlan(plan);
      response->success = true;
      response->error.clear();
      for (const auto & [connection_id, connection] : plan.connections) {
        (void)connection_id;
        fabric_msgs::msg::FabricConnection message;
        message.source.interface = connection.source.interface;
        message.source.provider = connection.source.provider;
        message.source.instance_id = connection.source.instance_id;
        message.on_start.interface = connection.on_start.interface;
        message.on_start.provider = connection.on_start.provider;
        message.on_start.instance_id = connection.on_start.instance_id;
        message.on_stop.interface = connection.on_stop.interface;
        message.on_stop.provider = connection.on_stop.provider;
        message.on_stop.instance_id = connection.on_stop.instance_id;
        message.on_success.interface = connection.on_success.interface;
        message.on_success.provider = connection.on_success.provider;
        message.on_success.instance_id = connection.on_success.instance_id;
        message.on_failure.interface = connection.on_failure.interface;
        message.on_failure.provider = connection.on_failure.provider;
        message.on_failure.instance_id = connection.on_failure.instance_id;
        message.description = connection.description;
        response->connections.push_back(message);
      }
    } catch (const std::exception & ex) {
      response->success = false;
      response->error = ex.what();
      response->connections.clear();
    }
  }

  /**
   * @brief Callback function to cancel a fabric plan.
   */
  void cancelPlanCallback(
    const std::shared_ptr<CancelFabricPlan::Request> request,
    std::shared_ptr<CancelFabricPlan::Response> response)
  {
    RCLCPP_INFO(this->get_logger(), "[server] Plan cancelling requested");
    std::string plan_id = request->plan_id;
    std::string bond_id_to_cancel;
    bool found = false;

    // search for the bond id associated with the plan id from current plan or plan queue
    if (current_plan_.plan_id == plan_id) {
      setCurrentPlanStatus(fabric::plan::Status::CANCELLED);
      bond_id_to_cancel = current_plan_.bond_id;
      markPlanCompleted();
      found = true;
    } else {
      found = tryCancelQueuedPlan(plan_id, bond_id_to_cancel);
    }

    // if bond id is found, break the bond and cancel the plan
    if (!bond_id_to_cancel.empty()) {
      RCLCPP_INFO(this->get_logger(), "[server] Cancelling plan with id: %s", plan_id.c_str());

      for (auto & [bond_id, bond_client] : bond_client_cache_) {
        if (bond_id == bond_id_to_cancel) {
          bond_client->stop();
          RCLCPP_INFO(this->get_logger(), "[server] Bond with id : %s stopped", bond_id.c_str());
          break;
        }
      }
    }

    response->success = found;
  }

  /**
   * @brief Consume queued Fabric-owned status updates and persist feedback artifacts.
   *
   * Separating this loop from snapshot processing keeps runtime evidence
   * ingestion serialized and easier to reason about under concurrent updates.
   */
  void processingSystemStatus()
  {
    RCLCPP_INFO(this->get_logger(), "Fabric telemetry feedback processing started.");

    // Main processing loop
    while (rclcpp::ok()) {
      SystemStatus status;

      if (!fabric::server::status::waitPopSystemStatus(system_status_mutex_, system_status_cv_,
          system_status_records_, shutting_down_, status))
      {
        return;
      }

      telemetry_aggregator_.maybeMergeExecutionStatus(status);
      core_plugin_->update(status);
      const auto shutdown_safety = fabric::server::status::assessShutdownSafety(status);
      RCLCPP_INFO(this->get_logger(),
          "Injected Fabric system status into Fabric scoring core: %s kill_state=%s active_capabilities=%zu incomplete_capabilities=%zu reason=%s",
          fabric::server::status::summarizeSystemStatus(status).c_str(),
          fabric::server::status::shutdownSafetyLabel(shutdown_safety.state),
          shutdown_safety.active_capabilities,
          shutdown_safety.incomplete_capabilities,
          shutdown_safety.reason.c_str());
      persistTerminalPlanFeedback(status);
    }
    RCLCPP_INFO(this->get_logger(), "Fabric telemetry feedback processing complete.");
  }

  /**
   * @brief Callback function to set the completion of a fabric plan.
   */
  void setCompleteCallback(
    const std::shared_ptr<CompleteFabric::Request> request,
    std::shared_ptr<CompleteFabric::Response> response)
  {
    (void)response;

    // mark the plan as completed using plan id
    RCLCPP_INFO(this->get_logger(), "[server] Plan completion received for plan id: %s",
        request->plan_id.c_str());

    if (current_plan_.plan_id != request->plan_id) {
      RCLCPP_WARN(this->get_logger(),
          "[server] Received plan id does not match the current plan id");
    } else {
      RCLCPP_INFO(this->get_logger(),
          "[server] Current plan id matches the received plan id. Proceeding to complete the plan.");
      markPlanCompleted();
    }
  }

  void enqueuePlan(const fabric::plan::Plan & plan)
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      plan_queue_.push_back(plan);
    }

    publishPlanStatus(plan);
  }

  bool hasQueuedPlans()
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return !plan_queue_.empty();
  }

  bool tryDequeueNextPlan(fabric::plan::Plan & plan)
  {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (plan_queue_.empty()) {
      return false;
    }

    plan = plan_queue_.front();
    plan_queue_.pop_front();
    return true;
  }

  bool tryCancelQueuedPlan(const std::string & plan_id, std::string & bond_id_to_cancel)
  {
    std::optional<fabric::plan::Plan> cancelled_plan;
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      for (auto & plan : plan_queue_) {
        if (plan.plan_id == plan_id) {
          plan.status = fabric::plan::Status::CANCELLED;
          bond_id_to_cancel = plan.bond_id;
          cancelled_plan = plan;
          break;
        }
      }
    }

    if (cancelled_plan.has_value()) {
      publishPlanStatus(*cancelled_plan);
      return true;
    }

    return false;
  }

  void markPlanCompleted()
  {
    {
      std::lock_guard<std::mutex> lock(plan_mutex_);
      plan_completed_ = true;
    }
    plan_cv_.notify_all();
  }

  /**
   * @brief Callback function to get the status of a fabric plan.
   */
  void getPlanStatusCallback(
    const std::shared_ptr<GetPlanStatus::Request> request,
    std::shared_ptr<GetPlanStatus::Response> response)
  {
    response->header.stamp = this->now();

    // If the request plan_id and bond_id are empty, return the current plan status
    if (request->plan_id.empty() && request->bond_id.empty()) {
      response->status = fabric::server::status::toFabricStatusMsg(current_plan_);

      if (fabric::server::status::shouldExposeRejectedList(current_plan_)) {
        response->rejected_list = current_plan_.rejected_list;
      }

      return;
    }

    // If plan id or bond id provided, search the plan queue
    std::lock_guard<std::mutex> lock(queue_mutex_);
    for (const auto & plan : plan_queue_) {
      if (plan.plan_id == request->plan_id || plan.bond_id == request->bond_id) {
        response->status = fabric::server::status::toFabricStatusMsg(plan);

        if (fabric::server::status::shouldExposeRejectedList(plan)) {
          response->rejected_list = plan.rejected_list;
        }

        return;
      }
    }

    // Not found
    response->status.code = fabric_msgs::msg::FabricStatus::UNKNOWN;
    response->rejected_list.clear();
  }
  /**
   * @brief Generate a UUID string for plan tracking
   */
  static std::string generate_uuid()
  {
    uuid_t uuid;
    uuid_generate_random(uuid);
    char uuid_str[40];
    uuid_unparse(uuid, uuid_str);
    return std::string(uuid_str);
  }

  /**
   * @brief Reset internal data structures for processing a new plan.
   *
   */
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

  void setCurrentPlanStatus(const fabric::plan::Status status)
  {
    current_plan_.status = status;
    publishPlanStatus(current_plan_);
  }

  void publishPlanStatus(const fabric::plan::Plan & plan)
  {
    if (plan.plan_id.empty() || !fabric_status_pub_) {
      return;
    }

    fabric_status_pub_->publish(fabric::server::status::toFabricStatusMsg(plan));
  }

  /**
   * @brief Load and initialize the configured estimator plugins.
   *
   * Estimators are loaded dynamically so deployments can add or remove feature
   * generators without recompiling the server.
   */
  void loadEstimatorPlugins()
  {
    const std::vector<std::string> default_estimators = {
      "fabric::SuccessProbabilityPlugin", "fabric::RuntimePlugin", "fabric::EnergyPlugin",
      "fabric::FaultBurdenPlugin", "fabric::ResourcePlugin", "fabric::UncertaintyPlugin"
    };

    const std::vector<std::string> default_estimator_names = {"success", "time", "energy", "fault",
      "resource", "uncertainty"};

    const std::unordered_map<std::string, std::string> default_estimator_types = {
      {"success", "fabric::SuccessProbabilityPlugin"},
      {"time", "fabric::RuntimePlugin"},
      {"energy", "fabric::EnergyPlugin"},
      {"fault", "fabric::FaultBurdenPlugin"},
      {"resource", "fabric::ResourcePlugin"},
      {"uncertainty", "fabric::UncertaintyPlugin"},
    };

    this->declare_parameter<std::vector<std::string>>(
      fabric::server::experience_runtime::parameterName(
      "RapfsScorerPlugin.estimator_names"), default_estimator_names);
    std::vector<std::string> configured_estimator_names;
    this->get_parameter(fabric::server::experience_runtime::parameterName(
      "RapfsScorerPlugin.estimator_names"),
        configured_estimator_names);

    std::vector<std::string> configured_plugins;
    for (const auto & estimator_name : configured_estimator_names) {
      const auto default_type_it = default_estimator_types.find(estimator_name);
      const std::string default_type = default_type_it !=
        default_estimator_types.end() ? default_type_it->second : std::string();
      const std::string type_key = fabric::server::experience_runtime::parameterName(
        "RapfsScorerPlugin.estimators." + estimator_name + ".type");
      this->declare_parameter<std::string>(type_key, default_type);
      const std::string plugin_type = this->get_parameter(type_key).as_string();
      if (!plugin_type.empty()) {
        configured_plugins.push_back(plugin_type);
      }
    }

    this->declare_parameter<std::vector<std::string>>(
      fabric::server::experience_runtime::parameterName("estimator_plugins"),
      default_estimators);
    this->get_parameter(fabric::server::experience_runtime::parameterName("estimator_plugins"),
      estimator_plugin_names_);
    if (!configured_plugins.empty()) {
      estimator_plugin_names_ = configured_plugins;
    }

    estimator_plugins_.clear();
    for (const auto & plugin_name : estimator_plugin_names_) {
      try {
        auto plugin = estimator_loader_.createSharedInstance(plugin_name);
        plugin->initialize(shared_from_this());
        estimator_plugins_.push_back(plugin);
        RCLCPP_INFO(this->get_logger(), "Loaded estimator plugin: %s", plugin_name.c_str());
      } catch (const pluginlib::PluginlibException & ex) {
        RCLCPP_WARN(this->get_logger(), "Failed to load estimator plugin '%s': %s",
            plugin_name.c_str(), ex.what());
      }
    }
  }

  /**
   * @brief Load and initialize the configured scorer plugin.
   *
   * The scorer is configured after estimators so it can immediately consume the
   * active feature pipeline during planning requests.
   */
  void loadScorerPlugin()
  {
    this->declare_parameter<std::string>(
      fabric::server::experience_runtime::parameterName("scorer_plugin"),
        "fabric::RapfsScorerPlugin");
    this->get_parameter(fabric::server::experience_runtime::parameterName("scorer_plugin"),
      scorer_plugin_name_);

    scorer_plugin_ = scorer_loader_.createSharedInstance(scorer_plugin_name_);
    scorer_plugin_->initialize(shared_from_this());
    scorer_plugin_->configure(core_plugin_, &estimator_plugins_);
    RCLCPP_INFO(this->get_logger(), "Loaded scorer plugin: %s", scorer_plugin_name_.c_str());
  }

  /**
   * @brief Load and initialize the configured validation plugins.
   *
   * Validators run after parsing and before scoring so malformed or
   * unsatisfied candidate plans never win final selection.
   */
  void loadValidationPlugins()
  {
    this->declare_parameter<std::vector<std::string>>(
        fabric::server::experience_runtime::validationParameterName(use_experience_),
        use_experience_ ? std::vector<std::string>{"fabric::CompatibilityValidation",
        "fabric::DependencyValidation", "fabric::DataflowValidation"} :
                          std::vector<std::string>{"fabric::CompatibilityValidation",
        "fabric::ParameterValidation"});
    this->get_parameter(
        fabric::server::experience_runtime::validationParameterName(use_experience_),
        validation_plugin_names_);

    validation_plugins_.clear();
    for (const auto & plugin_name : validation_plugin_names_) {
      try {
        auto plugin = validation_loader_.createSharedInstance(plugin_name);
        plugin->initialize(shared_from_this());
        validation_plugins_.push_back(plugin);
        RCLCPP_INFO(this->get_logger(), "Loaded validation plugin: %s", plugin_name.c_str());
      } catch (const pluginlib::PluginlibException & ex) {
        RCLCPP_WARN(this->get_logger(), "Failed to load validation plugin '%s': %s",
            plugin_name.c_str(), ex.what());
      }
    }
  }

  /**
   * @brief Load refinement retry policy driven by validation outcomes.
   */
  void loadPlanningRefinementConfig()
  {
    const std::string planner_parameter_namespace =
      fabric::server::experience_runtime::pluginParameterNamespace(planner_plugin_name_);
    const std::string max_attempts_key = planner_parameter_namespace + ".refinement.max_attempts";
    const std::string retry_on_penalty_key = planner_parameter_namespace +
      ".refinement.retry_on_penalty_findings";
    const std::string retry_on_warning_key = planner_parameter_namespace +
      ".refinement.retry_on_warning_findings";

    this->declare_parameter<int>(max_attempts_key, planning_refinement_max_attempts_);
    this->declare_parameter<bool>(retry_on_penalty_key,
        planning_refinement_retry_on_penalty_findings_);
    this->declare_parameter<bool>(retry_on_warning_key,
        planning_refinement_retry_on_warning_findings_);

    planning_refinement_max_attempts_ = std::max(0,
        static_cast<int>(this->get_parameter(max_attempts_key).as_int()));
    planning_refinement_retry_on_penalty_findings_ =
      this->get_parameter(retry_on_penalty_key).as_bool();
    planning_refinement_retry_on_warning_findings_ =
      this->get_parameter(retry_on_warning_key).as_bool();
  }

  /**
   * @brief Load and initialize the configured planner plugin.
   *
   * Planning is pluginized so generation strategies can change independently of
   * the server lifecycle and persistence code.
   */
  void loadPlannerPlugin()
  {
    this->declare_parameter<std::string>(
      fabric::server::experience_runtime::parameterName("planner_plugin"),
        "fabric::PlanningPlugin");
    this->get_parameter(fabric::server::experience_runtime::parameterName("planner_plugin"),
      planner_plugin_name_);

    planner_plugin_ = planner_loader_.createSharedInstance(planner_plugin_name_);
    planner_plugin_->initialize(shared_from_this());
    RCLCPP_INFO(this->get_logger(), "Loaded planner plugin: %s", planner_plugin_name_.c_str());
  }

  /**
   * @brief Run ordered validation plugins over parsed candidate plans.
   *
   * @param request Planning request associated with the candidates.
   * @param planning_result Parsed candidates to validate in place.
   */
  void validatePlanningResult(const PlanningRequest & request, PlanningResult & planning_result)
  {
    for (auto & candidate : planning_result.candidates) {
      candidate.validation = CandidateValidationSummary{};
      for (const auto & validation_plugin : validation_plugins_) {
        validation_plugin->validate(request, capabilities_, candidate);
        if (candidate.validation.rejected) {
          break;
        }
      }
    }
  }

  void collect_capabilities_if_needed()
  {
    std::lock_guard<std::mutex> lock(capabilities_mutex_);
    if (!capabilities_.empty()) {
      return;
    }

    std::vector<fabric::capability::Descriptor> capabilities;
    capability_client_->getRunnableSpecs(capabilities);
    capabilities_ = std::move(capabilities);
    RCLCPP_INFO(this->get_logger(), "Received runnable capability metadata from capabilities2.");
  }

  bool shouldRefinePlanningResult(const PlanningResult & planning_result) const
  {
    return fabric::server::planning::shouldRefinePlanningResult(planning_result,
        planning_refinement_retry_on_penalty_findings_,
                                                                planning_refinement_retry_on_warning_findings_);
  }

  bool selectedPlanCanDispatch(const PlanningResult & planning_result) const
  {
    return fabric::server::planning::selectedPlanCanDispatch(planning_result);
  }

  /**
   * @brief Persist attribution and score artifacts for the selected candidate plan.
   *
   * These records capture why the plan was chosen before execution mutates the
   * state, making later debugging and explanation queries reproducible.
   *
   * @param request Planning request that produced the candidate plans.
   * @param planning_result Planning result containing all candidates.
   */
  void markSelectedCandidate(
    const PlanningRequest & request,
    const PlanningResult & planning_result)
  {
    fabric::server::feedback::recordSelectedCandidateArtifacts(*core_plugin_, request,
        planning_result);
  }

  /**
   * @brief Queue the selected plan until runtime feedback identifies its executing plan ID.
   *
   * Supervisor telemetry arrives later under a runtime plan identifier, so this
   * queue bridges generation-time selection with execution-time feedback.
   *
   * @param request Planning request that produced the candidate plans.
   * @param planning_result Planning result containing all candidates.
   */
  void queuePendingSelectedPlan(
    const PlanningRequest & request,
    const PlanningResult & planning_result)
  {
    fabric::server::planning::queuePendingSelectedPlan(planning_state_mutex_,
        pending_generated_plans_, request, planning_result);
  }

  // Server member-function fragments for later integration into Fabric; do not include as a standalone header.

  CandidatePlan parseCandidatePlanForScoring(
    const PlanningRequest & request,
    const std::string & candidate_plan_id, const std::string & plan_xml) const
  {
    return fabric::server::planning::parseCandidatePlanForScoring(*core_plugin_, *parsing_plugin_,
        request, candidate_plan_id, plan_xml);
  }

  /**
   * @brief Serve stored plan-score explanations for a selected candidate plan.
   *
   * @param request Service request identifying the plan and optional node.
   * @param response Service response to populate.
   */
  void handleGetPlanScoreExplanation(
    const std::shared_ptr<GetPlanScoreExplanation::Request> request,
    std::shared_ptr<GetPlanScoreExplanation::Response> response)
  {
    if (!startup_complete_.load()) {
      fabric::server::score_services::setGetPlanScoreExplanationUnavailable(*response);
      return;
    }

    const PlanNodeScoreExplanationQuery query{request->planning_request_id,
      request->candidate_plan_id, request->graph_hash};
    const auto bundle = core_plugin_->readSelectedPlanScoreExplanationBundle(query);
    if (!bundle.has_value()) {
      fabric::server::score_services::setGetPlanScoreExplanationNotFound(*response);
      return;
    }

    std::optional<PlanNodeScoreExplanationRecord> node_record;

    if (!request->candidate_node_id.empty()) {
      const PlanNodeScoreExplanationNodeQuery node_query{request->planning_request_id,
        request->candidate_plan_id, request->graph_hash,
        request->candidate_node_id};
      const auto node = core_plugin_->readPlanNodeScoreExplanation(node_query);
      if (node.has_value()) {
        node_record = node.value();
      }
    }

    fabric::server::score_services::populateGetPlanScoreExplanationResponse(bundle.value(),
        node_record, *response);

    if (!request->candidate_node_id.empty() && !node_record.has_value()) {
      fabric::server::score_services::setGetPlanScoreExplanationNodeMissing(*response);
    }
  }

  void handleScorePlan(
    const std::shared_ptr<ScorePlan::Request> request,
    std::shared_ptr<ScorePlan::Response> response)
  {
    if (!startup_complete_.load()) {
      fabric::server::score_services::setScorePlanUnavailable(*response);
      return;
    }

    if (const auto error = fabric::server::score_services::validateScorePlanRequest(*request);
      error.has_value())
    {
      fabric::server::score_services::setScorePlanFailure(*response, error.value());
      return;
    }

    try {
      const auto now = this->now();
      PlanningRequest scoring_request;
      scoring_request.planning_request_id =
        !request->planning_request_id.empty() ? request->planning_request_id :
        fabric::server::feedback::generatePlanningRequestId(now);
      scoring_request.task_description = request->task_description;
      scoring_request.recorded_at_sec = static_cast<int32_t>(now.seconds());
      scoring_request.recorded_at_nanosec = static_cast<uint32_t>(now.nanoseconds() %
        1000000000ULL);

      const std::string candidate_plan_id =
        !request->candidate_plan_id.empty() ? request->candidate_plan_id :
        scoring_request.planning_request_id + "/candidate_0";

      PlanningResult planning_result;
      planning_result.candidates.push_back(parseCandidatePlanForScoring(scoring_request,
          candidate_plan_id, request->plan_xml));

      validatePlanningResult(scoring_request, planning_result);
      scorer_plugin_->scorePlanningResult(scoring_request, planning_result);

      const auto bundle = fabric::server::feedback::buildScoreExplanationBundle(scoring_request,
          planning_result);
      fabric::server::score_services::populateScorePlanResponse(bundle, planning_result,
          request->candidate_node_id, *response);
    } catch (const std::exception & ex) {
      fabric::server::score_services::setScorePlanFailure(*response, ex.what());
    }
  }

  /**
   * @brief Read one scoring epoch/version record by epoch ID.
   *
   * @param request Service request identifying the epoch.
   * @param response Service response populated with the stored record when found.
   */
  void handleGetScoringEpoch(
    const std::shared_ptr<GetScoringEpoch::Request> request,
    std::shared_ptr<GetScoringEpoch::Response> response)
  {
    if (!startup_complete_.load()) {
      fabric::server::score_services::setGetScoringEpochUnavailable(*response);
      return;
    }

    if (request->epoch_id.empty()) {
      fabric::server::score_services::setGetScoringEpochMissingId(*response);
      return;
    }

    const auto record = core_plugin_->readScoringEpoch(request->epoch_id);
    if (!record.has_value()) {
      fabric::server::score_services::setGetScoringEpochNotFound(*response);
      return;
    }

    fabric::server::score_services::populateGetScoringEpochResponse(record.value(), *core_plugin_,
        *response);
  }

  /**
   * @brief Persist or update a scoring epoch/version record.
   *
   * @param request Service request carrying epoch metadata.
   * @param response Service response indicating whether the row was stored.
   */
  void handleRecordScoringEpoch(
    const std::shared_ptr<RecordScoringEpoch::Request> request,
    std::shared_ptr<RecordScoringEpoch::Response> response)
  {
    if (!startup_complete_.load()) {
      fabric::server::score_services::setRecordScoringEpochUnavailable(*response);
      return;
    }

    if (request->epoch_id.empty()) {
      fabric::server::score_services::setRecordScoringEpochMissingId(*response);
      return;
    }

    const auto existing = core_plugin_->readScoringEpoch(request->epoch_id);

    const auto stamp = this->now();
    const auto record = fabric::server::score_services::buildScoringEpochRecord(existing, *request,
        static_cast<int32_t>(stamp.seconds()),
                                                                                static_cast<uint32_t>
        (stamp.nanoseconds() % 1000000000ULL));

    fabric::server::score_services::populateRecordScoringEpochResponse(existing.has_value(),
        core_plugin_->recordScoringEpoch(record), *response);
  }

  /**
  * @brief Persist terminal plan feedback once Fabric status reaches a terminal state.
   *
   * This closes the loop between predicted plan quality and observed execution
   * outcome so later scoring explanations can compare forecast versus reality.
   *
  * @param status Fabric-owned system status snapshot.
   */
  void persistTerminalPlanFeedback(const SystemStatus & status)
  {
    if (!fabric::server::feedback::isTerminalPlanState(status.capability_plan_state) ||
      status.plan_id.empty() ||
      status.planning_request_id.empty() || status.candidate_plan_id.empty() ||
      status.graph_hash.empty())
    {
      return;
    }

    PendingGeneratedPlan pending;
    {
      std::lock_guard<std::mutex> lock(planning_state_mutex_);
      if (finalized_plan_feedback_.count(status.plan_id) > 0) {
        return;
      }

      auto pending_it = pending_generated_plans_.end();
      for (auto it = pending_generated_plans_.begin(); it != pending_generated_plans_.end(); ++it) {
        if (it->planning_request_id == status.planning_request_id &&
          it->candidate_plan_id == status.candidate_plan_id &&
          it->graph_hash == status.graph_hash)
        {
          pending_it = it;
          break;
        }
      }

      if (pending_it == pending_generated_plans_.end()) {
        return;
      }

      pending = *pending_it;
      finalized_plan_feedback_.insert(status.plan_id);
      pending_generated_plans_.erase(pending_it);
    }

    fabric::server::feedback::recordTerminalPlanFeedback(*core_plugin_, pending, status);
    telemetry_aggregator_.eraseExecutionStatus(status.plan_id);
    telemetry_aggregator_.eraseCapabilityBatteryUsage(status.plan_id);
  }

  void batteryCallback(const BatteryState::SharedPtr msg)
  {
    {
      std::lock_guard<std::mutex> lock(battery_mutex_);
      last_battery_msg_ = msg;
    }

    const double percentage = msg->percentage;
    uint8_t label = 0;
    std::string description;
    if (percentage < 0.0) {
      description = "Battery state received (percentage unknown).";
    } else if (percentage < 0.2) {
      label = 2;
      description = "Battery critically low (" + std::to_string(percentage * 100.0) + "%).";
    } else if (percentage < 0.4) {
      label = 1;
      description = "Battery low (" + std::to_string(percentage * 100.0) + "%).";
    } else {
      description = "Battery level nominal (" + std::to_string(percentage * 100.0) + "%).";
    }

    if (label == 0) {
      return;
    }

    telemetry_aggregator_.rememberTelemetryRecord(TelemetryRecord{
        fabric::status::Time{msg->header.stamp.sec, msg->header.stamp.nanosec},
        "battery",
        description,
        label,
    }, this->now().nanoseconds());
  }

  /**
   * @brief Persist external plan-level feedback without erasing prior score or runtime fields.
   *
   * @param request Service request carrying plan identity and external outcome.
   * @param response Service response indicating whether the row was stored.
   */
  void handleRecordPlanFeedback(
    const std::shared_ptr<RecordPlanFeedback::Request> request,
    std::shared_ptr<RecordPlanFeedback::Response> response)
  {
    if (!startup_complete_.load()) {
      fabric::server::score_services::setRecordPlanFeedbackUnavailable(*response);
      return;
    }

    if (const auto error =
      fabric::server::score_services::validateRecordPlanFeedbackRequest(*request);
      error.has_value())
    {
      response->stored = false;
      response->found_existing = false;
      response->message = error.value();
      return;
    }

    const auto query = fabric::server::score_services::buildPlanFeedbackQuery(*request);
    const auto existing = core_plugin_->readSelectedPlanScoreExplanationBundle(query);

    const auto stamp = this->now();
    const auto feedback = fabric::server::score_services::buildPlanFeedbackRecord(existing,
        *request, static_cast<int32_t>(stamp.seconds()),
                                                                                  static_cast<
          uint32_t>(stamp.nanoseconds() % 1000000000ULL));

    fabric::server::score_services::populateRecordPlanFeedbackResponse(existing.has_value(),
        core_plugin_->recordPlanFeedback(feedback), *response);
  }

  /** Vector of plans */
  std::deque<fabric::plan::Plan> plan_queue_;

  /** Protect queued plan access across callbacks and processing thread */
  std::mutex queue_mutex_;

  /** Current plan being processed */
  fabric::plan::Plan current_plan_;

  /** Current plan related synchronization */
  bool plan_completed_{false};
  std::mutex plan_mutex_;
  std::condition_variable plan_cv_;

  /** Deferred startup and process thread lifecycle */
  std::thread startup_thread_;
  std::thread process_thread_;
  std::mutex process_thread_mutex_;
  std::atomic<bool> capabilities_ready_{false};
  std::atomic<bool> experience_ready_{false};

  bool use_experience_{false};
  bool rebuild_experience_{false};

  std::atomic<bool> process_thread_running_{false};
  std::atomic<bool> startup_complete_{false};
  std::atomic<bool> shutting_down_{false};

  /** Capability client to interact with capability server */
  std::shared_ptr<CapabilityClient> capability_client_;

  /** Capability List */
  std::vector<fabric::capability::Info> capability_list_;

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
   * @brief Loaded validation plugins used before candidate scoring.
   */
  std::vector<std::shared_ptr<fabric::ValidationBase>> validation_plugins_;

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
  std::filesystem::path core_db_path_{"core_graphrag.sqlite3"};
  int planning_refinement_max_attempts_{0};
  bool planning_refinement_retry_on_penalty_findings_{true};
  bool planning_refinement_retry_on_warning_findings_{false};
  std::string user_feedback_root_dir_;
  bool consume_feedback_{false};
  bool dispatch_terminal_success_only_{false};
  std::string fabric_status_topic_;
  bool enable_battery_monitoring_{true};
  std::string battery_topic_;
  double monitor_poll_frequency_hz_{2.0};
  int telemetry_retention_seconds_{300};
  fabric::server::telemetry::Aggregator telemetry_aggregator_;
  std::mutex battery_mutex_;
  BatteryState::SharedPtr last_battery_msg_;

  /** Fabric-owned runtime telemetry publication and internal feedback queue. */
  rclcpp::Publisher<fabric_msgs::msg::FabricStatus>::SharedPtr fabric_status_pub_;
  rclcpp::Subscription<BatteryState>::SharedPtr battery_sub_;
  std::thread system_status_process_thread_;
  std::thread monitor_ingestion_thread_;
  std::mutex system_status_mutex_;
  std::condition_variable system_status_cv_;
  std::deque<SystemStatus> system_status_records_;

  /** server to submit a plan with optional Fabric-owned metadata */
  rclcpp::Service<SubmitFabricPlan>::SharedPtr submit_plan_server_;

  /** server to cancel the current plan in the capabilities2 fabric */
  rclcpp::Service<CancelFabricPlan>::SharedPtr cancel_server_;

  /** server to get the status of the capabilities2 fabric */
  rclcpp::Service<CompleteFabric>::SharedPtr completion_server_;

  /** server to get the status of a fabric plan */
  rclcpp::Service<GetPlanStatus>::SharedPtr get_plan_status_server_;

  /** service to parse Fabric XML into connection graph data */
  rclcpp::Service<ParsePlan>::SharedPtr parse_plan_server_;

  /**
   * @brief Debug service for selected plan score explanations.
   */
  rclcpp::Service<GetPlanScoreExplanation>::SharedPtr get_plan_score_explanation_service_;

  /**
   * @brief Service for scoring a supplied plan XML without execution dispatch.
   */
  rclcpp::Service<ScorePlan>::SharedPtr score_plan_service_;

  /**
   * @brief Read service for scorer epoch/version checkpoints.
   */
  rclcpp::Service<GetScoringEpoch>::SharedPtr get_scoring_epoch_service_;

  /**
   * @brief Service for recording external whole-plan feedback.
   */
  rclcpp::Service<RecordPlanFeedback>::SharedPtr record_plan_feedback_service_;

  /**
   * @brief Service for recording scorer epoch/version checkpoints.
   */
  rclcpp::Service<RecordScoringEpoch>::SharedPtr record_scoring_epoch_service_;

  /** action server to generate a plan */
  rclcpp_action::Server<GeneratePlan>::SharedPtr generate_plan_server_;

};  // class Fabric

}  // namespace fabric
