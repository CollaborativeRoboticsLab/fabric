#!/usr/bin/env python3

import argparse
import time

import rclpy
from rclpy.node import Node
from std_srvs.srv import Trigger


class ReadinessWaiter(Node):
    def __init__(self, mode: str, service_name: str, min_runnable_specs: int = 1) -> None:
        super().__init__('fabric_launch_readiness_waiter')
        self._mode = mode
        self._service_name = service_name
        self._min_runnable_specs = max(0, min_runnable_specs)

        if mode in ('capabilities', 'experience', 'trigger'):
            self._client = self.create_client(Trigger, service_name)
        else:
            raise ValueError(f'Unsupported mode: {mode}')

    def wait(self, timeout_sec: float) -> int:
        deadline = None if timeout_sec < 0 else time.monotonic() + timeout_sec
        poll_interval_sec = 1.0

        while rclpy.ok():
            if deadline is not None and time.monotonic() >= deadline:
                self.get_logger().error(f'Timed out waiting for {self._service_name}')
                return 1

            if not self._client.wait_for_service(timeout_sec=1.0):
                self.get_logger().info(f'Waiting for {self._service_name}...')
                continue

            request = Trigger.Request()

            future = self._client.call_async(request)
            rclpy.spin_until_future_complete(self, future, timeout_sec=1.0)

            if not future.done():
                self.get_logger().info(f'Waiting for response from {self._service_name}...')
                continue

            if future.exception() is not None:
                self.get_logger().warn(f'Call to {self._service_name} failed: {future.exception()}')
                continue

            response = future.result()
            if response.success:
                self.get_logger().info(f'{self._service_name} is ready: {response.message}')
                return 0

            self.get_logger().info(f'{self._service_name} not ready yet: {response.message}')
            time.sleep(poll_interval_sec)

        self.get_logger().error('ROS shutdown while waiting for readiness.')
        return 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['capabilities', 'experience', 'trigger'], required=True)
    parser.add_argument('--service-name', required=True)
    parser.add_argument('--timeout-sec', type=float, default=-1.0)
    parser.add_argument('--min-runnable-specs', type=int, default=1)
    args = parser.parse_args()

    rclpy.init()
    node = ReadinessWaiter(args.mode, args.service_name, args.min_runnable_specs)
    try:
        return node.wait(args.timeout_sec)
    finally:
        node.destroy_node()
        rclpy.shutdown()


if __name__ == '__main__':
    raise SystemExit(main())