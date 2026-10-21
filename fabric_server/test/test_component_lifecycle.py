import os
from pathlib import Path
import signal
import subprocess
import time

from ament_index_python.packages import get_package_share_directory
from composition_interfaces.srv import LoadNode, UnloadNode
from prompt_msgs.srv import Embedding, Prompt, Tokenize
import pytest
import rclpy
from rclpy.parameter import Parameter
import yaml


pytestmark = pytest.mark.skipif(
    os.environ.get('FABRIC_RUN_INTEGRATION_TESTS') != '1',
    reason='Requires the complete installed Fabric runtime plugin set',
)


def flatten_parameters(parameters, prefix=''):
    for name, value in parameters.items():
        key = f'{prefix}.{name}' if prefix else name
        if isinstance(value, dict):
            yield from flatten_parameters(value, key)
        else:
            yield Parameter(key, value=value).to_parameter_msg()


@pytest.mark.parametrize('unload_first', [True, False])
def test_component_initializes_and_cleans_up(tmp_path, unload_first):
    rclpy.init()
    node = rclpy.create_node('fabric_component_lifecycle_test')
    services = [
        node.create_service(Prompt, '/prompt/prompt', lambda request, response: response),
        node.create_service(Embedding, '/prompt/embedding', lambda request, response: response),
        node.create_service(Tokenize, '/prompt/tokenizer', lambda request, response: response),
    ]
    processes = []
    try:
        fabric_share = Path(get_package_share_directory('fabric_server'))
        capability_share = Path(get_package_share_directory('capabilities2_server'))
        with (fabric_share / 'config/fabric.yaml').open() as stream:
            parameters = yaml.safe_load(stream)['/**']['ros__parameters']
        plan = tmp_path / 'startup.xml'
        plan.write_text('<Plan><Control type="sequential"/></Plan>')
        request = LoadNode.Request()
        request.package_name = 'fabric_server'
        request.plugin_name = 'fabric::Fabric'
        request.node_name = 'fabric_component_probe'
        request.parameters = list(flatten_parameters(parameters))
        request.parameters.append(
            Parameter('plan_file_path', value=str(plan)).to_parameter_msg())
        load = node.create_client(LoadNode, '/fabric_probe_container/_container/load_node')
        unload = node.create_client(UnloadNode, '/fabric_probe_container/_container/unload_node')
        environment = os.environ.copy()
        environment['HOME'] = str(tmp_path)
        commands = [
            ['ros2', 'run', 'capabilities2_server', 'capabilities2_server_node',
             '--ros-args', '-r', '__node:=capabilities', '--params-file',
             str(capability_share / 'config/capabilities.yaml')],
            ['ros2', 'run', 'rclcpp_components', 'component_container_mt',
             '--ros-args', '-r', '__node:=fabric_probe_container'],
        ]
        log_path = tmp_path / 'runtime.log'
        with log_path.open('w') as log:
            try:
                for command in commands:
                    processes.append(subprocess.Popen(
                        command, cwd=tmp_path, env=environment, stdout=log,
                        stderr=subprocess.STDOUT, start_new_session=True))
                assert load.wait_for_service(timeout_sec=10), 'No component load service'
                future = load.call_async(request)
                rclpy.spin_until_future_complete(node, future, timeout_sec=30)
                assert future.done(), 'Component loading timed out'
                response = future.result()
                assert response.success, response.error_message
                deadline = time.monotonic() + 15
                while time.monotonic() < deadline:
                    rclpy.spin_once(node, timeout_sec=0.1)
                    if 'Fabric processing is enabled.' in log_path.read_text():
                        break
                assert 'Fabric processing is enabled.' in log_path.read_text()
                if unload_first:
                    unload_request = UnloadNode.Request()
                    unload_request.unique_id = response.unique_id
                    future = unload.call_async(unload_request)
                    rclpy.spin_until_future_complete(node, future, timeout_sec=10)
                    assert future.done(), 'Component unload timed out'
                    assert future.result().success, future.result().error_message
            finally:
                for process in reversed(processes):
                    if process.poll() is None:
                        os.killpg(process.pid, signal.SIGINT)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait(timeout=5)
        output = log_path.read_text()
        assert all(process.returncode == 0 for process in processes), output
        assert 'Stopped monitor plugin: ResourceMonitor' in output, output
        assert 'Bond stopped with capabilities server' in output, output
    finally:
        for service in services:
            node.destroy_service(service)
        node.destroy_node()
        rclpy.shutdown()
