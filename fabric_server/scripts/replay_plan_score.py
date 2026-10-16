#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import sys

from fabric_msgs.srv import ScorePlan
import rclpy
from rclpy.node import Node


class ScorePlanClient(Node):
    def __init__(self) -> None:
        super().__init__('score_plan_replay_client')
        self._client = self.create_client(ScorePlan, '/fabric/experience/score')

    def wait(self, timeout_sec: float) -> bool:
        return self._client.wait_for_service(timeout_sec=timeout_sec)

    def request(
        self,
        task_description: str,
        plan_xml: str,
        planning_request_id: str,
        candidate_plan_id: str,
        candidate_node_id: str,
    ):
        request = ScorePlan.Request()
        request.task_description = task_description
        request.plan_xml = plan_xml
        request.planning_request_id = planning_request_id
        request.candidate_plan_id = candidate_plan_id
        request.candidate_node_id = candidate_node_id
        future = self._client.call_async(request)
        rclpy.spin_until_future_complete(self, future)
        return future.result()


def format_bool(value: bool) -> str:
    return 'true' if value else 'false'


def print_plan(response) -> None:
    plan = response.plan
    print(f'scored: {format_bool(response.scored)}')
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
    print(
        f'expected_energy_delta_percentage: '
        f'{plan.expected_energy_delta_percentage}'
    )
    print(f'expected_fault_burden: {plan.expected_fault_burden}')
    print(f'expected_resource_demand: {plan.expected_resource_demand}')
    print(f'expected_uncertainty: {plan.expected_uncertainty}')
    print(f'final_rapfs: {plan.final_rapfs}')
    print(f'score_components: {plan.score_components}')
    print(f'calibration_version: {plan.calibration_version}')
    print(f'normalizer_version: {plan.normalizer_version}')
    print(f'estimator_version: {plan.estimator_version}')
    print(f'weight_version: {plan.weight_version}')
    print(f'node_count: {len(plan.node_explanations)}')


def print_node(response) -> None:
    node = response.node_explanation
    print(f'node_found: {format_bool(response.node_found)}')
    if not response.node_found:
        return
    print(f'candidate_node_id: {node.candidate_node_id}')
    print(f'skill_name: {node.skill_name}')
    print(f'selected_key: {node.selected_key}')
    print(f'estimator_window: {node.estimator_window}')
    print(f'fallback_level: {node.fallback_level}')
    print(f'reason: {node.reason}')
    print(
        'x_i: '
        f"[{node.feature_vector[0]}, {node.feature_vector[1]}, {node.feature_vector[2]}, "
        f"{node.feature_vector[3]}, {node.feature_vector[4]}, {node.feature_vector[5]}]"
    )


def build_snapshot(epoch: str, plan_path: Path, response) -> dict:
    plan = response.plan
    return {
        'epoch': epoch,
        'plan_path': str(plan_path),
        'planning_request_id': plan.planning_request_id,
        'candidate_plan_id': plan.candidate_plan_id,
        'graph_hash': plan.graph_hash,
        'scoring_supported': bool(plan.scoring_supported),
        'feasible': bool(plan.feasible),
        'sequential_graph': bool(plan.sequential_graph),
        'terminal_success_estimate': plan.terminal_success_estimate,
        'expected_runtime_seconds': plan.expected_runtime_seconds,
        'expected_energy_delta_percentage': plan.expected_energy_delta_percentage,
        'expected_fault_burden': plan.expected_fault_burden,
        'expected_resource_demand': plan.expected_resource_demand,
        'expected_uncertainty': plan.expected_uncertainty,
        'final_rapfs': plan.final_rapfs,
        'score_components': plan.score_components,
        'calibration_version': plan.calibration_version,
        'normalizer_version': plan.normalizer_version,
        'estimator_version': plan.estimator_version,
        'weight_version': plan.weight_version,
        'recorded_at_sec': plan.recorded_at_sec,
        'recorded_at_nanosec': plan.recorded_at_nanosec,
    }


def append_snapshot(snapshot_path: Path, snapshot: dict) -> None:
    snapshot_path.parent.mkdir(parents=True, exist_ok=True)
    with snapshot_path.open('a', encoding='utf-8') as handle:
        handle.write(json.dumps(snapshot, sort_keys=True) + '\n')


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            'Replay one or more XML plans through Fabric score-only '
            'evaluation and optionally record snapshots.'
        )
    )
    parser.add_argument('task_description')
    parser.add_argument('plan_paths', nargs='+')
    parser.add_argument('--epoch', default='')
    parser.add_argument('--planning-request-id', default='')
    parser.add_argument('--candidate-plan-id', default='')
    parser.add_argument('--candidate-node-id', default='')
    parser.add_argument('--snapshot-out', default='')
    parser.add_argument('--timeout', type=float, default=5.0)
    args = parser.parse_args()

    plan_paths = [Path(value) for value in args.plan_paths]
    for plan_path in plan_paths:
        if not plan_path.is_file():
            print(f'plan file not found: {plan_path}', file=sys.stderr)
            return 1

    snapshot_path = Path(args.snapshot_out) if args.snapshot_out else None

    rclpy.init()
    node = ScorePlanClient()
    try:
        if not node.wait(args.timeout):
            print('service /fabric/experience/score not available', file=sys.stderr)
            return 1

        exit_code = 0
        for index, plan_path in enumerate(plan_paths, start=1):
            plan_xml = plan_path.read_text(encoding="utf-8")
            planning_request_id = args.planning_request_id
            candidate_plan_id = args.candidate_plan_id
            if len(plan_paths) > 1:
                if planning_request_id:
                    planning_request_id = f"{planning_request_id}_{index}"
                if candidate_plan_id:
                    candidate_plan_id = f"{candidate_plan_id}_{index}"

            response = node.request(
                args.task_description,
                plan_xml,
                planning_request_id,
                candidate_plan_id,
                args.candidate_node_id,
            )
            if response is None:
                print(f'service call failed for {plan_path}', file=sys.stderr)
                exit_code = 2
                continue

            print(f'=== {plan_path} ===')
            print_plan(response)
            if args.candidate_node_id:
                print_node(response)

            if response.scored:
                snapshot = build_snapshot(args.epoch, plan_path, response)
                if snapshot_path is not None:
                    append_snapshot(snapshot_path, snapshot)
                elif args.epoch:
                    print(json.dumps(snapshot, sort_keys=True))
            else:
                exit_code = 2

        return exit_code
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    raise SystemExit(main())
