#!/usr/bin/env python3

import argparse
import json
from pathlib import Path
import sys

from fabric_msgs.srv import RecordPlanFeedback
import rclpy
from rclpy.node import Node


def serialize_score_components(value) -> str:
    if isinstance(value, (dict, list)):
        return json.dumps(value, separators=(',', ':'))
    if value is None:
        return ''
    return str(value)


class PlanFeedbackBatchClient(Node):
    def __init__(self) -> None:
        super().__init__('plan_feedback_batch_client')
        self._client = self.create_client(
            RecordPlanFeedback,
            '/fabric/experience/record_plan_feedback',
        )

    def wait(self, timeout_sec: float) -> bool:
        return self._client.wait_for_service(timeout_sec=timeout_sec)

    def submit(self, payload: dict):
        request = RecordPlanFeedback.Request()
        request.planning_request_id = payload['planning_request_id']
        request.candidate_plan_id = payload['candidate_plan_id']
        request.graph_hash = payload['graph_hash']
        request.terminal_outcome = payload['terminal_outcome']
        request.terminal_success = bool(payload.get('terminal_success', False))
        request.human_judgement = str(payload.get('human_judgement', ''))
        request.score_components = serialize_score_components(payload.get('score_components', ''))
        request.calibration_version = str(payload.get('calibration_version', ''))
        request.normalizer_version = str(payload.get('normalizer_version', ''))
        request.estimator_version = str(payload.get('estimator_version', ''))
        request.weight_version = str(payload.get('weight_version', ''))
        request.recorded_at_sec = int(payload.get('recorded_at_sec', 0))
        request.recorded_at_nanosec = int(payload.get('recorded_at_nanosec', 0))
        request.provenance = str(payload.get('provenance', 'evaluation_harness_jsonl'))
        future = self._client.call_async(request)
        rclpy.spin_until_future_complete(self, future)
        return future.result()


def load_records(path: Path) -> list[dict]:
    text = path.read_text(encoding='utf-8').strip()
    if not text:
        return []
    if text.startswith('['):
        payload = json.loads(text)
        if not isinstance(payload, list):
            raise ValueError('JSON array input must contain objects')
        return payload
    records = []
    for line_number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if not stripped:
            continue
        try:
            records.append(json.loads(stripped))
        except json.JSONDecodeError as exc:
            raise ValueError(f'invalid JSON on line {line_number}: {exc}') from exc
    return records


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            'Submit external plan feedback records from a JSON or '
            'JSONL file.'
        )
    )
    parser.add_argument('input_path')
    parser.add_argument('--timeout', type=float, default=5.0)
    parser.add_argument('--stop-on-error', action='store_true')
    args = parser.parse_args()

    records = load_records(Path(args.input_path))
    if not records:
        print('no feedback records found', file=sys.stderr)
        return 1

    rclpy.init()
    node = PlanFeedbackBatchClient()
    stored = 0
    failed = 0
    try:
        if not node.wait(args.timeout):
            print('service /fabric/experience/record_plan_feedback not available', file=sys.stderr)
            return 1

        for index, record in enumerate(records, start=1):
            try:
                response = node.submit(record)
            except KeyError as exc:
                print(f'record {index} missing required field: {exc}', file=sys.stderr)
                failed += 1
                if args.stop_on_error:
                    return 2
                continue

            if response is None or not response.stored:
                message = 'service call failed' if response is None else response.message
                print(f'record {index} failed: {message}', file=sys.stderr)
                failed += 1
                if args.stop_on_error:
                    return 2
                continue

            stored += 1
            print(f'record {index} stored: found_existing={response.found_existing}')

        print(f'stored={stored} failed={failed}')
        return 0 if failed == 0 else 2
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    raise SystemExit(main())
