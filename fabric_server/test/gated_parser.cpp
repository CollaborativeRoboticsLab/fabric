#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>

#include <fabric_parser_xml/xml_parser_plugin.hpp>
#include <pluginlib/class_list_macros.hpp>

namespace fabric::test
{
class GatedParser : public fabric::XMLParser
{
public:
  void parse(fabric::plan::Plan& plan) override
  {
    if (plan.plan.find("demo/ParseGate") != std::string::npos)
    {
      const char* directory = std::getenv("FABRIC_TEST_PARSER_GATE");
      if (!directory)
      {
        throw fabric::fabric_exception("Parser gate directory is not configured");
      }
      const std::filesystem::path gate(directory);
      plan.connections[0].source = { "demo/Partial", "demo/PartialProvider", {}, 99 };
      std::ofstream(gate / "parser_entered").close();
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
      while (!std::filesystem::exists(gate / "parser_release"))
      {
        if (std::chrono::steady_clock::now() >= deadline)
        {
          throw fabric::fabric_exception("Parser gate timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      plan.connections.clear();
    }
    fabric::XMLParser::parse(plan);
    if (plan.plan.find("fabric-test-stop-edge") != std::string::npos)
    {
      plan.connections.at(0).on_stop = plan.connections.at(0).on_success;
    }
  }
};
}  // namespace fabric::test

PLUGINLIB_EXPORT_CLASS(fabric::test::GatedParser, fabric::ParserBase)