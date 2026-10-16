#!/usr/bin/env python3

import argparse
import sys

from fabric_msgs.srv import RecordScoringEpoch
import rclpy
from rclpy.node import Node


class ScoringEpochClient(Node):
    def __init__(self) -> None:
        super().__init__('scoring_epoch_client')
        self._client = self.create_client(
            RecordScoringEpoch,
            '/fabric/experience/record_scoring_epoch',
        )

    def wait(self, timeout_sec: float) -> bool:
        return self._client.wait_for_service(timeout_sec=timeout_sec)

    def request(self, args: argparse.Namespace):
        request = RecordScoringEpoch.Request()
        request.epoch_id = args.epoch_id
        request.strategy_name = args.strategy_name
        request.calibration_version = args.calibration_version
        request.normalizer_version = args.normalizer_version
        request.estimator_version = args.estimator_version
        request.weight_version = args.weight_version
        request.uncertainty_weight_locked = not args.allow_uncertainty_retune
        request.calibration_summary = args.calibration_summary
        request.selection_bias_summary = args.selection_bias_summary
        request.active = not args.closed
        request.start_sec = args.start_sec
        request.start_nanosec = args.start_nanosec
        request.end_sec = args.end_sec
        request.end_nanosec = args.end_nanosec
        request.recorded_at_sec = args.recorded_at_sec
        request.recorded_at_nanosec = args.recorded_at_nanosec
        request.provenance = args.provenance
        future = self._client.call_async(request)
        rclpy.spin_until_future_complete(self, future)
        return future.result()


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Record or update a scoring epoch/version checkpoint.'
    )
    parser.add_argument('epoch_id')
    parser.add_argument('strategy_name')
    parser.add_argument('calibration_version')
    parser.add_argument('normalizer_version')
    parser.add_argument('estimator_version')
    parser.add_argument('weight_version')
    parser.add_argument('--calibration-summary', default='')
    parser.add_argument('--selection-bias-summary', default='')
    parser.add_argument('--allow-uncertainty-retune', action='store_true')
    parser.add_argument('--closed', action='store_true')
    parser.add_argument('--start-sec', type=int, default=0)
    parser.add_argument('--start-nanosec', type=int, default=0)
    parser.add_argument('--end-sec', type=int, default=0)
    parser.add_argument('--end-nanosec', type=int, default=0)
    parser.add_argument('--recorded-at-sec', type=int, default=0)
    parser.add_argument('--recorded-at-nanosec', type=int, default=0)
    parser.add_argument('--provenance', default='manual scoring epoch script')
    parser.add_argument('--timeout', type=float, default=5.0)
    args = parser.parse_args()

    rclpy.init()
    node = ScoringEpochClient()
    try:
        if not node.wait(args.timeout):
            print(
                'service /fabric/experience/record_scoring_epoch not available',
                file=sys.stderr,
            )
            return 1
        response = node.request(args)
        if response is None:
            print('service call failed', file=sys.stderr)
            return 1
        print(
            f'stored={response.stored} '
            f'found_existing={response.found_existing} '
            f'message={response.message}'
        )
        return 0 if response.stored else 2
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    raise SystemExit(main())
