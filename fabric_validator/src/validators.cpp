#include <fabric_validator/compatibility_plugin.hpp>
#include <fabric_validator/parameter_plugin.hpp>
#include <pluginlib/class_list_macros.hpp>

PLUGINLIB_EXPORT_CLASS(fabric::CompatibilityValidation, fabric::ValidationBase)
PLUGINLIB_EXPORT_CLASS(fabric::ParameterValidation, fabric::ValidationBase)