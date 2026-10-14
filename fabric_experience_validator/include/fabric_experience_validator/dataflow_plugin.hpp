#pragma once

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <fabric_base/validation_base.hpp>

namespace fabric
{

using ValidationBase = ::fabric::ValidationBase;
using PlanningRequest = ::fabric::planning::Request;
using BehaviourInfo = ::fabric::capability::Descriptor;
using CandidatePlan = ::fabric::planning::CandidatePlan;
using CandidatePlanNode = ::fabric::planning::CandidatePlanNode;
using Parameter = ::fabric::capability::ParameterInfo;

/**
 * @brief Validate required runtime inputs against upstream outputs and declared fallback paths.
 */
class DataflowValidation : public ValidationBase
{
public:
  void validate(fabric::plan::Plan& plan, std::vector<fabric::capability::Info>& capabilities) override
  {
    (void)plan;
    (void)capabilities;
  }

  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "DataflowValidation");
    node_->declare_parameter<double>(plugin_name_ + ".weak_match_penalty_scale", 0.20);
    node_->declare_parameter<double>(plugin_name_ + ".external_penalty", 0.08);
    node_->declare_parameter<double>(plugin_name_ + ".configuration_penalty", 0.05);
    node_->declare_parameter<double>(plugin_name_ + ".default_penalty", 0.03);
    node_->declare_parameter<double>(plugin_name_ + ".partial_coverage_penalty", 0.05);
    weak_match_penalty_scale_ = node_->get_parameter(plugin_name_ + ".weak_match_penalty_scale").as_double();
    external_penalty_ = node_->get_parameter(plugin_name_ + ".external_penalty").as_double();
    configuration_penalty_ = node_->get_parameter(plugin_name_ + ".configuration_penalty").as_double();
    default_penalty_ = node_->get_parameter(plugin_name_ + ".default_penalty").as_double();
    partial_coverage_penalty_ = node_->get_parameter(plugin_name_ + ".partial_coverage_penalty").as_double();
  }

  void validate(const PlanningRequest& request, const std::vector<BehaviourInfo>& capabilities,
                CandidatePlan& candidate) const override
  {
    (void)request;

    std::unordered_map<std::string, const BehaviourInfo*> capability_by_node;
    std::unordered_map<std::string, const CandidatePlanNode*> node_by_id;
    std::unordered_map<std::string, size_t> node_order;
    for (size_t index = 0; index < candidate.nodes.size(); ++index)
    {
      const auto& node = candidate.nodes[index];
      node_by_id.emplace(node.candidate_node_id, &node);
      node_order.emplace(node.candidate_node_id, index);
      const auto* capability = findCapability(capabilities, node.capability_name, node.provider_name);
      if (capability != nullptr)
        capability_by_node.emplace(node.candidate_node_id, capability);
    }

    std::unordered_map<std::string, std::vector<std::string>> incoming_sources;
    for (const auto& edge : candidate.edges)
      incoming_sources[edge.target_node_id].push_back(edge.source_node_id);

    for (const auto& node : candidate.nodes)
    {
      const auto capability_it = capability_by_node.find(node.candidate_node_id);
      if (capability_it == capability_by_node.end())
        continue;

      const BehaviourInfo& capability = *capability_it->second;
      const auto incoming_it = incoming_sources.find(node.candidate_node_id);
      const std::vector<std::string> no_sources;
      const auto& sources = incoming_it == incoming_sources.end() ? no_sources : incoming_it->second;
      validateRuntimeInputs(candidate, node, capability, capability_by_node, node_by_id, node_order, sources);
      if (candidate.validation.rejected)
        return;
    }
  }

private:
  struct ParameterMatch
  {
    bool matched = false;
    std::string source_node_id;
    std::string source_parameter_name;
    std::string identity_kind;
    double identity_score = 0.0;
    double type_score = 0.0;
    double score = 0.0;
    bool source_order_valid = true;
  };

  void validateRuntimeInputs(CandidatePlan& candidate, const CandidatePlanNode& node, const BehaviourInfo& capability,
                             const std::unordered_map<std::string, const BehaviourInfo*>& capability_by_node,
                             const std::unordered_map<std::string, const CandidatePlanNode*>& node_by_id,
                             const std::unordered_map<std::string, size_t>& node_order,
                             const std::vector<std::string>& incoming_sources) const
  {
    int required_inputs = 0;
    int structurally_satisfied = 0;

    for (const auto& input : capability.runtime_input_parameters)
    {
      if (!input.required)
        continue;

      ++required_inputs;
      const auto satisfiable_from = effectiveSatisfiableSources(input);
      const ParameterMatch upstream_match = bestUpstreamMatch(input, capability_by_node, node_order,
                                                             node.candidate_node_id, incoming_sources);

      if (upstream_match.matched && !upstream_match.source_order_valid)
      {
        std::ostringstream message;
        message << "runtime input '" << input.name << "' depends on source node " << upstream_match.source_node_id
                << " which appears after target node " << node.candidate_node_id;
        addReject(candidate, message.str(), node.candidate_node_id, input.name, upstream_match.source_node_id,
                  node.candidate_node_id);
        return;
      }

      if (upstream_match.matched)
      {
        ++structurally_satisfied;
        if (upstream_match.score < 0.999)
        {
          const double penalty = std::clamp(weak_match_penalty_scale_ * (1.0 - upstream_match.score), 0.02, 0.15);
          std::ostringstream message;
          message << "runtime input '" << input.name << "' is satisfied only by a weak " << upstream_match.identity_kind
                  << " match from node " << upstream_match.source_node_id;
          addWarning(candidate, message.str(), node.candidate_node_id, input.name, upstream_match.source_node_id,
                     node.candidate_node_id);
          addPenalty(candidate, penalty, message.str(), node.candidate_node_id, input.name,
                     upstream_match.source_node_id, node.candidate_node_id);
        }
        continue;
      }

      if (allowsSource(satisfiable_from, "configuration"))
      {
        const Parameter* config_parameter = supportingConfigurationParameter(input, capability);
        if (config_parameter != nullptr)
        {
          std::ostringstream message;
          message << "runtime input '" << input.name << "' relies on local configuration parameter '"
                  << config_parameter->name << "'";
          addWarning(candidate, message.str(), node.candidate_node_id, input.name);
          addPenalty(candidate, configuration_penalty_, message.str(), node.candidate_node_id, input.name);
          continue;
        }
      }

      if (allowsSource(satisfiable_from, "default") && hasDefaultSupport(input, capability))
      {
        std::ostringstream message;
        message << "runtime input '" << input.name << "' falls back to a declared default value";
        addWarning(candidate, message.str(), node.candidate_node_id, input.name);
        addPenalty(candidate, default_penalty_, message.str(), node.candidate_node_id, input.name);
        continue;
      }

      if (allowsSource(satisfiable_from, "external"))
      {
        std::ostringstream message;
        message << "runtime input '" << input.name << "' requires external injection";
        addWarning(candidate, message.str(), node.candidate_node_id, input.name);
        addPenalty(candidate, external_penalty_, message.str(), node.candidate_node_id, input.name);
        continue;
      }

      std::ostringstream message;
      message << "required runtime input '" << input.name << "' has no allowed upstream, configuration, external, or default source";
      addReject(candidate, message.str(), node.candidate_node_id, input.name);
      return;
    }

    if (required_inputs > 0 && structurally_satisfied < required_inputs)
    {
      const double missing_ratio = static_cast<double>(required_inputs - structurally_satisfied) /
                                   static_cast<double>(required_inputs);
      if (missing_ratio > 0.0)
      {
        std::ostringstream message;
        message << "node '" << node.candidate_node_id << "' has structural upstream coverage for "
                << structurally_satisfied << '/' << required_inputs << " required runtime inputs";
        addPenalty(candidate, partial_coverage_penalty_ * missing_ratio, message.str(), node.candidate_node_id);
      }
    }

    (void)node_by_id;
  }

  ParameterMatch bestUpstreamMatch(const Parameter& input,
                                   const std::unordered_map<std::string, const BehaviourInfo*>& capability_by_node,
                                   const std::unordered_map<std::string, size_t>& node_order,
                                   const std::string& target_node_id,
                                   const std::vector<std::string>& incoming_sources) const
  {
    if (!allowsSource(effectiveSatisfiableSources(input), "upstream"))
      return {};

    ParameterMatch best_match;
    for (const auto& source_node_id : incoming_sources)
    {
      const auto capability_it = capability_by_node.find(source_node_id);
      if (capability_it == capability_by_node.end())
        continue;

      const auto source_order_it = node_order.find(source_node_id);
      const auto target_order_it = node_order.find(target_node_id);
      const bool source_order_valid = source_order_it == node_order.end() || target_order_it == node_order.end() ||
                                      source_order_it->second < target_order_it->second;

      for (const auto& output : capability_it->second->runtime_output_parameters)
      {
        const auto [identity_kind, identity_score] = parameterIdentityScore(input, output);
        const double type_score = parameterTypeScore(input.type, output.type);
        const double score = identity_score * type_score;
        if (score <= 0.0 || score <= best_match.score)
          continue;

        best_match.matched = true;
        best_match.source_node_id = source_node_id;
        best_match.source_parameter_name = output.name;
        best_match.identity_kind = identity_kind;
        best_match.identity_score = identity_score;
        best_match.type_score = type_score;
        best_match.score = score;
        best_match.source_order_valid = source_order_valid;
      }
    }

    return best_match;
  }

  static std::vector<std::string> effectiveSatisfiableSources(const Parameter& parameter)
  {
    if (!parameter.satisfiable_from.empty())
      return parameter.satisfiable_from;
    return { "upstream" };
  }

  static bool allowsSource(const std::vector<std::string>& sources, const std::string& source)
  {
    const std::string needle = lower(source);
    for (const auto& item : sources)
    {
      if (lower(item) == needle)
        return true;
    }
    return false;
  }

  static const BehaviourInfo* findCapability(const std::vector<BehaviourInfo>& capabilities,
                                             const std::string& interface_name,
                                             const std::string& provider_name)
  {
    for (const auto& capability : capabilities)
    {
      if (capability.interface_name == interface_name && capability.provider_name == provider_name)
        return &capability;
    }
    return nullptr;
  }

  static const Parameter* supportingConfigurationParameter(const Parameter& input, const BehaviourInfo& capability)
  {
    if (!input.fallback_parameter.empty())
    {
      for (const auto& parameter : capability.configuration_parameters)
      {
        if (parameter.name == input.fallback_parameter)
          return &parameter;
      }
    }

    for (const auto& parameter : capability.configuration_parameters)
    {
      const auto [identity_kind, identity_score] = parameterIdentityScore(input, parameter);
      if (identity_score > 0.0 && identity_kind != "alias")
        return &parameter;
    }
    return nullptr;
  }

  static bool hasDefaultSupport(const Parameter& input, const BehaviourInfo& capability)
  {
    if (input.has_default)
      return true;

    const Parameter* config_parameter = supportingConfigurationParameter(input, capability);
    return config_parameter != nullptr && config_parameter->has_default;
  }

  static std::pair<std::string, double> parameterIdentityScore(const Parameter& left, const Parameter& right)
  {
    const std::string left_semantic = lower(left.semantic_key);
    const std::string right_semantic = lower(right.semantic_key);
    if (!left_semantic.empty() && left_semantic == right_semantic)
      return { "semantic_key", 1.0 };

    const std::string left_name = lower(left.name);
    const std::string right_name = lower(right.name);
    if (!left_name.empty() && left_name == right_name)
      return { "name", 0.85 };

    std::unordered_set<std::string> left_aliases;
    for (const auto& alias : left.aliases)
      left_aliases.insert(lower(alias));
    if (!left_name.empty())
      left_aliases.insert(left_name);
    if (!left_semantic.empty())
      left_aliases.insert(left_semantic);

    const auto matches_alias = [&left_aliases](const std::string& value) {
      return !value.empty() && left_aliases.count(lower(value)) > 0;
    };

    if (matches_alias(right.name) || matches_alias(right.semantic_key))
      return { "alias", 0.60 };
    for (const auto& alias : right.aliases)
    {
      if (matches_alias(alias))
        return { "alias", 0.60 };
    }

    return { "", 0.0 };
  }

  static double parameterTypeScore(const std::string& left_type, const std::string& right_type)
  {
    const std::string left = lower(left_type);
    const std::string right = lower(right_type);
    if (left.empty() || right.empty())
      return 0.75;
    return left == right ? 1.0 : 0.0;
  }

  static std::string lower(const std::string& value)
  {
    std::string lowered;
    lowered.reserve(value.size());
    for (const unsigned char ch : value)
      lowered.push_back(static_cast<char>(std::tolower(ch)));
    return lowered;
  }

  double weak_match_penalty_scale_ = 0.20;
  double external_penalty_ = 0.08;
  double configuration_penalty_ = 0.05;
  double default_penalty_ = 0.03;
  double partial_coverage_penalty_ = 0.05;
};

}  // namespace fabric