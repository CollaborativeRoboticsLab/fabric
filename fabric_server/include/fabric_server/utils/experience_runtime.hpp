#pragma once

#include <filesystem>
#include <string>

#include <rclcpp/rclcpp.hpp>

namespace fabric::server::experience_runtime
{
inline bool isCoreGraphRagPlugin(const std::string & plugin_name)
{
  return plugin_name == "experience::CoreGraphRag" ||
         plugin_name == "experience::GraphKnowledgeCore" ||
         plugin_name == "fabric::CoreGraphRag" || plugin_name == "fabric::GraphKnowledgeCore";
}

inline std::string parameterName(const std::string & suffix)
{
  return "experience." + suffix;
}

inline std::string unqualifiedPluginName(const std::string & plugin_name)
{
  const std::size_t separator = plugin_name.rfind("::");
  if (separator == std::string::npos) {
    return plugin_name;
  }

  return plugin_name.substr(separator + 2);
}

inline std::string pluginParameterNamespace(const std::string & plugin_name)
{
  return parameterName(unqualifiedPluginName(plugin_name));
}

inline std::string validationParameterName(const bool use_experience)
{
  return use_experience ? parameterName("validation_plugins") : std::string("validation_plugins");
}

inline bool hasPersistedCoreDb(const std::filesystem::path & core_db_path)
{
  return std::filesystem::exists(core_db_path);
}

inline void removePersistedCoreDbIfSupported(
  const rclcpp::Logger & logger,
  const std::string & core_plugin_name,
  const std::filesystem::path & core_db_path)
{
  if (!isCoreGraphRagPlugin(core_plugin_name)) {
    RCLCPP_WARN(logger,
        "rebuild=true does not know how to clear persisted state for core plugin: %s",
        core_plugin_name.c_str());
    return;
  }

  if (!hasPersistedCoreDb(core_db_path)) {
    return;
  }

  std::error_code error_code;
  if (!std::filesystem::remove(core_db_path, error_code) && error_code) {
    throw std::runtime_error("Failed to delete persisted core database: " +
          core_db_path.string() + " " + error_code.message());
  }

  RCLCPP_INFO(logger, "Removed persisted core database: %s", core_db_path.c_str());
}
}  // namespace fabric::server::experience_runtime
