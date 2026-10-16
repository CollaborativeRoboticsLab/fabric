#!/usr/bin/env python3

import argparse

from fabric_msgs.srv import RecordPlanFeedback
import rclpy
from rclpy.node import Node


class PlanFeedbackClient(Node):
    def __init__(self):
        super().__init__('plan_feedback_client')
        self.client = self.create_client(
            RecordPlanFeedback,
            '/fabric/experience/record_plan_feedback',
        )
        while not self.client.wait_for_service(timeout_sec=1.0):
            self.get_logger().info(
                'waiting for /fabric/experience/record_plan_feedback service...'
            )

    def request(self, args: argparse.Namespace):
        request = RecordPlanFeedback.Request()
        request.planning_request_id = args.planning_request_id
        request.candidate_plan_id = args.candidate_plan_id
        request.graph_hash = args.graph_hash
        request.terminal_outcome = args.terminal_outcome
        request.terminal_success = args.terminal_success
        request.human_judgement = args.human_judgement
        request.score_components = args.score_components
        request.calibration_version = args.calibration_version
        request.normalizer_version = args.normalizer_version
        request.estimator_version = args.estimator_version
        request.weight_version = args.weight_version
        request.recorded_at_sec = args.recorded_at_sec
        request.recorded_at_nanosec = args.recorded_at_nanosec
        request.provenance = args.provenance
        return self.client.call_async(request)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description='Record external feedback for a selected plan'
    )
    parser.add_argument('planning_request_id')
    parser.add_argument('candidate_plan_id')
    parser.add_argument('graph_hash')
    parser.add_argument('terminal_outcome')
    parser.add_argument(
        '--terminal-success',
        action='store_true',
        help='Mark the final plan outcome as successful',
    )
    parser.add_argument(
        '--human-judgement',
        default='',
        help='Optional operator or external-system judgement',
    )
    parser.add_argument(
        '--score-components',
        default='',
        help='Optional serialized score components override',
    )
    parser.add_argument(
        '--calibration-version',
        default='',
        help='Optional calibration checkpoint/version override',
    )
    parser.add_argument(
        '--normalizer-version',
        default='',
        help='Optional normalizer version override',
    )
    parser.add_argument(
        '--estimator-version',
        default='',
        help='Optional estimator version override',
    )
    parser.add_argument(
        '--weight-version',
        default='',
        help='Optional weight version override',
    )
    parser.add_argument(
        '--recorded-at-sec',
        type=int,
        default=0,
        help='Optional timestamp seconds override',
    )
    parser.add_argument(
        '--recorded-at-nanosec',
        type=int,
        default=0,
        help='Optional timestamp nanoseconds override',
    )
    parser.add_argument('--provenance', default='', help='Optional provenance string')
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    rclpy.init()
    node = PlanFeedbackClient()
    future = node.request(args)
    rclpy.spin_until_future_complete(node, future)
    response = future.result()
    if response is None:
        node.get_logger().error('feedback request failed')
    else:
        node.get_logger().info(
            f"stored={response.stored} found_existing={response.found_existing} message={response.message}"
        )
    node.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()
