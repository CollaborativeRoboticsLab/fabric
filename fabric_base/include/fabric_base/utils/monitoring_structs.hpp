#pragma once

#include <string>
#include <vector>

#include <fabric_base/utils/status_structs.hpp>

namespace fabric
{
namespace monitor
{

enum class MonitoringStatus
{
  NORMAL,
  SUSPICIOUS,
  FAULTY,
};

struct ComponentStatus
{
  status::Time timestamp;
  MonitoringStatus status{ MonitoringStatus::NORMAL };
  std::string description;
};

struct BatteryData
{
  double percentage{ -1.0 };
  std::vector<ComponentStatus> warnings;
};

struct LoggingData
{
  status::Time timestamp;
  std::vector<std::string> debug;
  std::vector<std::string> info;
  std::vector<std::string> warn;
  std::vector<std::string> error;
  std::vector<std::string> fatal;
};

struct DiagnosticData
{
  status::Time timestamp;
  std::vector<std::string> ok;
  std::vector<std::string> warn;
  std::vector<std::string> error;
  std::vector<std::string> stale;
};

}  // namespace monitor
}  // namespace fabric