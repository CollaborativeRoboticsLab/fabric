#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rclcpp/rclcpp.hpp>

#include <prompt_msgs/msg/model_option.hpp>
#include <prompt_msgs/srv/prompt.hpp>

#include <fabric_base/behaviour_interface_base.hpp>
#include <fabric_base/planner_base.hpp>

namespace fabric
{
/**
 * @brief Planning plugin for XML plans.
 *
 * This plugin plans the XML plan for task execution.
 */
class PlanningPlugin : public PlannerBase
{
public:
  using Prompt = prompt_msgs::srv::Prompt;
  using PlanningRequest = planning::Request;
  using PlanningRefinementRequest = planning::RefinementRequest;
  using PlanningResult = planning::Result;
  using BehaviourInfo = capability::Descriptor;
  using BehaviourPlanDefinition = capability::PlanDefinition;
  using BehaviourInterfaceBase = CapabilityInterfaceBase;
  using CandidatePlan = planning::CandidatePlan;
  using CandidatePlanNode = planning::CandidatePlanNode;
  using CandidatePlanEdge = planning::CandidatePlanEdge;
  using CandidateSkillRequest = experience::CandidateSkillRequest;
  using CandidateSkillResolution = experience::CandidateSkillResolution;
  using CandidatePlanSkillAttribution = experience::CandidatePlanSkillAttribution;
  using CandidatePlanEdgeAttribution = experience::CandidatePlanEdgeAttribution;
  using SemanticContextBundle = experience::SemanticContextBundle;
  using SemanticContextItem = experience::SemanticContextItem;
  using Dependency = capability::Relation;
  using SkillInfo = plan::node;

  struct GeneratedPlanResponse
  {
    std::string plan;
    std::string reasoning;
  };

  PlanningPlugin() = default;
  virtual ~PlanningPlugin() = default;
  /**
   * @brief Initialize the XML planning plugin.
   *
   * @param node Shared pointer to the ROS2 node.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "PlanningPlugin");
    prompt_client_ = node_->create_client<Prompt>("prompt/prompt");
    if (!prompt_client_->wait_for_service(std::chrono::seconds(10)))
      throw std::runtime_error("Prompt service not available for PlanningPlugin");

    node_->declare_parameter<std::string>(plugin_name_ + ".model_family", "openai");
    node_->declare_parameter<std::string>(plugin_name_ + ".model", "gpt-5.1");
    node_->declare_parameter<bool>(plugin_name_ + ".use_cache", true);
    node_->declare_parameter<bool>(plugin_name_ + ".use_chat_mode", false);
    node_->declare_parameter<int>(plugin_name_ + ".max_capability_context", max_capability_context_);
    node_->declare_parameter<std::string>(plugin_name_ + ".semantic_context_mode", semantic_context_mode_);
    node_->declare_parameter<bool>(plugin_name_ + ".log_semantic_context_comparison", log_semantic_context_comparison_);
    node_->declare_parameter<bool>(plugin_name_ + ".persist_prompt_audit", persist_prompt_audit_);
    node_->declare_parameter<std::string>(plugin_name_ + ".prompt_audit_dir", prompt_audit_dir_);
    node_->declare_parameter<bool>(plugin_name_ + ".prompt_audit_include_full_text", prompt_audit_include_full_text_);

    node_->get_parameter(plugin_name_ + ".model_family", model_family_);
    node_->get_parameter(plugin_name_ + ".model", model_);
    node_->get_parameter(plugin_name_ + ".use_cache", use_cache_);
    node_->get_parameter(plugin_name_ + ".use_chat_mode", use_chat_mode_);
    node_->get_parameter(plugin_name_ + ".max_capability_context", max_capability_context_);
    node_->get_parameter(plugin_name_ + ".semantic_context_mode", semantic_context_mode_);
    node_->get_parameter(plugin_name_ + ".log_semantic_context_comparison", log_semantic_context_comparison_);
    node_->get_parameter(plugin_name_ + ".persist_prompt_audit", persist_prompt_audit_);
    node_->get_parameter(plugin_name_ + ".prompt_audit_dir", prompt_audit_dir_);
    node_->get_parameter(plugin_name_ + ".prompt_audit_include_full_text", prompt_audit_include_full_text_);
  }

  /**
   * @brief Plan the given XML plan.
   *
   */
  PlanningResult plan(const PlanningRequest& request, const std::vector<BehaviourInfo>& capabilities, CoreBase& core,
                      InterfaceBase& interface) override
  {
    auto& behaviour_interface = dynamic_cast<BehaviourInterfaceBase&>(interface);
    const auto task_semantic_context = core.retrieveSemanticContext(
      request.task_description, "", static_cast<size_t>(std::max(2, max_capability_context_ * 2)));
    const auto ranked_capabilities = selectCandidateCapabilities(request.task_description, capabilities, task_semantic_context);
    const auto generation_context = collectCapabilityContext(request, ranked_capabilities, core);
    const std::string semantic_mode = normalizedSemanticContextMode(semantic_context_mode_);
    const std::string detailed_generation_prompt =
      buildGenerationPrompt(request, generation_context, task_semantic_context, false);
    const std::string community_generation_prompt =
      buildGenerationPrompt(request, generation_context, task_semantic_context, true);
    if (log_semantic_context_comparison_)
      logSemanticContextComparison(detailed_generation_prompt, community_generation_prompt, task_semantic_context);
    if (persist_prompt_audit_)
      persistPromptAudit(request, ranked_capabilities, task_semantic_context, detailed_generation_prompt,
                         community_generation_prompt, semantic_mode, "generation", 1);

    const auto draft_plan_response = parseGeneratedPlanResponse(
      requestPrompt(semantic_mode == "community" ? community_generation_prompt : detailed_generation_prompt,
                    promptRequestUuid(request), request.flush));

    std::string draft_plan = sanitizePlanXml(draft_plan_response.plan);
    std::string critique;
    try
    {
      const auto parsed_draft = behaviour_interface.parseBehaviourPlan(draft_plan);
      if (!parsed_draft.success)
        throw std::runtime_error(parsed_draft.error);
      critique = critiquePlan(parsed_draft, generation_context);
    }
    catch (const std::exception& ex)
    {
      critique = std::string("Parser failed on draft plan: ") + ex.what();
    }

    return refinePlanningResult(request, generation_context, task_semantic_context, semantic_mode == "community",
                  draft_plan, critique, core, behaviour_interface, 2, "refinement", 0);
  }

  PlanningResult refinePlan(const PlanningRefinementRequest& refinement_request,
                            const std::vector<BehaviourInfo>& capabilities, CoreBase& core,
                            InterfaceBase& interface) override
  {
    auto& behaviour_interface = dynamic_cast<BehaviourInterfaceBase&>(interface);
    const auto& request = refinement_request.request;
    const auto task_semantic_context = core.retrieveSemanticContext(
      request.task_description, "", static_cast<size_t>(std::max(2, max_capability_context_ * 2)));
    const auto ranked_capabilities =
      selectCandidateCapabilities(request.task_description, capabilities, task_semantic_context);
    const auto generation_context = collectCapabilityContext(request, ranked_capabilities, core);
    const bool use_community_context = normalizedSemanticContextMode(semantic_context_mode_) == "community";
    return refinePlanningResult(request, generation_context, task_semantic_context, use_community_context,
                  sanitizePlanXml(refinement_request.draft_plan), refinement_request.critique, core,
                  behaviour_interface, std::max(2, refinement_request.attempt_index + 2),
                  "refinement", refinement_request.attempt_index);
  }

protected:
  struct CapabilityGenerationContext
  {
    const BehaviourInfo* capability = nullptr;
    std::string context_key;
    std::string context_class;
    CandidateSkillResolution resolution;
  };

  /**
   * @brief Rank capabilities by lexical overlap with the task description.
    *
    * The planner keeps prompt context small, so this heuristic biases the model
    * toward capabilities whose names and descriptions most clearly match the task.
   *
   * @param task_description Natural-language task description.
   * @param capabilities Available capabilities to rank.
   * @return std::vector<const BehaviourInfo*> Selected capability subset for prompting.
   */
  std::vector<const BehaviourInfo*> selectCandidateCapabilities(const std::string& task_description,
                                                                const std::vector<BehaviourInfo>& capabilities,
                                                                const SemanticContextBundle& semantic_context) const
  {
    const auto task_tokens = planning::tokenize(task_description);
    std::unordered_set<std::string> task_token_set(task_tokens.begin(), task_tokens.end());
    std::unordered_map<std::string, double> semantic_relevance;
    for (const auto& item : semantic_context.relevant_capabilities)
    {
      const std::string skill_id = planning::normalize_identifier(item.name);
      auto it = semantic_relevance.find(skill_id);
      if (it == semantic_relevance.end() || item.relevance > it->second)
        semantic_relevance[skill_id] = item.relevance;
    }

    struct RankedCapability
    {
      const BehaviourInfo* capability;
      double score;
    };

    std::vector<RankedCapability> ranked;
    ranked.reserve(capabilities.size());
    for (const auto& capability : capabilities)
    {
      const auto capability_tokens =
        planning::tokenize(capability.interface_name + " " + capability.interface_description);
      int score = 0;
      for (const auto& token : capability_tokens)
      {
        if (task_token_set.count(token) > 0)
          ++score;
      }
      const auto semantic_it = semantic_relevance.find(planning::normalize_identifier(capability.interface_name));
      const double semantic_score = semantic_it == semantic_relevance.end() ? 0.0 : semantic_it->second * 10.0;
      ranked.push_back(RankedCapability{ &capability, static_cast<double>(score) + semantic_score });
    }

    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const RankedCapability& lhs, const RankedCapability& rhs) { return lhs.score > rhs.score; });

    std::vector<const BehaviourInfo*> selected;
    for (const auto& item : ranked)
    {
      if (selected.size() >= static_cast<size_t>(std::max(1, max_capability_context_)))
        break;
      if (item.score > 0 || selected.empty())
        selected.push_back(item.capability);
    }

    if (selected.empty())
    {
      for (const auto& capability : capabilities)
      {
        selected.push_back(&capability);
        if (selected.size() >= static_cast<size_t>(std::max(1, max_capability_context_)))
          break;
      }
    }

    return selected;
  }

  /**
   * @brief Resolve evidence context for the capabilities included in prompt generation.
    *
    * The prompt includes fallback and sample-count metadata so the model can
    * prefer capabilities backed by stronger empirical evidence.
   *
   * @param request Planning request being processed.
   * @param capabilities Capabilities selected for prompt generation.
   * @param core Core plugin used to resolve capability evidence.
   * @return std::vector<CapabilityGenerationContext> Evidence-annotated capability context.
   */
  std::vector<CapabilityGenerationContext> collectCapabilityContext(
      const PlanningRequest& request, const std::vector<const BehaviourInfo*>& capabilities, CoreBase& core) const
  {
    std::vector<CapabilityGenerationContext> context;
    const std::string context_key = planning::build_context_key(request.task_description);
    const std::string context_class = planning::build_context_class(request.task_description);

    int index = 0;
    for (const auto* capability : capabilities)
    {
      CapabilityGenerationContext item;
      item.capability = capability;
      item.context_key = context_key;
      item.context_class = context_class;
      item.resolution = core.resolveCandidateSkill(
          CandidateSkillRequest{ request.planning_request_id, request.planning_request_id + "/draft", "",
                     "draft_node_" + std::to_string(index), capability->interface_name,
               planning::normalize_identifier(capability->interface_name), context_key,
                     context_class });
      context.push_back(item);
      ++index;
    }
    return context;
  }

  /**
   * @brief Build the initial prompt used to generate a candidate Fabric plan.
    *
    * This prompt constrains the model to valid Fabric XML and injects evidence
    * summaries so generation can trade off feasibility against prior familiarity.
   *
   * @param request Planning request being processed.
   * @param context Capability evidence context to include in the prompt.
   * @return std::string Prompt text for initial plan generation.
   */
  std::string buildGenerationPrompt(const PlanningRequest& request,
                                    const std::vector<CapabilityGenerationContext>& context,
                                    const SemanticContextBundle& semantic_context,
                                    bool use_community_context) const
  {
    std::ostringstream prompt;
    prompt << "Generate one valid Fabric XML plan for the task. Use only <Plan>, <Control>, and <Runner>. ";
    prompt << "Use <Control type=\"sequential\">, <Control type=\"parallel_any\">, <Control type=\"parallel_all\">, "
              "and <Control type=\"recovery\"> when needed. ";
    prompt << "Return one JSON object with keys \"plan\" and \"reasoning\". ";
    prompt << "The \"plan\" value must be valid Fabric XML. The \"reasoning\" value must be a brief summary, not chain-of-thought. ";
    prompt << "Return no markdown fences.\n\n";
    prompt << "Task: " << request.task_description << "\n\n";
    appendSemanticContext(prompt, semantic_context, use_community_context);
    prompt << "Candidate capability definitions. Use interface/provider exactly as listed. Use command as the "
              "authoritative execution intent.\n";
    prompt << "If a capability lists implicit_prerequisites, those dependencies are auto-started by capabilities2 "
              "backend dependency handling. Do not add them as explicit <Runner> nodes unless the task is "
              "specifically about managing that dependency lifecycle.\n";
    for (const auto& item : context)
    {
      prompt << "- interface: " << item.capability->interface_name << "\n";
      prompt << "  provider: " << item.capability->provider_name << "\n";
      prompt << "  command: " << item.capability->command << "\n";
      prompt << "  description: " << item.capability->interface_description << "\n";
      const auto implicit_prerequisites = implicitPrerequisites(*item.capability);
      if (!implicit_prerequisites.empty())
      {
        prompt << "  implicit_prerequisites:";
        for (const auto& dependency : implicit_prerequisites)
          prompt << " [" << dependency << "]";
        prompt << "\n";
      }
      const auto semantic_relations = semanticRelations(*item.capability);
      if (!semantic_relations.empty())
      {
        prompt << "  relations:";
        for (const auto& relation : semantic_relations)
          prompt << " [" << relation.subject << ' ' << relation.predicate << ' ' << relation.object << "]";
        prompt << "\n";
      }
      prompt << "  evidence: fallback=" << item.resolution.fallback_level << ", reason=" << item.resolution.reason
             << ", success_samples=" << item.resolution.evidence.runs_completed
             << ", runtime_samples=" << item.resolution.evidence.runtime_sample_count << "\n";
    }
    return prompt.str();
  }

  static std::vector<std::string> implicitPrerequisites(const BehaviourInfo& capability)
  {
    std::vector<std::string> dependencies;
    std::unordered_set<std::string> seen;
    for (const auto& relation : capability.relations)
    {
      if (relation.predicate != "depends_on" || relation.object.empty())
        continue;
      if (seen.insert(relation.object).second)
        dependencies.push_back(relation.object);
    }
    return dependencies;
  }

  static std::vector<Dependency> semanticRelations(const BehaviourInfo& capability)
  {
    std::vector<Dependency> relations;
    for (const auto& relation : capability.relations)
    {
      if (relation.predicate == "depends_on")
        continue;
      relations.push_back(relation);
    }
    return relations;
  }

  /**
   * @brief Build the follow-up prompt used to refine a draft plan.
    *
    * Refinement gives the planner a second pass after parser feedback so invalid
    * or weakly supported drafts can be corrected before scoring.
   *
   * @param request Planning request being processed.
   * @param context Capability evidence context to include in the prompt.
   * @param draft_plan Draft plan generated in the first prompt round.
   * @param critique Parser and evidence critique for the draft plan.
   * @return std::string Prompt text for refinement.
   */
  std::string buildRefinementPrompt(const PlanningRequest& request,
                                    const std::vector<CapabilityGenerationContext>& context,
                                    const SemanticContextBundle& semantic_context,
                                    bool use_community_context,
                                    const std::string& draft_plan, const std::string& critique) const
  {
    std::ostringstream prompt;
    prompt << buildGenerationPrompt(request, context, semantic_context, use_community_context) << "\n";
    prompt << "Draft plan to refine:\n" << draft_plan << "\n\n";
    prompt << "Issues identified by Experience:\n" << critique << "\n\n";
    prompt << "Refine the draft to fix parser issues and prefer capabilities with empirical evidence over prior-only "
              "fallbacks when possible. Use the issues identified by Experience as authoritative constraints. "
              "Return one JSON object with keys \"plan\" and \"reasoning\" only.";
    return prompt.str();
  }

  PlanningResult refinePlanningResult(const PlanningRequest& request,
                                      const std::vector<CapabilityGenerationContext>& generation_context,
                                      const SemanticContextBundle& task_semantic_context,
                                      const bool use_community_context, const std::string& draft_plan,
                                      const std::string& critique, CoreBase& core,
                                      BehaviourInterfaceBase& behaviour_interface,
                                      const int prompt_iteration = 2,
                                      const std::string& prompt_phase = "refinement",
                                      const int refinement_attempt_index = 0) const
  {
    PlanningResult result;
    const std::string detailed_refinement_prompt =
      buildRefinementPrompt(request, generation_context, task_semantic_context, false, draft_plan, critique);
    const std::string community_refinement_prompt =
      buildRefinementPrompt(request, generation_context, task_semantic_context, true, draft_plan, critique);
    if (log_semantic_context_comparison_)
      logSemanticContextComparison(detailed_refinement_prompt, community_refinement_prompt, task_semantic_context);
    if (persist_prompt_audit_)
    {
      std::vector<const BehaviourInfo*> ranked_capabilities;
      ranked_capabilities.reserve(generation_context.size());
      for (const auto& item : generation_context)
      {
        if (item.capability != nullptr)
          ranked_capabilities.push_back(item.capability);
      }

      persistPromptAudit(request, ranked_capabilities, task_semantic_context, detailed_refinement_prompt,
                         community_refinement_prompt, use_community_context ? "community" : "detailed",
                         prompt_phase, prompt_iteration, draft_plan, critique, refinement_attempt_index);
    }

    const auto final_plan_response = parseGeneratedPlanResponse(
      requestPrompt(use_community_context ? community_refinement_prompt : detailed_refinement_prompt,
                    promptRequestUuid(request), request.flush));

    CandidatePlan candidate = parseCandidatePlan(request, sanitizePlanXml(final_plan_response.plan), generation_context,
                                                 core, behaviour_interface);
    candidate.reasoning = final_plan_response.reasoning;
    persistCandidateAttribution(request, candidate, core);
    result.candidates.push_back(candidate);
    result.selected_candidate_plan_id = candidate.candidate_plan_id;
    result.selected_plan = candidate.plan;
    result.selected_reasoning = candidate.reasoning;
    return result;
  }

  /**
   * @brief Send a prompt request to PromptTools and return the model response.
    *
    * The call stays synchronous here because planning needs a complete model
    * response before it can parse, critique, or score the candidate plan.
   *
   * @param prompt_text Prompt text to send.
   * @param uuid Request UUID used for caching and traceability.
   * @param flush_cache Whether the remote prompt service should bypass cache.
   * @return std::string Prompt response text.
   */
  std::string requestPrompt(const std::string& prompt_text, const std::string& uuid, bool flush_cache) const
  {
    auto request = std::make_shared<Prompt::Request>();
    request->uuid = uuid;
    request->prompt.prompt = prompt_text;
    request->prompt.model_family = model_family_;
    request->prompt.use_cache = use_cache_;
    request->prompt.flush_cache = flush_cache;
    request->prompt.use_chat_mode = use_chat_mode_;

    prompt_msgs::msg::ModelOption model_option;
    model_option.key = "model";
    model_option.value = model_;
    request->prompt.options.push_back(model_option);

    std::mutex mutex;
    std::condition_variable cv;
    bool completed = false;
    Prompt::Response::SharedPtr response;
    std::unique_lock<std::mutex> lock(mutex);

    prompt_client_->async_send_request(request,
                                       [&response, &completed, &cv](rclcpp::Client<Prompt>::SharedFuture future) {
                                         response = future.get();
                                         completed = true;
                                         cv.notify_all();
                                       });

    cv.wait(lock, [&completed]() { return completed; });
    if (!response || !response->response.success)
      throw std::runtime_error("PromptTools plan generation failed");
    return response->response.response;
  }

  /**
   * @brief Extract the outer Plan XML document from a model response.
    *
    * Models sometimes wrap the answer with extra prose or fences, so this keeps
    * only the executable XML fragment expected by downstream parsers.
   *
   * @param response Raw model response text.
   * @return std::string Sanitized Plan XML fragment.
   */
  static std::string sanitizePlanXml(const std::string& response)
  {
    const std::string xml_start = "<Plan";
    const auto start = response.find(xml_start);
    const auto end = response.rfind("</Plan>");
    if (start == std::string::npos || end == std::string::npos)
      return response;
    return response.substr(start, end - start + 7);
  }

  static std::string promptRequestUuid(const PlanningRequest& request)
  {
    return request.uuid.empty() ? request.planning_request_id : request.uuid;
  }

  GeneratedPlanResponse parseGeneratedPlanResponse(const std::string& response) const
  {
    try
    {
      const auto parsed = extractJsonObject(response);
      GeneratedPlanResponse generated_plan;
      generated_plan.plan = parsed.at("plan").get<std::string>();
      if (parsed.contains("reasoning") && !parsed.at("reasoning").is_null())
        generated_plan.reasoning = parsed.at("reasoning").get<std::string>();
      if (generated_plan.reasoning.empty())
        RCLCPP_WARN(node_->get_logger(), "Planner response JSON omitted reasoning summary");
      return generated_plan;
    }
    catch (const std::exception& ex)
    {
      RCLCPP_WARN(node_->get_logger(), "Failed to parse planner response as JSON: %s", ex.what());
      return GeneratedPlanResponse{ sanitizePlanXml(response), "" };
    }
  }

  static nlohmann::json extractJsonObject(const std::string& response)
  {
    const auto fenced_start = response.find("```json");
    if (fenced_start != std::string::npos)
    {
      const auto fenced_end = response.rfind("```");
      if (fenced_end != std::string::npos && fenced_end > fenced_start + 7)
        return nlohmann::json::parse(response.substr(fenced_start + 7, fenced_end - (fenced_start + 7)));
    }

    const auto json_start = response.find('{');
    const auto json_end = response.rfind('}');
    if (json_start == std::string::npos || json_end == std::string::npos || json_end <= json_start)
      throw std::runtime_error("No JSON object found in planner response");

    return nlohmann::json::parse(response.substr(json_start, json_end - json_start + 1));
  }

  /**
   * @brief Build a critique string from a parsed draft plan and capability evidence.
    *
    * The critique feeds the refinement step with concrete parser failures and
    * evidence weaknesses instead of asking the model to guess what went wrong.
   *
   * @param plan Parsed draft plan.
   * @param context Capability evidence context used during generation.
   * @return std::string Critique string for the refinement prompt.
   */
  static std::string critiquePlan(const BehaviourPlanDefinition& plan,
                                  const std::vector<CapabilityGenerationContext>& context)
  {
    std::unordered_map<std::string, CandidateSkillResolution> resolution_by_skill;
    for (const auto& item : context)
      resolution_by_skill.emplace(planning::normalize_identifier(item.capability->interface_name), item.resolution);

    std::ostringstream critique;
    if (plan.connections.empty())
      critique << "Plan parsed but no executable runners were found. ";

    for (const auto& connection : plan.connections)
    {
      const auto skill_id = planning::normalize_identifier(connection.source.interface);
      auto it = resolution_by_skill.find(skill_id);
      if (it != resolution_by_skill.end() && it->second.fallback_level == "prior_default")
      {
        critique << "Skill " << connection.source.interface << " only has prior/default evidence. ";
      }
    }

    const auto text = critique.str();
    return text.empty() ? "No parser errors. Keep the plan concise and executable." : text;
  }

  static void appendSemanticItems(std::ostringstream& prompt, const char* label,
                                  const std::vector<SemanticContextItem>& items) 
  {
    if (items.empty())
      return;
    prompt << label << ": ";
    const size_t limit = std::min<size_t>(3, items.size());
    for (size_t index = 0; index < limit; ++index)
    {
      if (index > 0)
        prompt << "; ";
      prompt << items[index].name;
      if (!items[index].edge_strength_class.empty())
        prompt << " {strength=" << items[index].edge_strength_class << '}';
      if (!items[index].relationship.empty())
        prompt << " [" << items[index].relationship << "]";
    }
    prompt << "\n";
  }

  static void appendSemanticContext(std::ostringstream& prompt, const SemanticContextBundle& semantic_context,
                                    bool use_community_context)
  {
    if (semantic_context.relevant_capabilities.empty() && semantic_context.related_entities.empty() &&
        semantic_context.dependencies.empty() && semantic_context.plan_patterns.empty() &&
        semantic_context.fault_concepts.empty() && semantic_context.community_summaries.empty())
      return;

    prompt << "Retrieved semantic context:\n";
    appendSemanticItems(prompt, "- relevant capabilities", semantic_context.relevant_capabilities);
    if (use_community_context)
    {
      appendSemanticItems(prompt, "- community summaries", semantic_context.community_summaries);
    }
    else
    {
      appendSemanticItems(prompt, "- related entities", semantic_context.related_entities);
      appendSemanticItems(prompt, "- dependencies", semantic_context.dependencies);
      appendSemanticItems(prompt, "- plan patterns", semantic_context.plan_patterns);
      appendSemanticItems(prompt, "- fault concepts", semantic_context.fault_concepts);
      appendSemanticItems(prompt, "- community summaries", semantic_context.community_summaries);
    }
    prompt << "\n";
  }

  void logSemanticContextComparison(const std::string& detailed_prompt, const std::string& community_prompt,
                                    const SemanticContextBundle& semantic_context) const
  {
    const auto detailed_chars = detailed_prompt.size();
    const auto community_chars = community_prompt.size();
    const auto delta = static_cast<long long>(community_chars) - static_cast<long long>(detailed_chars);
    RCLCPP_INFO(node_->get_logger(),
                "[PlanningPlugin] Semantic prompt comparison: detailed=%zu chars, community=%zu chars, delta=%lld, "
                "communities=%zu, related_entities=%zu, dependencies=%zu, plan_patterns=%zu, fault_concepts=%zu",
                detailed_chars, community_chars, delta, semantic_context.community_summaries.size(),
                semantic_context.related_entities.size(), semantic_context.dependencies.size(),
                semantic_context.plan_patterns.size(), semantic_context.fault_concepts.size());
  }

  static std::string normalizedSemanticContextMode(std::string mode)
  {
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char value) {
      return static_cast<char>(std::tolower(value));
    });
    return mode == "community" ? "community" : "detailed";
  }

  static std::string sanitizePathComponent(const std::string& value)
  {
    if (value.empty())
      return "unknown";

    std::string sanitized;
    sanitized.reserve(value.size());
    for (const unsigned char ch : value)
    {
      if (std::isalnum(ch) || ch == '_' || ch == '-')
        sanitized.push_back(static_cast<char>(ch));
      else
        sanitized.push_back('_');
    }
    return sanitized;
  }

  static nlohmann::json semanticItemsToJson(const std::vector<SemanticContextItem>& items)
  {
    nlohmann::json result = nlohmann::json::array();
    for (const auto& item : items)
    {
      result.push_back({
        { "name", item.name },
        { "relationship", item.relationship },
        { "relevance", item.relevance },
        { "edge_strength_class", item.edge_strength_class },
      });
    }
    return result;
  }

  std::filesystem::path promptAuditPath(const PlanningRequest& request) const
  {
    const std::string request_id = request.planning_request_id.empty() ? promptRequestUuid(request)
                                                                       : request.planning_request_id;
    const std::string file_name = sanitizePathComponent(request_id) + "_" +
                                  std::to_string(request.recorded_at_sec) + "_" +
                                  std::to_string(request.recorded_at_nanosec) + ".json";
    return std::filesystem::path(prompt_audit_dir_) / file_name;
  }

  void persistPromptAudit(const PlanningRequest& request,
                          const std::vector<const BehaviourInfo*>& ranked_capabilities,
                          const SemanticContextBundle& semantic_context,
                          const std::string& detailed_prompt,
                          const std::string& community_prompt,
                          const std::string& selected_mode,
                          const std::string& phase,
                          const int iteration,
                          const std::string& draft_plan = "",
                          const std::string& critique = "",
                          const int refinement_attempt_index = 0) const
  {
    try
    {
      const std::filesystem::path audit_dir(prompt_audit_dir_);
      std::filesystem::create_directories(audit_dir);
      const std::filesystem::path audit_path = promptAuditPath(request);

      nlohmann::json capability_names = nlohmann::json::array();
      for (const auto* capability : ranked_capabilities)
      {
        if (capability == nullptr)
          continue;
        capability_names.push_back({
          { "interface_name", capability->interface_name },
          { "provider_name", capability->provider_name },
        });
      }

      const size_t detailed_chars = detailed_prompt.size();
      const size_t community_chars = community_prompt.size();
      const long long reduction_chars = static_cast<long long>(detailed_chars) - static_cast<long long>(community_chars);
      const double reduction_ratio = detailed_chars > 0
                                       ? static_cast<double>(reduction_chars) / static_cast<double>(detailed_chars)
                                       : 0.0;

      nlohmann::json audit;
      if (std::filesystem::exists(audit_path))
      {
        std::ifstream input(audit_path);
        if (input.good())
          input >> audit;
      }

      if (!audit.is_object())
        audit = nlohmann::json::object();

      audit["planning_request_id"] = request.planning_request_id;
      audit["request_uuid"] = promptRequestUuid(request);
      audit["task_description"] = request.task_description;
      audit["recorded_at_sec"] = request.recorded_at_sec;
      audit["recorded_at_nanosec"] = request.recorded_at_nanosec;
      if (!audit.contains("iterations") || !audit["iterations"].is_object())
        audit["iterations"] = nlohmann::json::object();

      nlohmann::json iteration_audit = {
        { "phase", phase },
        { "iteration", iteration },
        { "selected_semantic_context_mode", selected_mode },
        { "prompt_comparison", {
            { "detailed_chars", detailed_chars },
            { "community_chars", community_chars },
            { "reduction_chars", reduction_chars },
            { "reduction_ratio", reduction_ratio },
          } },
        { "semantic_counts", {
            { "relevant_capabilities", semantic_context.relevant_capabilities.size() },
            { "related_entities", semantic_context.related_entities.size() },
            { "dependencies", semantic_context.dependencies.size() },
            { "plan_patterns", semantic_context.plan_patterns.size() },
            { "fault_concepts", semantic_context.fault_concepts.size() },
            { "community_summaries", semantic_context.community_summaries.size() },
          } },
        { "candidate_capabilities", capability_names },
        { "semantic_context", {
            { "relevant_capabilities", semanticItemsToJson(semantic_context.relevant_capabilities) },
            { "related_entities", semanticItemsToJson(semantic_context.related_entities) },
            { "dependencies", semanticItemsToJson(semantic_context.dependencies) },
            { "plan_patterns", semanticItemsToJson(semantic_context.plan_patterns) },
            { "fault_concepts", semanticItemsToJson(semantic_context.fault_concepts) },
            { "community_summaries", semanticItemsToJson(semantic_context.community_summaries) },
          } },
      };

      if (phase == "refinement")
      {
        iteration_audit["refinement_context"] = {
          { "attempt_index", refinement_attempt_index },
          { "draft_plan_chars", draft_plan.size() },
          { "critique_chars", critique.size() },
        };
      }

      if (prompt_audit_include_full_text_)
      {
        iteration_audit["prompts"] = {
          { "detailed", detailed_prompt },
          { "community", community_prompt },
        };
        if (phase == "refinement")
        {
          iteration_audit["refinement_context"]["draft_plan"] = draft_plan;
          iteration_audit["refinement_context"]["critique"] = critique;
        }
      }

      audit["iterations"]["iteration_" + std::to_string(iteration)] = iteration_audit;

      std::ofstream stream(audit_path);
      stream << std::setw(2) << audit << '\n';
    }
    catch (const std::exception& ex)
    {
      RCLCPP_WARN(node_->get_logger(), "Failed to persist prompt audit artifact: %s", ex.what());
    }
  }

  /**
   * @brief Parse a generated plan into the candidate-plan graph representation.
    *
    * Converting XML into normalized nodes and edges gives the scorer and storage
    * layers a stable graph shape independent of the original prompt response.
   *
   * @param request Planning request being processed.
   * @param plan_xml Sanitized plan XML.
   * @param context Capability evidence context used during generation.
   * @param core Core plugin used to resolve skill evidence.
   * @param behaviour_interface Behaviour interface used to parse the XML plan.
   * @return CandidatePlan Parsed candidate plan.
   */
  CandidatePlan parseCandidatePlan(const PlanningRequest& request, const std::string& plan_xml,
                                   const std::vector<CapabilityGenerationContext>& context, CoreBase& core,
                                   BehaviourInterfaceBase& behaviour_interface) const
  {
    const auto parsed_plan = behaviour_interface.parseBehaviourPlan(plan_xml);
    if (!parsed_plan.success)
      throw std::runtime_error(parsed_plan.error);

    CandidatePlan candidate;
    candidate.candidate_plan_id = request.planning_request_id + "/candidate_0";
    candidate.plan = plan_xml;

    std::unordered_map<std::string, const CapabilityGenerationContext*> context_by_skill;
    for (const auto& item : context)
      context_by_skill.emplace(planning::normalize_identifier(item.capability->interface_name), &item);

    std::unordered_map<int, std::string> node_id_by_instance;
    for (const auto& connection : parsed_plan.connections)
    {
      if (isInternalSkill(connection.source.interface))
        continue;
      const int instance_id = connection.source.instance_id;
      if (node_id_by_instance.count(instance_id) == 0)
      {
        CandidatePlanNode node;
        node.candidate_node_id = "runner_" + std::to_string(instance_id);
        node.runtime_instance_id = instance_id;
        node.capability_name = connection.source.interface;
        node.provider_name = connection.source.provider;
        node.capability_id = planning::normalize_identifier(connection.source.interface);
        auto it = context_by_skill.find(node.capability_id);
        node.context_key =
          it != context_by_skill.end() ? it->second->context_key : planning::build_context_key(request.task_description);
        node.context_class = it != context_by_skill.end() ? it->second->context_class
                                                          : planning::build_context_class(request.task_description);
        node.resolution = core.resolveCandidateSkill(CandidateSkillRequest{
          request.planning_request_id, candidate.candidate_plan_id, "", node.candidate_node_id,
            node.capability_name, node.capability_id, node.context_key, node.context_class });
        candidate.nodes.push_back(node);
        node_id_by_instance.emplace(instance_id, node.candidate_node_id);
      }
    }

    for (const auto& connection : parsed_plan.connections)
    {
      appendEdge(candidate, node_id_by_instance, connection.source, connection.on_success, "on_success");
      appendEdge(candidate, node_id_by_instance, connection.source, connection.on_failure, "on_failure");
      appendEdge(candidate, node_id_by_instance, connection.source, connection.on_start, "on_start");
      appendEdge(candidate, node_id_by_instance, connection.source, connection.on_stop, "on_stop");
    }

    candidate.graph_hash = planning::hash_candidate_plan(candidate);
    return candidate;
  }

  /**
   * @brief Append a graph edge when both referenced plan instances are executable skills.
    *
    * Internal Fabric helper nodes are filtered out here so later scoring only
    * reflects user-relevant behaviour transitions.
   *
   * @param candidate Candidate plan being constructed.
   * @param node_id_by_instance Mapping from Fabric instance IDs to candidate-node IDs.
   * @param source Source skill reference.
   * @param target Target skill reference.
   * @param label Edge label describing the transition.
   */
  static void appendEdge(CandidatePlan& candidate, const std::unordered_map<int, std::string>& node_id_by_instance,
                         const SkillInfo& source, const SkillInfo& target, const std::string& label)
  {
    if (!target.exists())
      return;
    auto source_it = node_id_by_instance.find(source.instance_id);
    auto target_it = node_id_by_instance.find(target.instance_id);
    if (source_it == node_id_by_instance.end() || target_it == node_id_by_instance.end())
      return;

    CandidatePlanEdge edge;
    edge.source_node_id = source_it->second;
    edge.target_node_id = target_it->second;
    if (isInternalSkill(source.interface) || isInternalSkill(target.interface))
      return;
    edge.source_capability_id = planning::normalize_identifier(source.interface);
    edge.target_capability_id = planning::normalize_identifier(target.interface);
    edge.edge_label = label;
    candidate.edges.push_back(edge);
  }

  /**
   * @brief Determine whether a skill name refers to an internal Fabric helper skill.
    *
    * These helper skills exist to complete orchestration and should not distort
    * plan evidence, graph shape, or final RAPFS scoring.
   *
   * @param skill_name Skill name to inspect.
   * @return bool True when the skill should be excluded from candidate scoring.
   */
  static bool isInternalSkill(const std::string& capability_name)
  {
    return capability_name == "fabric_capabilities/FabricCompletionRunner";
  }

  /**
   * @brief Persist initial attribution records for a generated candidate plan.
    *
    * Persisting these rows before execution preserves which evidence row and
    * graph structure were selected, even if runtime feedback arrives much later.
   *
   * @param request Planning request being processed.
   * @param candidate Candidate plan whose nodes and edges should be persisted.
   * @param core Core plugin used to store attribution records.
   */
  void persistCandidateAttribution(const PlanningRequest& request, const CandidatePlan& candidate,
                                   CoreBase& core) const
  {
    for (const auto& node : candidate.nodes)
    {
      core.recordCandidatePlanSkillAttribution(CandidatePlanSkillAttribution{
          request.planning_request_id, candidate.candidate_plan_id, candidate.graph_hash,
          node.candidate_node_id, "", std::to_string(node.runtime_instance_id),
          node.capability_name, node.capability_id, node.context_key, node.context_class,
          node.resolution.selected_skill_id, node.resolution.selected_key, node.resolution.estimator_window,
          node.resolution.fallback_level, node.resolution.proxy_source, false, false, "", request.recorded_at_sec,
          request.recorded_at_nanosec, node.resolution.reason });
    }

    for (const auto& edge : candidate.edges)
    {
        core.recordCandidatePlanEdgeAttribution(CandidatePlanEdgeAttribution{
          request.planning_request_id, candidate.candidate_plan_id, candidate.graph_hash,
          edge.source_node_id, edge.target_node_id, edge.source_capability_id, edge.target_capability_id,
          edge.edge_label, false, false, "", request.recorded_at_sec, request.recorded_at_nanosec,
          "parsed from PromptTools-generated plan graph" });
    }
  }

  rclcpp::Client<Prompt>::SharedPtr prompt_client_;
  std::string model_family_;
  std::string model_;
  bool use_cache_ = true;
  bool use_chat_mode_ = false;
  int max_capability_context_ = 8;
  std::string semantic_context_mode_ = "detailed";
  bool log_semantic_context_comparison_ = true;
  bool persist_prompt_audit_ = true;
  std::string prompt_audit_dir_ = "planner_prompt_audit";
  bool prompt_audit_include_full_text_ = false;
};
}  // namespace fabric