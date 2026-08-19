# Parameter Validation

This document defines the Fabric-side interpretation of provider parameter metadata during plan validation.

## Scope

Fabric validation is intentionally local and execution-oriented.

Its purpose is to reject concrete plans that cannot satisfy the required runtime inputs of the providers chosen in that plan.

It should not perform broad semantic retrieval, community traversal, or ranking. Those responsibilities belong to Experience.

## Canonical Parameter Identity

Fabric uses the same parameter identity order as the wider system:

1. `semantic_key`
2. `name`
3. `aliases`

For execution validation, exact matching is preferred and lower-confidence matches should not be stretched beyond what the provider metadata explicitly allows.

## Fallback Satisfaction Semantics

Fabric respects the provider-declared `satisfiable_from` list when checking a required runtime input.

Supported satisfaction sources are:

- `upstream`: a directly upstream capability in the current plan produces a matching runtime output
- `configuration`: a local configuration parameter or its default satisfies the runtime input
- `external`: the current plan provides the value explicitly for this runner
- `default`: the runtime input or its fallback configuration parameter declares a default value

Satisfaction precedence is:

1. upstream runtime output
2. explicit plan-provided value
3. configuration fallback parameter
4. declared default value

## Ordered Validation Plugins

Fabric now supports an ordered `validation_plugins` list instead of a single replacement validator.

Recommended default order:

1. `fabric::CompatibilityValidation`
2. `fabric::ParameterValidation`
3. future policy-specific validators

## Validation Rules

`fabric::CompatibilityValidation` remains responsible for interface/provider existence.

`fabric::ParameterValidation` then checks required runtime inputs using the provider metadata loaded from runnable specs.

Current rule set:

- validate only required runtime inputs
- inspect only direct upstream control-flow neighbors in the plan
- accept output-to-input matches when canonical identity overlaps and types are equal, or one side omits type information
- accept explicit plan values only when the runtime input allows `external` or `configuration`
- accept configuration fallback only when the runtime input allows `configuration`
- accept defaults only when the runtime input allows `default`, or when configuration fallback is allowed and the referenced configuration parameter has a default
- reject immediately when no allowed satisfaction path exists for a required runtime input

Optional runtime inputs are not treated as blocking validation failures.

## Code Surfaces

- `fabric_base/utils/structs.hpp`: capability and provider parameter metadata used during validation
- `fabric_server/capability_client.hpp`: fetches runnable specs and attaches provider metadata
- `fabric_server/server.hpp`: loads and runs ordered validation plugins
- `fabric_validator/compatibility_plugin.hpp`: compatibility validation
- `fabric_validator/parameter_plugin.hpp`: required runtime-input validation
- `fabric/docs/validation_plugins.md`: plugin-system overview

## Current Implementation Boundary

Implemented now:

- ordered validator composition through `validation_plugins`
- runnable-spec metadata loading into Fabric capability data
- required runtime-input validation using upstream, explicit, configuration, and default satisfaction paths

Future extensions:

- non-blocking warnings for weak but satisfiable links
- richer type compatibility rules
- policy validators that consume the same provider metadata after structural validation succeeds
