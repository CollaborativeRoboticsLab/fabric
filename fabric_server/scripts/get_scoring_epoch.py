#!/usr/bin/env python3

import argparse
import sys

from fabric_msgs.srv import GetScoringEpoch
import rclpy
from rclpy.node import Node


class ScoringEpochQueryClient(Node):
    def __init__(self) -> None:
        super().__init__('scoring_epoch_query_client')
        self._client = self.create_client(
            GetScoringEpoch,
            '/fabric/experience/get_scoring_epoch',
        )

    def wait(self, timeout_sec: float) -> bool:
        return self._client.wait_for_service(timeout_sec=timeout_sec)

    def request(self, epoch_id: str):
        request = GetScoringEpoch.Request()
        request.epoch_id = epoch_id
        future = self._client.call_async(request)
        rclpy.spin_until_future_complete(self, future)
        return future.result()


def format_bool(value: bool) -> str:
    return 'true' if value else 'false'


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Fetch a stored scoring epoch record from Fabric.'
    )
    parser.add_argument('epoch_id')
    parser.add_argument('--timeout', type=float, default=5.0)
    args = parser.parse_args()

    rclpy.init()
    node = ScoringEpochQueryClient()
    try:
        if not node.wait(args.timeout):
            print('service /fabric/experience/get_scoring_epoch not available', file=sys.stderr)
            return 1
        response = node.request(args.epoch_id)
        if response is None:
            print('service call failed', file=sys.stderr)
            return 1
        print(f'found: {format_bool(response.found)}')
        print(f'message: {response.message}')
        if not response.found:
            return 2
        print(f'epoch_id: {response.epoch_id}')
        print(f'strategy_name: {response.strategy_name}')
        print(f'calibration_version: {response.calibration_version}')
        print(f'normalizer_version: {response.normalizer_version}')
        print(f'estimator_version: {response.estimator_version}')
        print(f'weight_version: {response.weight_version}')
        print(f'uncertainty_weight_locked: {format_bool(response.uncertainty_weight_locked)}')
        print(f'calibration_summary: {response.calibration_summary}')
        print(f'selection_bias_summary: {response.selection_bias_summary}')
        print(f'active: {format_bool(response.active)}')
        print(f'start_sec: {response.start_sec}')
        print(f'start_nanosec: {response.start_nanosec}')
        print(f'end_sec: {response.end_sec}')
        print(f'end_nanosec: {response.end_nanosec}')
        print(f'recorded_at_sec: {response.recorded_at_sec}')
        print(f'recorded_at_nanosec: {response.recorded_at_nanosec}')
        print(f'provenance: {response.provenance}')
        return 0
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    raise SystemExit(main())
