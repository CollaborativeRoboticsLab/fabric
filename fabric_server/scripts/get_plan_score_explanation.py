#!/usr/bin/env python3

import argparse
import sys

from fabric_msgs.srv import GetPlanScoreExplanation
import rclpy
from rclpy.node import Node

class PlanScoreExplanationClient(Node):
    def __init__(self) -> None:
        super().__init__('plan_score_explanation_client')
        self._client = self.create_client(
            GetPlanScoreExplanation,
            '/fabric/experience/score_explanation',
        )

    def wait(self, timeout_sec: float) -> bool:
        return self._client.wait_for_service(timeout_sec=timeout_sec)

    def request(
        self,
        planning_request_id: str,
        candidate_plan_id: str,
        graph_hash: str,
        candidate_node_id: str,
    ):
        request = GetPlanScoreExplanation.Request()
        request.planning_request_id = planning_request_id
        request.candidate_plan_id = candidate_plan_id
        request.graph_hash = graph_hash
        request.candidate_node_id = candidate_node_id
        future = self._client.call_async(request)
        rclpy.spin_until_future_complete(self, future)
        return future.result()


def format_bool(value: bool) -> str:
    return 'true' if value else 'false'


def print_plan(response) -> None:
    plan = response.plan
    # Show the predicted plan-level score first so the human reader can compare it with the
    # observed terminal fields
    # and then drill into the per-node explanations below.
    print(f'found: {format_bool(response.found)}')
    print(f'message: {response.message}')
    print(f'planning_request_id: {plan.planning_request_id}')
    print(f'candidate_plan_id: {plan.candidate_plan_id}')
    print(f'graph_hash: {plan.graph_hash}')
    print(f'selected: {format_bool(plan.selected)}')
    print(f'scoring_supported: {format_bool(plan.scoring_supported)}')
    print(f'feasible: {format_bool(plan.feasible)}')
    print(f'sequential_graph: {format_bool(plan.sequential_graph)}')
    print(f'feasibility_reason: {plan.feasibility_reason}')
    print(f'terminal_success_estimate: {plan.terminal_success_estimate}')
    print(f'expected_runtime_seconds: {plan.expected_runtime_seconds}')
    print(f'expected_energy_delta_percentage: {plan.expected_energy_delta_percentage}')
    print(f'expected_fault_burden: {plan.expected_fault_burden}')
    print(f'expected_resource_demand: {plan.expected_resource_demand}')
    print(f'expected_uncertainty: {plan.expected_uncertainty}')
    print(f'final_rapfs: {plan.final_rapfs}')
    print(f'terminal_outcome: {plan.terminal_outcome}')
    print(f'terminal_success: {format_bool(plan.terminal_success)}')
    print(f'runtime_seconds: {plan.runtime_seconds}')
    print(f'energy_delta_percentage: {plan.energy_delta_percentage}')
    print(f'warning_count: {plan.warning_count}')
    print(f'error_count: {plan.error_count}')
    print(f'score_components: {plan.score_components}')
    print(f'calibration_version: {plan.calibration_version}')
    print(f'normalizer_version: {plan.normalizer_version}')
    print(f'estimator_version: {plan.estimator_version}')
    print(f'weight_version: {plan.weight_version}')
    print(f'node_count: {len(plan.node_explanations)}')
    for node in plan.node_explanations:
        print('---')
        print(f'node: {node.candidate_node_id}')
        print(f'  skill_name: {node.skill_name}')
        print(f'  selected_key: {node.selected_key}')
        print(f'  estimator_window: {node.estimator_window}')
        print(f'  fallback_level: {node.fallback_level}')
        print(f'  reason: {node.reason}')
        print(
            '  x_i: '
            f'[{node.feature_vector[0]}, {node.feature_vector[1]}, {node.feature_vector[2]}, '
            f'{node.feature_vector[3]}, {node.feature_vector[4]}, {node.feature_vector[5]}]'
        )
        print(
            '  available: '
            f'[{format_bool(node.feature_available[0])}, {format_bool(node.feature_available[1])}, '
            f'{format_bool(node.feature_available[2])}, {format_bool(node.feature_available[3])}, '
            f'{format_bool(node.feature_available[4])}, {format_bool(node.feature_available[5])}]'
        )


def print_node(response) -> None:
    node = response.node_explanation
    # The single-node view keeps the full estimator list because that is the fastest way to inspect
    # why one runner's
    # RAPFS slot values were missing or unexpectedly high.
    print(f'node_found: {format_bool(response.node_found)}')
    if not response.node_found:
        return
    print(f'candidate_node_id: {node.candidate_node_id}')
    print(f'skill_name: {node.skill_name}')
    print(f'skill_id: {node.skill_id}')
    print(f'requested_context_key: {node.requested_context_key}')
    print(f'requested_context_class: {node.requested_context_class}')
    print(f'selected_skill_id: {node.selected_skill_id}')
    print(f'selected_key: {node.selected_key}')
    print(f'estimator_window: {node.estimator_window}')
    print(f'fallback_level: {node.fallback_level}')
    print(f'proxy_source: {node.proxy_source}')
    print(f'reason: {node.reason}')
    print(f'semantic_familiarity: {node.semantic_familiarity}')
    print(f'semantic_relevant_capabilities: {list(node.semantic_relevant_capabilities)}')
    print(f'semantic_related_entities: {list(node.semantic_related_entities)}')
    print(f'semantic_dependencies: {list(node.semantic_dependencies)}')
    print(f'semantic_plan_patterns: {list(node.semantic_plan_patterns)}')
    print(f'semantic_fault_concepts: {list(node.semantic_fault_concepts)}')
    print(f'semantic_community_summaries: {list(node.semantic_community_summaries)}')
    print(
        'x_i: '
        f'[{node.feature_vector[0]}, {node.feature_vector[1]}, {node.feature_vector[2]}, '
        f'{node.feature_vector[3]}, {node.feature_vector[4]}, {node.feature_vector[5]}]'
    )
    for output in node.estimator_outputs:
        print(
            f'estimator[{output.name}]: value={output.value}, '
            f'available={format_bool(output.available)}, reason={output.reason}'
        )


def main() -> int:
    parser = argparse.ArgumentParser(description='Fetch a selected plan score explanation from Fabric.')
    parser.add_argument('planning_request_id')
    parser.add_argument('candidate_plan_id')
    parser.add_argument('graph_hash')
    parser.add_argument('--candidate-node-id', default='')
    parser.add_argument('--timeout', type=float, default=5.0)
    args = parser.parse_args()

    rclpy.init()
    node = PlanScoreExplanationClient()
    try:
        if not node.wait(args.timeout):
            print('service /fabric/experience/score_explanation not available', file=sys.stderr)
            return 1
        response = node.request(
            args.planning_request_id,
            args.candidate_plan_id,
            args.graph_hash,
            args.candidate_node_id,
        )
        if response is None:
            print('service call failed', file=sys.stderr)
            return 1
        print_plan(response)
        if args.candidate_node_id:
            print_node(response)
        return 0 if response.found else 2
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    raise SystemExit(main())
