#include <pluginlib/class_list_macros.hpp>
#include <fabric_experience_estimator/energy_plugin.hpp>
#include <fabric_experience_estimator/fault_burden_plugin.hpp>
#include <fabric_experience_estimator/resource_plugin.hpp>
#include <fabric_experience_estimator/runtime_plugin.hpp>
#include <fabric_experience_estimator/success_probability_plugin.hpp>
#include <fabric_experience_estimator/uncertainity_plugin.hpp>

PLUGINLIB_EXPORT_CLASS(fabric::EnergyPlugin, fabric::EstimateBase)
PLUGINLIB_EXPORT_CLASS(fabric::FaultBurdenPlugin, fabric::EstimateBase)
PLUGINLIB_EXPORT_CLASS(fabric::ResourcePlugin, fabric::EstimateBase)
PLUGINLIB_EXPORT_CLASS(fabric::RuntimePlugin, fabric::EstimateBase)
PLUGINLIB_EXPORT_CLASS(fabric::SuccessProbabilityPlugin, fabric::EstimateBase)
PLUGINLIB_EXPORT_CLASS(fabric::UncertaintyPlugin, fabric::EstimateBase)