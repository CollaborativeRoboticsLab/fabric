#pragma once

#include <algorithm>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <rclcpp/rclcpp.hpp>
#include <fabric_base/validation_base.hpp>

namespace fabric
{
/**
 * @brief Validation plugin that checks interface/provider compatibility and runtime data-flow coverage.
 *
 * This plugin validates that each plan node references a valid provider and that every required
 * runtime input can be satisfied by upstream outputs, explicit plan values, configuration fallbacks,
 * or declared defaults.
 */
class ParameterValidation : public ValidationBase
{
public:
  ParameterValidation() = default;
  virtual ~ParameterValidation() = default;

  /**
   * @brief Initialize the syntax validation plugin.
   */
  void initialize(const rclcpp::Node::SharedPtr& node) override
  {
    initialize_base(node, "ParameterValidationPlugin");
  }

  /**
   * @brief Validate the given XML plan for compatibility.
   *
   * @param plan The fabric::Plan to validate.
   * @param capabilities Support data for evaluation as a vector of fabric::CapabilityInfo.
   */
  void validate(fabric::Plan& plan, std::vector<CapabilityInfo>& capabilities) override
  {
    for (const auto& [id, connection] : plan.connections)
    {
      (void)id;

      const CapabilityProviderInfo* provider_info = find_provider_info(connection.source, capabilities);
      if (provider_info == nullptr)
      {
        throw fabric::fabric_exception("Unmatched interface: " + connection.source.interface + " provider: " + connection.source.provider +
                                       " combination");
      }

      for (const auto& runtime_input : provider_info->runtime_input_parameters)
      {
        if (!runtime_input.required)
        {
          continue;
        }

        if (is_runtime_input_satisfied(plan, connection.source, *provider_info, runtime_input, capabilities))
        {
          continue;
        }

        std::ostringstream error;
        error << "Unsatisfied required runtime input '" << runtime_input.name << "' for capability " << connection.source.interface << " provider "
              << connection.source.provider;
        if (!runtime_input.semantic_key.empty())
        {
          error << " (semantic_key=" << runtime_input.semantic_key << ")";
        }
        throw fabric::fabric_exception(error.str());
      }
    }
  }

private:
  static const CapabilityInfo* find_capability(const fabric::node& plan_node, const std::vector<CapabilityInfo>& capabilities)
  {
    for (const auto& capability : capabilities)
    {
      if (capability.interface != plan_node.interface)
      {
        continue;
      }

      if (capability.provider == plan_node.provider)
      {
        return &capability;
      }

      if (std::find(capability.alt_providers.begin(), capability.alt_providers.end(), plan_node.provider) != capability.alt_providers.end())
      {
        return &capability;
      }
    }

    return nullptr;
  }

  static const CapabilityProviderInfo* find_provider_details(const CapabilityInfo& capability, const std::string& provider_name)
  {
    for (const auto& provider_details : capability.provider_details)
    {
      if (provider_details.provider == provider_name)
      {
        return &provider_details;
      }
    }

    return nullptr;
  }

  static const CapabilityProviderInfo* find_provider_info(const fabric::node& plan_node, const std::vector<CapabilityInfo>& capabilities)
  {
    const CapabilityInfo* capability = find_capability(plan_node, capabilities);
    if (capability == nullptr)
    {
      return nullptr;
    }

    return find_provider_details(*capability, plan_node.provider);
  }

  static bool source_allowed(const CapabilityParameterInfo& parameter, const std::string& source)
  {
    if (parameter.satisfiable_from.empty())
    {
      return source == "upstream";
    }

    return std::find(parameter.satisfiable_from.begin(), parameter.satisfiable_from.end(), source) != parameter.satisfiable_from.end();
  }

  static bool type_compatible(const CapabilityParameterInfo& output, const CapabilityParameterInfo& input)
  {
    return output.type.empty() || input.type.empty() || output.type == input.type;
  }

  static std::set<std::string> parameter_keys(const CapabilityParameterInfo& parameter)
  {
    std::set<std::string> keys;

    if (!parameter.name.empty())
    {
      keys.insert(parameter.name);
    }

    if (!parameter.semantic_key.empty())
    {
      keys.insert(parameter.semantic_key);
    }

    for (const auto& alias : parameter.aliases)
    {
      if (!alias.empty())
      {
        keys.insert(alias);
      }
    }

    return keys;
  }

  static bool parameters_match(const CapabilityParameterInfo& output, const CapabilityParameterInfo& input)
  {
    if (!type_compatible(output, input))
    {
      return false;
    }

    const std::set<std::string> output_keys = parameter_keys(output);
    const std::set<std::string> input_keys = parameter_keys(input);

    for (const auto& key : output_keys)
    {
      if (!key.empty() && input_keys.find(key) != input_keys.end())
      {
        return true;
      }
    }

    return false;
  }

  static bool has_explicit_plan_value(const fabric::node& plan_node, const CapabilityParameterInfo& parameter)
  {
    if (plan_node.parameters.has_value(parameter.name))
    {
      return true;
    }

    for (const auto& alias : parameter.aliases)
    {
      if (plan_node.parameters.has_value(alias))
      {
        return true;
      }
    }

    return false;
  }

  static const CapabilityParameterInfo* find_configuration_parameter(const CapabilityProviderInfo& provider_info,
                                                                     const CapabilityParameterInfo& runtime_input)
  {
    const std::string fallback_name = runtime_input.fallback_parameter.empty() ? runtime_input.name : runtime_input.fallback_parameter;
    if (fallback_name.empty())
    {
      return nullptr;
    }

    for (const auto& parameter : provider_info.configuration_parameters)
    {
      if (parameter.name == fallback_name)
      {
        return &parameter;
      }
    }

    return nullptr;
  }

  static bool has_configuration_fallback(const fabric::node& plan_node, const CapabilityProviderInfo& provider_info,
                                         const CapabilityParameterInfo& runtime_input)
  {
    const CapabilityParameterInfo* config_parameter = find_configuration_parameter(provider_info, runtime_input);
    if (config_parameter == nullptr)
    {
      return false;
    }

    if (plan_node.parameters.has_value(config_parameter->name))
    {
      return true;
    }

    return config_parameter->has_default;
  }

  static bool has_local_default(const CapabilityProviderInfo& provider_info, const CapabilityParameterInfo& runtime_input)
  {
    if (runtime_input.has_default)
    {
      return true;
    }

    const CapabilityParameterInfo* config_parameter = find_configuration_parameter(provider_info, runtime_input);
    return config_parameter != nullptr && config_parameter->has_default;
  }

  static bool nodes_match(const fabric::node& lhs, const fabric::node& rhs)
  {
    if (lhs.interface != rhs.interface || lhs.provider != rhs.provider)
    {
      return false;
    }

    if (lhs.instance_id >= 0 && rhs.instance_id >= 0)
    {
      return lhs.instance_id == rhs.instance_id;
    }

    return true;
  }

  static std::vector<fabric::node> find_upstream_nodes(const fabric::Plan& plan, const fabric::node& target)
  {
    std::vector<fabric::node> upstream_nodes;

    for (const auto& [id, connection] : plan.connections)
    {
      (void)id;
      if ((connection.on_start.exists() && nodes_match(connection.on_start, target)) ||
          (connection.on_stop.exists() && nodes_match(connection.on_stop, target)) ||
          (connection.on_success.exists() && nodes_match(connection.on_success, target)) ||
          (connection.on_failure.exists() && nodes_match(connection.on_failure, target)))
      {
        upstream_nodes.push_back(connection.source);
      }
    }

    return upstream_nodes;
  }

  static bool has_upstream_match(const fabric::Plan& plan, const fabric::node& target, const CapabilityParameterInfo& runtime_input,
                                 const std::vector<CapabilityInfo>& capabilities)
  {
    const std::vector<fabric::node> upstream_nodes = find_upstream_nodes(plan, target);
    for (const auto& upstream_node : upstream_nodes)
    {
      const CapabilityProviderInfo* upstream_provider_info = find_provider_info(upstream_node, capabilities);
      if (upstream_provider_info == nullptr)
      {
        continue;
      }

      for (const auto& runtime_output : upstream_provider_info->runtime_output_parameters)
      {
        if (parameters_match(runtime_output, runtime_input))
        {
          return true;
        }
      }
    }

    return false;
  }

  static bool is_runtime_input_satisfied(const fabric::Plan& plan, const fabric::node& plan_node, const CapabilityProviderInfo& provider_info,
                                         const CapabilityParameterInfo& runtime_input, const std::vector<CapabilityInfo>& capabilities)
  {
    if (source_allowed(runtime_input, "upstream") && has_upstream_match(plan, plan_node, runtime_input, capabilities))
    {
      return true;
    }

    if ((source_allowed(runtime_input, "external") || source_allowed(runtime_input, "configuration")) &&
        has_explicit_plan_value(plan_node, runtime_input))
    {
      return true;
    }

    if (source_allowed(runtime_input, "configuration") && has_configuration_fallback(plan_node, provider_info, runtime_input))
    {
      return true;
    }

    if (source_allowed(runtime_input, "default") && has_local_default(provider_info, runtime_input))
    {
      return true;
    }

    return false;
  }
};
}  // namespace fabric
