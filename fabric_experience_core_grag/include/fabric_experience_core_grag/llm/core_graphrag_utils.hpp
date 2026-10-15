#pragma once

#include <string>

#include <fabric_base/utils/metadata_structs.hpp>
#include <fabric_experience_core_grag/database/core_graphrag_utils.hpp>
#include <fabric_experience_core_grag/database/structs.hpp>

namespace fabric::experience::llm::core_graphrag
{
using BehaviourInfo = ::fabric::capability::Descriptor;

inline std::string node_description(const BehaviourInfo& capability)
{
  if (capability.interface_description.empty())
    return capability.provider_description;
  if (capability.provider_description.empty())
    return capability.interface_description;
  return capability.interface_description + "\n" + capability.provider_description;
}

inline experience::database::NodeType nodeTypeFromExtractionLabel(const std::string& type)
{
  const std::string normalized = experience::database::core_graphrag::normalize_id_segment(type);

  if (normalized == "capability")
    return experience::database::NodeType::CAPABILITY;
  if (normalized == "task_entity" || normalized == "taskentity" || normalized == "task")
    return experience::database::NodeType::TASK_ENTITY;
  if (normalized == "context_entity" || normalized == "contextentity" || normalized == "context" ||
      normalized == "persona" || normalized == "object" || normalized == "place")
    return experience::database::NodeType::CONTEXT_ENTITY;
  if (normalized == "plan_pattern" || normalized == "planpattern" || normalized == "pattern")
    return experience::database::NodeType::PLAN_PATTERN;
  if (normalized == "fault_concept" || normalized == "faultconcept" || normalized == "fault")
    return experience::database::NodeType::FAULT_CONCEPT;

  return experience::database::NodeType::CONTEXT_ENTITY;
}
}  // namespace fabric::experience::llm::core_graphrag
