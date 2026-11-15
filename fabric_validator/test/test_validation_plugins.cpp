#include <gtest/gtest.h>

#include <fabric_validator/compatibility_plugin.hpp>
#include <fabric_validator/parameter_plugin.hpp>

namespace
{
class RunnableValidationTest : public ::testing::Test
{
protected:
  void SetUp() override
  {
    fabric::capability::Descriptor descriptor;
    descriptor.interface_name = "demo/SemanticMove";
    descriptor.provider_name = "demo/Provider";
    capabilities.push_back(descriptor);
    plan.connections[0].source.interface = descriptor.interface_name;
    plan.connections[0].source.provider = descriptor.provider_name;
    plan.connections[0].source.instance_id = 0;
  }

  fabric::capability::ParameterInfo required_input()
  {
    fabric::capability::ParameterInfo input;
    input.name = "target";
    input.type = "string";
    input.semantic_key = "pose";
    input.required = true;
    return input;
  }

  fabric::plan::Plan plan;
  std::vector<fabric::capability::Descriptor> capabilities;
  fabric::CompatibilityValidation compatibility;
  fabric::ParameterValidation parameters;
};

TEST_F(RunnableValidationTest, AcceptsResolvedSemanticPairAndAlternativeProvider)
{
  auto alternative = capabilities.front();
  alternative.provider_name = "demo/Alternative";
  capabilities.push_back(alternative);
  plan.connections[0].source.provider = alternative.provider_name;
  EXPECT_NO_THROW(compatibility.validate(plan, capabilities));
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
}

TEST_F(RunnableValidationTest, RejectsUnlistedInterfaceProviderCombination)
{
  auto other = capabilities.front();
  other.interface_name = "demo/OtherMove";
  other.provider_name = "demo/OtherProvider";
  capabilities.push_back(other);
  plan.connections[0].source.provider = other.provider_name;
  EXPECT_THROW(compatibility.validate(plan, capabilities), fabric::fabric_exception);
  EXPECT_THROW(parameters.validate(plan, capabilities), fabric::fabric_exception);
}

TEST_F(RunnableValidationTest, KeepsContractsSeparateForOneProviderWithMultipleInterfaces)
{
  auto other = capabilities.front();
  capabilities.front().runtime_input_parameters.push_back(required_input());
  other.interface_name = "demo/OtherMove";
  capabilities.push_back(other);
  EXPECT_THROW(parameters.validate(plan, capabilities), fabric::fabric_exception);
  plan.connections[0].source.interface = other.interface_name;
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
}

TEST_F(RunnableValidationTest, AcceptsUpstreamSemanticMatchAndRejectsTypeMismatch)
{
  capabilities.front().runtime_input_parameters.push_back(required_input());
  fabric::capability::Descriptor upstream;
  upstream.interface_name = "demo/Locate";
  upstream.provider_name = "demo/Locator";
  fabric::capability::ParameterInfo output;
  output.name = "location";
  output.type = "string";
  output.semantic_key = "pose";
  upstream.runtime_output_parameters.push_back(output);
  capabilities.push_back(upstream);
  plan.connections[1].source.interface = upstream.interface_name;
  plan.connections[1].source.provider = upstream.provider_name;
  plan.connections[1].source.instance_id = 1;
  plan.connections[1].on_success = plan.connections[0].source;
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
  capabilities[1].runtime_output_parameters.front().type = "double";
  EXPECT_THROW(parameters.validate(plan, capabilities), fabric::fabric_exception);
}

TEST_F(RunnableValidationTest, UsesExactUpstreamPairRatherThanProviderAlone)
{
  capabilities.front().runtime_input_parameters.push_back(required_input());
  auto other = capabilities.front();
  other.interface_name = "demo/OtherMove";
  other.runtime_input_parameters.clear();
  other.runtime_output_parameters.push_back(required_input());
  capabilities.push_back(other);
  plan.connections[1].source = plan.connections[0].source;
  plan.connections[1].source.instance_id = 1;
  plan.connections[1].on_success = plan.connections[0].source;
  EXPECT_THROW(parameters.validate(plan, capabilities), fabric::fabric_exception);
  plan.connections[1].source.interface = other.interface_name;
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
}

TEST_F(RunnableValidationTest, AcceptsExplicitAliasOnlyWhenAllowed)
{
  auto input = required_input();
  input.aliases = { "goal" };
  input.satisfiable_from = { "external" };
  capabilities.front().runtime_input_parameters.push_back(input);
  plan.connections[0].source.parameters.set_value("goal", std::string("destination"), fabric::OptionType::STRING);
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
  capabilities.front().runtime_input_parameters.front().satisfiable_from = { "upstream" };
  EXPECT_THROW(parameters.validate(plan, capabilities), fabric::fabric_exception);
}

TEST_F(RunnableValidationTest, AcceptsConfigurationFallbackAndDefaultOnlyWhenAllowed)
{
  auto input = required_input();
  input.satisfiable_from = { "configuration" };
  input.fallback_parameter = "configured_target";
  capabilities.front().runtime_input_parameters.push_back(input);
  fabric::capability::ParameterInfo configuration;
  configuration.name = "configured_target";
  capabilities.front().configuration_parameters.push_back(configuration);
  EXPECT_THROW(parameters.validate(plan, capabilities), fabric::fabric_exception);
  plan.connections[0].source.parameters.set_value(configuration.name, std::string("destination"), fabric::OptionType::STRING);
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
  plan.connections[0].source.parameters.options.clear();
  capabilities.front().configuration_parameters.front().has_default = true;
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
  capabilities.front().runtime_input_parameters.front().satisfiable_from = { "default" };
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
  capabilities.front().runtime_input_parameters.front().satisfiable_from = { "upstream" };
  EXPECT_THROW(parameters.validate(plan, capabilities), fabric::fabric_exception);
}

TEST_F(RunnableValidationTest, AcceptsRuntimeDefaultAndIgnoresOptionalInputs)
{
  auto input = required_input();
  input.satisfiable_from = { "default" };
  input.has_default = true;
  capabilities.front().runtime_input_parameters.push_back(input);
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
  capabilities.front().runtime_input_parameters.front().has_default = false;
  EXPECT_THROW(parameters.validate(plan, capabilities), fabric::fabric_exception);
  capabilities.front().runtime_input_parameters.front().required = false;
  EXPECT_NO_THROW(parameters.validate(plan, capabilities));
}
}  // namespace