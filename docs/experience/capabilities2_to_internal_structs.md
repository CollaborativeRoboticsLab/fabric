# Capabilities2 To Internal Structs

This document records how `capabilities2` metadata becomes Experience internal
skill metadata.

## Principle

Experience keeps the external link in mind, but internally it should speak in
terms of skills and implementations instead of capability-server terminology.

- capability/interface becomes skill
- provider becomes implementation
- runnable spec command becomes skill command definition

## Current Mapping

| capabilities2 source | Experience internal field |
|---|---|
| runnable spec interface name | `BehaviourInfo.behaviour_name` |
| runnable spec provider name | `BehaviourInfo.implementation_name` |
| runnable spec interface description | `BehaviourInfo.behaviour_description` |
| runnable spec provider description | `BehaviourInfo.implementation_description` |
| runnable spec command | `BehaviourInfo.command` |
| runnable spec relations | `BehaviourInfo.relations` |

## Planning Rule

When Experience builds prompts for PromptTools-backed plan generation,
`BehaviourInfo.command` should be treated as the strongest execution-intent
signal. Descriptions and relations help retrieval and critique, but the command
definition is the clearest hint for how a skill should appear in a generated
behaviour plan.
