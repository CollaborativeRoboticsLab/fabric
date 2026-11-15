#include <pluginlib/class_list_macros.hpp>

#include <fabric_experience_validator/compatibility_plugin.hpp>
#include <fabric_experience_validator/dataflow_plugin.hpp>
#include <fabric_experience_validator/dependency_plugin.hpp>
#include <fabric_experience_validator/semantic_plugin.hpp>

PLUGINLIB_EXPORT_CLASS(fabric::ExtendedCompatibilityValidation, fabric::ValidationBase)
PLUGINLIB_EXPORT_CLASS(fabric::DependencyValidation, fabric::ValidationBase)
PLUGINLIB_EXPORT_CLASS(fabric::DataflowValidation, fabric::ValidationBase)
PLUGINLIB_EXPORT_CLASS(fabric::SemanticValidation, fabric::ValidationBase)