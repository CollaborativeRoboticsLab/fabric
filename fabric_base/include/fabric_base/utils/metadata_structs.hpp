#pragma once

#include <string>
#include <vector>

#include <fabric_base/utils/plan_structs.hpp>

namespace fabric
{
namespace capability
{
struct ParameterInfo
{
  std::string name;
  std::string type;
  std::string description;
  std::string semantic_key;
  bool required = false;
  std::vector<std::string> satisfiable_from;
  std::string fallback_parameter;
  std::vector<std::string> aliases;
  bool has_default = false;
  std::string default_value;
};

struct ProviderInfo
{
  std::string provider;
  std::vector<ParameterInfo> configuration_parameters;
  std::vector<ParameterInfo> runtime_input_parameters;
  std::vector<ParameterInfo> runtime_output_parameters;
};

struct Info
{
  std::string interface;
  std::string provider;
  bool is_semantic;
  std::vector<std::string> alt_providers;
  std::vector<ProviderInfo> provider_details;
};

struct Relation
{
  std::string subject;
  std::string predicate;
  std::string object;
};

struct Descriptor
{
  std::string interface_name;
  std::string interface_description;
  std::string provider_name;
  std::string provider_description;
  std::vector<Relation> relations;
  std::vector<ParameterInfo> configuration_parameters;
  std::vector<ParameterInfo> runtime_input_parameters;
  std::vector<ParameterInfo> runtime_output_parameters;
  std::string command;
};

struct PlanDefinition
{
  bool success = false;
  std::string error;
  std::vector<fabric::plan::connection> connections;
};

}  // namespace capability

}  // namespace fabric

