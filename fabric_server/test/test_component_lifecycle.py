import os
from pathlib import Path
import signal
import subprocess
import threading
import time

from ament_index_python.packages import get_package_share_directory
from capabilities2_msgs.srv import GetRunnableSpecs
from capabilities2_msgs.srv import ConnectCapability
from capabilities2_msgs.srv import FreeCapability as CapabilitiesFreeCapability
from capabilities2_msgs.srv import TriggerCapability
from composition_interfaces.srv import LoadNode, UnloadNode
from capabilities2_msgs.msg import CapabilityEventCode
from fabric_msgs.msg import FabricStatus
from fabric_msgs.srv import CancelFabricPlan, CompleteFabric, GetFabricPlanSnapshot, GetPlanStatus, ParsePlan, SubmitFabricPlan
from prompt_msgs.msg import Embedding as PromptEmbeddingMessage
from prompt_msgs.srv import Embedding, Prompt, Tokenize
import pytest
import rclpy
from rclpy.callback_groups import MutuallyExclusiveCallbackGroup
from rclpy.executors import MultiThreadedExecutor, SingleThreadedExecutor
from rclpy.parameter import Parameter
from rcl_interfaces.srv import GetParameters, ListParameters
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


@pytest.mark.parametrize('unload_first,use_default_plan,experience_enabled,gate_runnable_specs,gate_trigger,gate_recovery,gate_connecting,gate_parsing', [
    (True, False, True, False, False, False, False, False), (True, False, True, True, False, False, False, False),
    (True, False, True, True, False, True, False, False), (True, False, True, False, True, False, False, False),
    (True, False, True, False, True, False, False, True), (True, False, True, False, False, False, True, False),
    (False, False, True, False, False, False, False, False), (True, True, True, False, False, False, False, False),
    (True, False, False, False, False, False, False, False),
])
def test_component_initializes_and_cleans_up(
    tmp_path, unload_first, use_default_plan, experience_enabled, gate_runnable_specs, gate_trigger,
    gate_recovery, gate_connecting, gate_parsing,
):
    rclpy.init()
    node = rclpy.create_node('fabric_component_lifecycle_test')

    def successful_prompt(_request, response):
        response.response.success = True
        response.response.response = (
            '{"combined_description":"","entities":[],"relationships":[]}'
        )
        return response

    def successful_embedding(_request, response):
        response.output.success = True
        embedding = PromptEmbeddingMessage()
        embedding.is_float = True
        embedding.float_embedding = [0.0] * 1536
        response.output.embeddings = [embedding]
        return response

    def successful_tokenization(_request, response):
        response.output.success = True
        return response

    def assert_completion_graph(snapshot, plan_id, has_stop_edge=False):
        runner = 'fabric_capabilities/FabricCompletionRunner'
        assert snapshot.has_plan
        assert snapshot.plan_id == plan_id
        assert snapshot.connections_complete
        assert len(snapshot.instances) == 2
        first, second = sorted(snapshot.instances, key=lambda instance: instance.instance_id)
        assert first.instance_id != second.instance_id
        assert (first.interface, first.provider) == (runner, runner)
        assert (second.interface, second.provider) == (runner, runner)
        assert len(snapshot.connections) == 2
        first_connection, second_connection = sorted(
            snapshot.connections, key=lambda connection: connection.source.instance_id
        )
        assert first_connection.source.instance_id == first.instance_id
        assert first_connection.on_success.instance_id == second.instance_id
        assert (first_connection.on_success.interface, first_connection.on_success.provider) == (runner, runner)
        assert not first_connection.on_start.interface
        if has_stop_edge:
            assert (first_connection.on_stop.interface, first_connection.on_stop.provider) == (runner, runner)
            assert first_connection.on_stop.instance_id == second.instance_id
        else:
            assert not first_connection.on_stop.interface
        assert not first_connection.on_failure.interface
        assert second_connection.source.instance_id == second.instance_id
        assert not second_connection.on_start.interface
        assert not second_connection.on_stop.interface
        assert not second_connection.on_success.interface
        assert not second_connection.on_failure.interface
        endpoint = lambda node: (node.interface, node.provider, node.instance_id)
        return (
            tuple(sorted(endpoint(instance) for instance in snapshot.instances)),
            tuple(sorted(
                (
                    endpoint(connection.source),
                    endpoint(connection.on_start),
                    endpoint(connection.on_stop),
                    endpoint(connection.on_success),
                    endpoint(connection.on_failure),
                    connection.description,
                )
                for connection in snapshot.connections
            )),
        )

    def assert_parallel_start_graph(snapshot, plan_id):
        runner_a = 'demo/GatedA'
        runner_b = 'demo/GatedB'
        mux = 'capabilities2_runner/InputMultiplexRunner'
        completion = 'fabric_capabilities/FabricCompletionRunner'
        assert snapshot.has_plan
        assert snapshot.plan_id == plan_id
        assert snapshot.connections_complete
        instances = {instance.interface: instance for instance in snapshot.instances}
        assert set(instances) == {runner_a, runner_b, mux, completion}
        connections = {connection.source.interface: connection for connection in snapshot.connections}
        assert set(connections) == {runner_a, runner_b, mux, completion}
        first = connections[runner_a]
        second = connections[runner_b]
        assert first.on_start.interface == runner_b
        assert first.on_start.instance_id == instances[runner_b].instance_id
        assert first.on_success.interface == mux
        assert first.on_success.instance_id == instances[mux].instance_id
        assert second.on_success.interface == mux
        assert second.on_success.instance_id == instances[mux].instance_id
        assert connections[mux].on_success.interface == completion
        assert connections[mux].on_success.instance_id == instances[completion].instance_id
        for connection in snapshot.connections:
            assert not connection.on_stop.interface
            assert not connection.on_failure.interface

    def assert_recovery_graph(snapshot, plan_id):
        waypoint = 'turtlebot_capabilities/TurtlebotWaypointRunner'
        assert snapshot.has_plan
        assert snapshot.plan_id == plan_id
        assert snapshot.connections_complete
        waypoint_instances = [instance for instance in snapshot.instances if instance.interface == waypoint]
        assert len(waypoint_instances) == 6
        waypoint_ids = {instance.instance_id for instance in waypoint_instances}
        assert len(waypoint_ids) == len(waypoint_instances)
        failure_edges = [connection for connection in snapshot.connections if connection.on_failure.interface]
        assert len(failure_edges) == 1
        failure_edge = failure_edges[0]
        assert (failure_edge.source.interface, failure_edge.source.provider) == (waypoint, waypoint)
        assert (failure_edge.on_failure.interface, failure_edge.on_failure.provider) == (waypoint, waypoint)
        assert failure_edge.source.instance_id in waypoint_ids
        assert failure_edge.on_failure.instance_id in waypoint_ids
        assert failure_edge.source.instance_id != failure_edge.on_failure.instance_id
        assert all(not connection.on_stop.interface for connection in snapshot.connections)

    tmp_path, unload_first, use_default_plan, experience_enabled, gate_runnable_specs, gate_trigger,
    gate_recovery,

    services = [
        node.create_service(Prompt, '/prompt/prompt', successful_prompt),
        node.create_service(Embedding, '/prompt/embedding', successful_embedding),
        node.create_service(Tokenize, '/prompt/tokenizer', successful_tokenization),
    ]
    processes = []
    gate_node = None
    gate_executor = None
    gate_thread = None
    gate_active = threading.Event()
    gate_entered = threading.Event()
    gate_release = threading.Event()
    gate_returned = threading.Event()
    connect_gate_enabled = threading.Event()
    connect_gate_claimed = threading.Event()
    connect_gate_entered = threading.Event()
    connect_gate_release = threading.Event()
    connect_gate_returned = threading.Event()
    connect_failure_enabled = threading.Event()
    connection_requests = []
    trigger_entered = threading.Event()
    free_entered = threading.Event()
    free_release = threading.Event()
    try:
        fabric_share = Path(get_package_share_directory('fabric_server'))
        capability_share = Path(get_package_share_directory('capabilities2_server'))
        with (fabric_share / 'config/fabric.yaml').open() as stream:
            parameters = yaml.safe_load(stream)['/**']['ros__parameters']
        experience = parameters['experience']
        experience['enabled'] = experience_enabled
        if not experience_enabled:
            experience['telemetry']['monitor_plugins'] = ['fabric::NonexistentMonitor']
            parameters['telemetry']['fabric_status_topic'] = '/fabric_test/status'
        else:
            experience['BatteryMonitor']['topic'] = '/fabric_test/battery'
            experience['RosoutMonitor']['model'] = 'monitor-test-model'
        experience['KnowledgeCoreGraphRag']['edge']['parameter']['min_score'] = 0.42
        experience['PlanningPlugin']['model_family'] = 'test_family'
        experience['RapfsScorerPlugin']['estimators']['success']['weight'] = 0.73
        experience['SuccessProbabilityPlugin']['alpha'] = 3.0
        if gate_parsing:
            parameters['parsing_plugin'] = 'fabric::test::GatedParser'
            parameters['capability_client']['services']['connect_capability'] = (
                '/fabric_test/gated_connect_capability'
            )
        if gate_runnable_specs:
            parameters['capability_client']['services']['get_runnable_specs'] = (
                '/fabric_test/gated_get_runnable_specs'
            )
        if gate_trigger:
            parameters['capability_client']['services']['trigger_capability'] = (
                '/fabric_test/gated_trigger_capability'
            )
            parameters['capability_client']['services']['free_capability'] = (
                '/fabric_test/gated_free_capability'
            )
        if gate_connecting:
            parameters['capability_client']['services']['connect_capability'] = (
                '/fabric_test/gated_connect_capability'
            )
            parameters['capability_client']['services']['trigger_capability'] = (
                '/fabric_test/gated_trigger_capability'
            )
            parameters['capability_client']['services']['free_capability'] = (
                '/fabric_test/gated_free_capability'
            )
        if gate_runnable_specs or gate_trigger or gate_connecting:
            gate_node = rclpy.create_node('fabric_runnable_specs_gate')

            if gate_runnable_specs:
                def hold_runnable_specs(_request, response):
                    if gate_active.is_set():
                        gate_entered.set()
                        gate_release.wait(timeout=30)
                        gate_release.clear()
                        gate_entered.clear()
                        gate_returned.set()
                    return response

                gate_node.create_service(
                    GetRunnableSpecs,
                    '/fabric_test/gated_get_runnable_specs',
                    hold_runnable_specs,
                )
            if gate_connecting or gate_parsing:
                def hold_capability_connection(request, response):
                    connection_requests.append(request)
                    response.successful = not connect_failure_enabled.is_set()
                    if not response.successful:
                        response.error_message = 'Controlled connection rejection'
                    if connect_gate_enabled.is_set() and not connect_gate_claimed.is_set():
                        connect_gate_claimed.set()
                        connect_gate_entered.set()
                        connect_gate_release.wait(timeout=30)
                        connect_gate_returned.set()
                    return response

                gate_node.create_service(
                    ConnectCapability,
                    '/fabric_test/gated_connect_capability',
                    hold_capability_connection,
                )
            if gate_trigger or gate_connecting:
                trigger_callback_group = MutuallyExclusiveCallbackGroup()

                def trigger_without_completion(_request, response):
                    trigger_entered.set()
                    response.successful = True
                    return response

                def hold_capability_cleanup(_request, response):
                    free_entered.set()
                    free_release.wait(timeout=30)
                    return response

                gate_node.create_service(
                    TriggerCapability,
                    '/fabric_test/gated_trigger_capability',
                    trigger_without_completion,
                    callback_group=trigger_callback_group,
                )
                gate_node.create_service(
                    CapabilitiesFreeCapability,
                    '/fabric_test/gated_free_capability',
                    hold_capability_cleanup,
                )
            gate_executor = MultiThreadedExecutor(num_threads=2) if gate_trigger or gate_connecting else SingleThreadedExecutor()
            gate_executor.add_node(gate_node)
            gate_thread = threading.Thread(target=gate_executor.spin, daemon=True)
            gate_thread.start()
        plan = tmp_path / 'startup.xml'
        plan.write_text('<Plan><Control type="sequential"/></Plan>')
        if not use_default_plan:
            parameters['plan_file_path'] = str(plan)
        request = LoadNode.Request()
        request.package_name = 'fabric_server'
        request.plugin_name = 'fabric::Fabric'
        request.node_name = 'fabric_component_probe'
        request.parameters = list(flatten_parameters(parameters))
        load = node.create_client(LoadNode, '/fabric_probe_container/_container/load_node')
        unload = node.create_client(UnloadNode, '/fabric_probe_container/_container/unload_node')
        environment = os.environ.copy()
        environment['HOME'] = str(tmp_path)
        if gate_parsing:
            environment['FABRIC_TEST_PARSER_GATE'] = str(tmp_path)
        capability_config = capability_share / 'config/capabilities.yaml'
        if gate_trigger or gate_connecting:
            with capability_config.open() as stream:
                capability_parameters = yaml.safe_load(stream)
            capability_parameters['/**']['ros__parameters']['package_paths'] = [
                str(Path(get_package_share_directory('capabilities2_server')).parents[2])
            ]
            capability_config = tmp_path / 'capabilities.yaml'
            with capability_config.open('w') as stream:
                yaml.safe_dump(capability_parameters, stream)
        commands = [
            ['ros2', 'run', 'capabilities2_server', 'capabilities2_server_node',
             '--ros-args', '-r', '__node:=capabilities', '--params-file',
             str(capability_config)],
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
                runnable_specs = node.create_client(GetRunnableSpecs, '/capabilities/get_runnable_specs')
                assert runnable_specs.wait_for_service(timeout_sec=30), 'No capabilities2 runnable-specs service'
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
                if gate_runnable_specs or gate_connecting:
                    gate_active.set()
                if gate_connecting:
                    connect_gate_enabled.set()
                snapshot = None
                if not use_default_plan and unload_first and experience_enabled:
                    submit = node.create_client(SubmitFabricPlan, '/fabric/plan/submit')
                    snapshot = node.create_client(GetFabricPlanSnapshot, '/fabric/plan/get_snapshot')
                    parse = node.create_client(ParsePlan, '/fabric/plan/parse')
                    status = node.create_client(GetPlanStatus, '/fabric/plan/get_status')
                    cancel = node.create_client(CancelFabricPlan, '/fabric/plan/cancel')
                    assert submit.wait_for_service(timeout_sec=5), 'No plan submission service'
                    assert snapshot.wait_for_service(timeout_sec=5), 'No plan snapshot service'
                    assert parse.wait_for_service(timeout_sec=5), 'No plan parse service'
                    assert status.wait_for_service(timeout_sec=5), 'No plan status service'
                    assert cancel.wait_for_service(timeout_sec=5), 'No plan cancellation service'

                    if not gate_runnable_specs and not gate_trigger and not gate_connecting:
                        unknown_status_request = GetPlanStatus.Request()
                        unknown_status_request.plan_id = 'missing-plan-id'
                        future = status.call_async(unknown_status_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Unknown plan status request timed out'
                        assert future.result().status.code == FabricStatus.UNKNOWN

                        unknown_cancel_request = CancelFabricPlan.Request()
                        unknown_cancel_request.plan_id = 'missing-plan-id'
                        future = cancel.call_async(unknown_cancel_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Unknown plan cancellation timed out'
                        assert not future.result().success

                    malformed_plan = '<Plan>'
                    parse_request = ParsePlan.Request()
                    parse_request.plan = malformed_plan
                    future = parse.call_async(parse_request)
                    rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                    assert future.done(), 'Malformed plan parse request timed out'
                    parsed = future.result()
                    assert not parsed.success
                    assert parsed.error
                    assert not parsed.connections

                    future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                    rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                    assert future.done(), 'Snapshot request before parse timed out'
                    before_parse = future.result()

                    parse_request.plan = (
                        '<Plan><Runner interface="demo/ParseOnly" '
                        'provider="demo/ParseOnlyProvider"/></Plan>'
                    )
                    future = parse.call_async(parse_request)
                    rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                    assert future.done(), 'Valid plan parse request timed out'
                    parsed = future.result()
                    assert parsed.success, parsed.error
                    assert parsed.connections
                    assert any(
                        connection.source.interface == 'demo/ParseOnly'
                        for connection in parsed.connections
                    )

                    future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                    rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                    assert future.done(), 'Snapshot request after parse timed out'
                    after_parse = future.result()
                    if not gate_trigger and not gate_connecting:
                        assert after_parse.has_plan == before_parse.has_plan
                        assert after_parse.plan_id == before_parse.plan_id
                        assert after_parse.status.code == before_parse.status.code

                    rejected_request = SubmitFabricPlan.Request()
                    rejected_request.plan = malformed_plan
                    future = submit.call_async(rejected_request)
                    rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                    assert future.done(), 'Malformed plan submission timed out'
                    rejected = future.result()
                    assert not rejected.plan_id
                    assert rejected.error

                    if gate_parsing:
                        trigger_entered.clear()
                        parsing_request = SubmitFabricPlan.Request()
                        parsing_request.plan = (
                            '<Plan><Runner interface="demo/ParseGate" '
                            'provider="demo/ParseGateProvider"/></Plan>'
                        )
                        future = submit.call_async(parsing_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Gated parsing submission timed out'
                        parsing_plan = future.result()
                        assert parsing_plan.plan_id, parsing_plan.error
                        deadline = time.monotonic() + 15
                        while not (tmp_path / 'parser_entered').exists() and time.monotonic() < deadline:
                            rclpy.spin_once(node, timeout_sec=0.1)
                        assert (tmp_path / 'parser_entered').exists(), 'Parser did not reach the test gate'
                        for _ in range(3):
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Snapshot read blocked during parsing'
                            parsing_snapshot = future.result()
                            assert parsing_snapshot.has_plan
                            assert parsing_snapshot.plan_id == parsing_plan.plan_id
                            assert parsing_snapshot.status.code == FabricStatus.PARSING
                            assert not parsing_snapshot.connections_complete
                            assert not parsing_snapshot.instances
                            assert not parsing_snapshot.connections
                            assert not trigger_entered.wait(timeout=0.2), 'Snapshot read triggered execution during parsing'
                        parse_log_offset = len(log_path.read_text())
                        (tmp_path / 'parser_release').touch()
                        deadline = time.monotonic() + 15
                        while time.monotonic() < deadline:
                            rclpy.spin_once(node, timeout_sec=0.1)
                            parse_log = log_path.read_text()[parse_log_offset:]
                            if 'Compatibility validation failed with error:' in parse_log:
                                break
                        assert 'Compatibility validation failed with error:' in parse_log, (
                            'Gated plan did not resume and reach validation failure'
                        )
                        deadline = time.monotonic() + 5
                        while time.monotonic() < deadline:
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Snapshot read blocked after parsing resumed'
                            if not future.result().has_plan:
                                break
                        assert not future.result().has_plan, 'Gated parsing plan remained active'

                    if gate_runnable_specs:
                        active_request = SubmitFabricPlan.Request()
                        if gate_recovery:
                            turtlebot_share = Path(get_package_share_directory('turtlebot_capabilities'))
                            active_request.plan = (turtlebot_share / 'plans/turtlebot_3.xml').read_text()
                        else:
                            active_request.plan = (
                                '<Plan><Control type="parallel_any">'
                                '<Runner interface="demo/GatedA" provider="demo/GatedProvider"/>'
                                '<Runner interface="demo/GatedB" provider="demo/GatedProvider"/>'
                                '</Control></Plan>'
                            )
                        future = submit.call_async(active_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Active-plan submission timed out'
                        active = future.result()
                        assert active.plan_id, active.error
                        gate_deadline = time.monotonic() + 15
                        active_snapshot = None
                        while time.monotonic() < gate_deadline:
                            assert gate_entered.wait(timeout=5), 'Runtime did not reach gated capability discovery'
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Snapshot read blocked during capability discovery'
                            active_snapshot = future.result()
                            if active_snapshot.has_plan and active_snapshot.plan_id == active.plan_id:
                                break
                            gate_returned.clear()
                            gate_release.set()
                            assert gate_returned.wait(timeout=5), 'Gated capability request did not resume'
                        assert active_snapshot is not None, 'No snapshot was observed during capability discovery'
                        assert active_snapshot.has_plan
                        assert active_snapshot.plan_id == active.plan_id
                        assert active_snapshot.status.code == FabricStatus.VALIDATING
                        if gate_recovery:
                            assert_recovery_graph(active_snapshot, active.plan_id)
                        else:
                            assert_parallel_start_graph(active_snapshot, active.plan_id)

                        queued_request = SubmitFabricPlan.Request()
                        queued_request.plan = '<NotPlan/>'
                        future = submit.call_async(queued_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Queued-plan submission timed out'
                        submitted = future.result()
                        assert submitted.plan_id, submitted.error

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked with a queued plan'
                        active_snapshot = future.result()
                        if gate_recovery:
                            assert_recovery_graph(active_snapshot, active.plan_id)
                        else:
                            assert_parallel_start_graph(active_snapshot, active.plan_id)
                        assert active_snapshot.plan_id != submitted.plan_id

                        queued_status_request = GetPlanStatus.Request()
                        queued_status_request.plan_id = submitted.plan_id
                        future = status.call_async(queued_status_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Queued-plan status request timed out'
                        assert future.result().status.code == FabricStatus.QUEUED

                        queued_cancel_request = CancelFabricPlan.Request()
                        queued_cancel_request.plan_id = submitted.plan_id
                        future = cancel.call_async(queued_cancel_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Queued-plan cancellation timed out'
                        assert future.result().success

                        future = status.call_async(queued_status_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Cancelled queued-plan status request timed out'
                        assert future.result().status.code == FabricStatus.CANCELLED

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked after queued cancellation'
                        active_snapshot = future.result()
                        assert active_snapshot.has_plan
                        assert active_snapshot.plan_id == active.plan_id
                        gate_release.set()
                    elif gate_trigger or gate_connecting:
                        active_request = SubmitFabricPlan.Request()
                        active_request.plan = (
                            '<Plan><Runner interface="fabric_capabilities/FabricCompletionRunner" '
                            'provider="fabric_capabilities/FabricCompletionRunner"/></Plan>'
                        )
                        future = submit.call_async(active_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Active-plan submission timed out'
                        active = future.result()
                        assert active.plan_id, active.error
                        if gate_connecting:
                            connect_deadline = time.monotonic() + 15
                            connecting_snapshot = None
                            while time.monotonic() < connect_deadline:
                                assert connect_gate_entered.wait(timeout=5), (
                                    'Runtime did not reach gated capability connection'
                                )
                                future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                                rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                                assert future.done(), 'Snapshot read blocked during capability connection'
                                connecting_snapshot = future.result()
                                if connecting_snapshot.has_plan and connecting_snapshot.plan_id == active.plan_id:
                                    break
                                connect_gate_returned.clear()
                                connect_gate_release.set()
                                assert connect_gate_returned.wait(timeout=5), (
                                    'Gated startup connection did not resume'
                                )
                                connect_gate_release.clear()
                                connect_gate_entered.clear()
                                connect_gate_returned.clear()
                                connect_gate_claimed.clear()
                            assert connecting_snapshot is not None, 'No snapshot was observed during capability connection'
                            graph_signature = assert_completion_graph(connecting_snapshot, active.plan_id)
                            assert connecting_snapshot.status.code == FabricStatus.CAPABILITY_CONNECTING
                            blocked_status_request = GetPlanStatus.Request()
                            blocked_status_request.plan_id = active.plan_id
                            blocked_status = status.call_async(blocked_status_request)
                            rclpy.spin_until_future_complete(node, blocked_status, timeout_sec=0.5)
                            assert not blocked_status.done(), 'Status unexpectedly returned while connection was gated'
                            trigger_entered.clear()
                            connection_count = len(connection_requests)
                            for _ in range(3):
                                future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                                rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                                assert future.done(), 'Repeated snapshot read blocked during capability connection'
                                repeated_snapshot = future.result()
                                assert repeated_snapshot.status.code == FabricStatus.CAPABILITY_CONNECTING
                                assert assert_completion_graph(repeated_snapshot, active.plan_id) == graph_signature
                                assert len(connection_requests) == connection_count, 'Snapshot read created a connection'
                                assert not trigger_entered.wait(timeout=0.2), 'Snapshot read triggered execution during connection'
                            connect_gate_enabled.clear()
                            connect_gate_release.set()
                            assert connect_gate_returned.wait(timeout=5), 'Active connection gate did not resume'
                            rclpy.spin_until_future_complete(node, blocked_status, timeout_sec=10)
                            assert blocked_status.done(), 'Status remained blocked after the connection gate was released'
                            assert blocked_status.result().status.code == FabricStatus.RUNNING
                        assert trigger_entered.wait(timeout=10), 'Runtime did not trigger the active plan'

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked while trigger was in flight'
                        running_snapshot = future.result()
                        assert_completion_graph(running_snapshot, active.plan_id)
                        assert running_snapshot.status.code == FabricStatus.RUNNING

                        active_status_request = GetPlanStatus.Request()
                        active_status_request.plan_id = active.plan_id
                        future = status.call_async(active_status_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Active plan status request timed out'
                        assert future.result().status.code == FabricStatus.RUNNING

                        trigger_entered.clear()
                        completion = node.create_client(CompleteFabric, '/fabric/plan/set_completion')
                        assert completion.wait_for_service(timeout_sec=5), 'No plan completion service'
                        complete_request = CompleteFabric.Request()
                        complete_request.plan_id = active.plan_id
                        future = completion.call_async(complete_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Active-plan completion timed out'
                        assert free_entered.wait(timeout=5), 'Completed-plan cleanup did not reach the test gate'

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked during completion cleanup'
                        completed_snapshot = future.result()
                        completed_graph = assert_completion_graph(completed_snapshot, active.plan_id)
                        assert completed_snapshot.status.code == FabricStatus.COMPLETED
                        for _ in range(3):
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Repeated snapshot read blocked during completion cleanup'
                            repeated_snapshot = future.result()
                            assert repeated_snapshot.status.code == FabricStatus.COMPLETED
                            assert assert_completion_graph(repeated_snapshot, active.plan_id) == completed_graph
                            if gate_trigger:
                                assert not trigger_entered.wait(timeout=0.2), 'Snapshot read triggered execution during cleanup'
                        free_release.set()

                        deadline = time.monotonic() + 5
                        while time.monotonic() < deadline:
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Snapshot read blocked after completed-plan cleanup'
                            if not future.result().has_plan:
                                break
                        assert not future.result().has_plan, 'Completed plan remained active after cleanup'

                        trigger_entered.clear()
                        free_entered.clear()
                        free_release.clear()
                        future = submit.call_async(active_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Cancellation-plan submission timed out'
                        cancelled_plan = future.result()
                        assert cancelled_plan.plan_id, cancelled_plan.error
                        assert trigger_entered.wait(timeout=10), 'Runtime did not trigger the cancellation plan'

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked for the cancellation plan'
                        running_snapshot = future.result()
                        assert_completion_graph(running_snapshot, cancelled_plan.plan_id)
                        assert running_snapshot.status.code == FabricStatus.RUNNING

                        active_cancel_request = CancelFabricPlan.Request()
                        active_cancel_request.plan_id = cancelled_plan.plan_id
                        trigger_entered.clear()
                        future = cancel.call_async(active_cancel_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Active-plan cancellation timed out'
                        assert future.result().success
                        assert free_entered.wait(timeout=5), 'Active-plan cleanup did not reach the test gate'

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked during cancellation cleanup'
                        cancelled_snapshot = future.result()
                        cancelled_graph = assert_completion_graph(cancelled_snapshot, cancelled_plan.plan_id)
                        assert cancelled_snapshot.status.code == FabricStatus.CANCELLED
                        for _ in range(3):
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Repeated snapshot read blocked during cancellation cleanup'
                            repeated_snapshot = future.result()
                            assert repeated_snapshot.status.code == FabricStatus.CANCELLED
                            assert assert_completion_graph(repeated_snapshot, cancelled_plan.plan_id) == cancelled_graph
                            if gate_trigger:
                                assert not trigger_entered.wait(timeout=0.2), 'Snapshot read triggered execution during cleanup'
                        free_release.set()

                        deadline = time.monotonic() + 5
                        while time.monotonic() < deadline:
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Snapshot read blocked after cancelled-plan cleanup'
                            if not future.result().has_plan:
                                break
                        assert not future.result().has_plan, 'Cancelled plan remained active after cleanup'

                        trigger_entered.clear()
                        free_entered.clear()
                        free_release.clear()
                        future = submit.call_async(active_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Parse-failure predecessor submission timed out'
                        predecessor = future.result()
                        assert predecessor.plan_id, predecessor.error
                        assert trigger_entered.wait(timeout=10), 'Runtime did not trigger the parse-failure predecessor'

                        failure_request = SubmitFabricPlan.Request()
                        failure_request.plan = '<NotPlan/>'
                        future = submit.call_async(failure_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Parse-failure plan submission timed out'
                        failed_plan = future.result()
                        assert failed_plan.plan_id, failed_plan.error

                        complete_request.plan_id = predecessor.plan_id
                        trigger_entered.clear()
                        future = completion.call_async(complete_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Parse-failure predecessor completion timed out'
                        assert free_entered.wait(timeout=5), 'Predecessor cleanup did not reach the parse-failure gate'

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked during parse-failure cleanup'
                        failed_snapshot = future.result()
                        assert failed_snapshot.has_plan
                        assert failed_snapshot.plan_id == failed_plan.plan_id
                        assert failed_snapshot.status.code == FabricStatus.PARSE_FAILED
                        assert not failed_snapshot.connections_complete
                        assert not failed_snapshot.connections
                        assert not failed_snapshot.instances
                        for _ in range(3):
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Repeated snapshot read blocked during parse-failure cleanup'
                            repeated_snapshot = future.result()
                            assert repeated_snapshot.has_plan
                            assert repeated_snapshot.plan_id == failed_plan.plan_id
                            assert repeated_snapshot.status.code == FabricStatus.PARSE_FAILED
                            assert not repeated_snapshot.connections_complete
                            assert not repeated_snapshot.connections
                            assert not repeated_snapshot.instances
                            if gate_trigger:
                                assert not trigger_entered.wait(timeout=0.2), 'Snapshot read triggered execution after parse failure'
                        free_release.set()

                        deadline = time.monotonic() + 5
                        while time.monotonic() < deadline:
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Snapshot read blocked after parse-failure cleanup'
                            if not future.result().has_plan:
                                break
                        assert not future.result().has_plan, 'Parse-failed plan remained active after cleanup'

                        if gate_parsing:
                            trigger_entered.clear()
                            free_entered.clear()
                            free_release.clear()
                            stop_request = SubmitFabricPlan.Request()
                            stop_request.plan = (
                                '<Plan><!-- fabric-test-stop-edge -->'
                                '<Runner interface="fabric_capabilities/FabricCompletionRunner" '
                                'provider="fabric_capabilities/FabricCompletionRunner"/></Plan>'
                            )
                            future = submit.call_async(stop_request)
                            rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                            assert future.done(), 'Stop-edge plan submission timed out'
                            stop_plan = future.result()
                            assert stop_plan.plan_id, stop_plan.error
                            assert trigger_entered.wait(timeout=10), 'Stop-edge plan did not reach running'
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Stop-edge running snapshot timed out'
                            stop_snapshot = future.result()
                            assert stop_snapshot.status.code == FabricStatus.RUNNING
                            stop_graph = assert_completion_graph(stop_snapshot, stop_plan.plan_id, has_stop_edge=True)
                            source, target = sorted(stop_snapshot.instances, key=lambda instance: instance.instance_id)
                            stop_connections = [
                                request for request in connection_requests
                                if request.connection.ownership_id == stop_plan.plan_id
                            ]
                            assert [request.connection.type.code for request in stop_connections] == [
                                CapabilityEventCode.STOPPED, CapabilityEventCode.SUCCEEDED,
                            ]
                            for connection_request in stop_connections:
                                assert connection_request.bond_id == stop_snapshot.bond_id
                                assert connection_request.connection.source.capability == source.interface
                                assert connection_request.connection.source.provider == source.provider
                                assert connection_request.connection.source.instance_id == str(source.instance_id)
                                assert connection_request.connection.target.capability == target.interface
                                assert connection_request.connection.target.provider == target.provider
                                assert connection_request.connection.target.instance_id == str(target.instance_id)

                            trigger_entered.clear()
                            complete_request.plan_id = stop_plan.plan_id
                            future = completion.call_async(complete_request)
                            rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                            assert future.done(), 'Stop-edge plan completion timed out'
                            assert free_entered.wait(timeout=5), 'Stop-edge cleanup did not reach the test gate'
                            for _ in range(3):
                                future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                                rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                                assert future.done(), 'Stop-edge cleanup snapshot timed out'
                                repeated_snapshot = future.result()
                                assert repeated_snapshot.status.code == FabricStatus.COMPLETED
                                assert assert_completion_graph(
                                    repeated_snapshot, stop_plan.plan_id, has_stop_edge=True
                                ) == stop_graph
                                assert not trigger_entered.wait(timeout=0.2), 'Stop-edge snapshot triggered execution'
                            assert len([
                                request for request in connection_requests
                                if request.connection.ownership_id == stop_plan.plan_id
                            ]) == 2, 'Snapshot reads created extra connections'
                            free_release.set()
                            deadline = time.monotonic() + 5
                            while time.monotonic() < deadline:
                                future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                                rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                                assert future.done(), 'Snapshot read blocked after stop-edge cleanup'
                                if not future.result().has_plan:
                                    break
                            assert not future.result().has_plan, 'Stop-edge plan remained active after cleanup'

                        if gate_connecting:
                            trigger_entered.clear()
                            free_entered.clear()
                            free_release.clear()
                            connect_failure_enabled.set()
                            future = submit.call_async(active_request)
                            rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                            assert future.done(), 'Connection-failure submission timed out'
                            connection_failure = future.result()
                            assert connection_failure.plan_id, connection_failure.error
                            assert free_entered.wait(timeout=10), 'Connection failure did not reach cleanup'
                            graph_signature = None
                            for _ in range(3):
                                future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                                rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                                assert future.done(), 'Snapshot read blocked during connection-failure cleanup'
                                failed_snapshot = future.result()
                                assert failed_snapshot.status.code == FabricStatus.CAPABILITY_CONNECT_FAILED
                                failed_graph = assert_completion_graph(failed_snapshot, connection_failure.plan_id)
                                if graph_signature is None:
                                    graph_signature = failed_graph
                                assert failed_graph == graph_signature
                                assert not trigger_entered.wait(timeout=0.2), 'Failed connection triggered execution'
                            free_release.set()
                            deadline = time.monotonic() + 5
                            while time.monotonic() < deadline:
                                future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                                rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                                assert future.done(), 'Snapshot read blocked after connection-failure cleanup'
                                if not future.result().has_plan:
                                    break
                            assert not future.result().has_plan, 'Connection-failed plan remained active'
                    if not gate_runnable_specs and not gate_trigger and not gate_connecting:
                        submit_request = SubmitFabricPlan.Request()
                        submit_request.plan = '<NotPlan/>'
                        future = submit.call_async(submit_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Plan submission timed out'
                        submitted = future.result()
                        assert submitted.plan_id, submitted.error

                    if not gate_runnable_specs and not gate_trigger and not gate_connecting:
                        deadline = time.monotonic() + 10
                        observed = None
                        plan_started = False
                        parse_failure_logged = False
                        while time.monotonic() < deadline:
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Plan snapshot service timed out during transition'
                            observed = future.result()
                            if observed.has_plan and observed.plan_id == submitted.plan_id:
                                assert observed.status.code != FabricStatus.QUEUED
                                if observed.status.code == FabricStatus.PARSE_FAILED:
                                    parse_failure_logged = True
                            log_text = log_path.read_text()
                            start_marker = f'processing starting with id: {submitted.plan_id}'
                            start_offset = log_text.find(start_marker)
                            plan_started = plan_started or start_offset >= 0
                            if start_offset >= 0:
                                plan_log = log_text[start_offset + len(start_marker):]
                                parse_failure_logged = parse_failure_logged or (
                                    'Fabric plan parsing failed with error:' in plan_log
                                )
                            if parse_failure_logged:
                                break
                            rclpy.spin_once(node, timeout_sec=0.05)
                        assert observed is not None, 'Plan snapshot service returned no response'
                        assert plan_started, 'Submitted plan never entered the runtime execution process'
                        assert parse_failure_logged, 'Submitted plan did not reach the expected parse failure'
                        while (
                            observed.has_plan
                            and (observed.plan_id != submitted.plan_id or observed.status.code != FabricStatus.PARSE_FAILED)
                            and time.monotonic() < deadline
                        ):
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Plan snapshot service timed out after parse failure'
                            observed = future.result()
                        if observed.has_plan:
                            assert observed.plan_id == submitted.plan_id
                            assert observed.status.code == FabricStatus.PARSE_FAILED
                            assert not observed.connections_complete
                            assert not observed.connections
                        else:
                            assert not observed.plan_id
                            assert observed.status.code == FabricStatus.UNKNOWN

                if not experience_enabled:
                    status_topic = '/fabric_test/status'
                    deadline = time.monotonic() + 5
                    while node.count_publishers(status_topic) == 0 and time.monotonic() < deadline:
                        rclpy.spin_once(node, timeout_sec=0.1)
                    assert node.count_publishers(status_topic) > 0
                if use_default_plan:
                    assert f'Plan loaded from : {fabric_share / "plans/default.xml"}' in log_path.read_text()
                get_parameters = node.create_client(
                    GetParameters, '/fabric_component_probe/get_parameters')
                assert get_parameters.wait_for_service(timeout_sec=5)
                parameter_request = GetParameters.Request()
                parameter_request.names = [
                    'experience.KnowledgeCoreGraphRag.edge.parameter.min_score',
                    'experience.PlanningPlugin.model_family',
                    'experience.RapfsScorerPlugin.estimators.success.weight',
                    'experience.SuccessProbabilityPlugin.alpha',
                    'experience.BatteryMonitor.topic',
                    'experience.RosoutMonitor.model',
                ]
                future = get_parameters.call_async(parameter_request)
                rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                assert future.done(), 'Parameter lookup timed out'
                if experience_enabled:
                    values = future.result().values
                    assert values[0].double_value == 0.42
                    assert values[1].string_value == 'test_family'
                    assert values[2].double_value == 0.73
                    assert values[3].double_value == 3.0
                    assert values[4].string_value == '/fabric_test/battery'
                    assert values[5].string_value == 'monitor-test-model'
                list_parameters = node.create_client(
                    ListParameters, '/fabric_component_probe/list_parameters')
                assert list_parameters.wait_for_service(timeout_sec=5)
                list_request = ListParameters.Request()
                list_request.depth = ListParameters.Request.DEPTH_RECURSIVE
                future = list_parameters.call_async(list_request)
                rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                assert future.done(), 'Parameter listing timed out'
                configured = {parameter.name for parameter in flatten_parameters(parameters)}
                missing = {
                    name for name in future.result().result.names
                    if name not in configured
                    and name != 'start_type_description_service'
                    and name != 'capability_client.bond_topic'
                    and name != 'bond.topic'
                    and name.lstrip('/') != 'bond_disable_heartbeat_timeout'
                    and not name.startswith('qos_overrides.')
                }
                assert not missing, f'Parameters absent from Fabric configuration: {sorted(missing)}'
                if unload_first:
                    unload_request = UnloadNode.Request()
                    unload_request.unique_id = response.unique_id
                    if gate_runnable_specs and not gate_recovery:
                        assert gate_returned.wait(timeout=5), 'Previous discovery gate did not return'
                        deadline = time.monotonic() + 10
                        while time.monotonic() < deadline:
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            assert future.done(), 'Snapshot read blocked before discovery shutdown test'
                            if not future.result().has_plan:
                                break
                        assert not future.result().has_plan, 'Previous plan remained active before shutdown test'
                        gate_entered.clear()
                        gate_release.clear()
                        gate_returned.clear()
                        future = submit.call_async(active_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Discovery shutdown-plan submission timed out'
                        shutdown_plan = future.result()
                        assert shutdown_plan.plan_id, shutdown_plan.error
                        assert gate_entered.wait(timeout=10), 'Shutdown plan did not reach discovery gate'
                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked before discovery shutdown'
                        assert_parallel_start_graph(future.result(), shutdown_plan.plan_id)
                        assert future.result().status.code == FabricStatus.VALIDATING
                    if gate_parsing:
                        (tmp_path / 'parser_entered').unlink()
                        (tmp_path / 'parser_release').unlink()
                        trigger_entered.clear()
                        future = submit.call_async(parsing_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Shutdown-plan submission timed out'
                        shutdown_plan = future.result()
                        assert shutdown_plan.plan_id, shutdown_plan.error
                        deadline = time.monotonic() + 15
                        while not (tmp_path / 'parser_entered').exists() and time.monotonic() < deadline:
                            rclpy.spin_once(node, timeout_sec=0.1)
                        assert (tmp_path / 'parser_entered').exists(), 'Shutdown plan did not reach parser gate'
                        shutdown_log_offset = len(log_path.read_text())
                        unload_future = unload.call_async(unload_request)
                        deadline = time.monotonic() + 10
                        while time.monotonic() < deadline:
                            rclpy.spin_once(node, timeout_sec=0.1)
                            if 'Stopped monitor plugin:' in log_path.read_text()[shutdown_log_offset:]:
                                break
                        assert 'Stopped monitor plugin:' in log_path.read_text()[shutdown_log_offset:], (
                            'Component shutdown did not start'
                        )
                        assert not unload_future.done(), 'Shutdown finished before the parser gate was released'
                        for _ in range(3):
                            future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                            rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                            if future.done():
                                shutdown_snapshot = future.result()
                                assert shutdown_snapshot.has_plan
                                assert shutdown_snapshot.plan_id == shutdown_plan.plan_id
                                assert shutdown_snapshot.status.code == FabricStatus.PARSING
                                assert not shutdown_snapshot.connections_complete
                                assert not shutdown_snapshot.instances
                                assert not shutdown_snapshot.connections
                            else:
                                future.cancel()
                            assert not trigger_entered.wait(timeout=0.2), 'Shutdown snapshot triggered execution'
                        (tmp_path / 'parser_release').touch()
                        future = unload_future
                    else:
                        future = unload.call_async(unload_request)
                    rclpy.spin_until_future_complete(node, future, timeout_sec=10)
                    assert future.done(), 'Component unload timed out'
                    assert future.result().success, future.result().error_message
                    if gate_runnable_specs and not gate_recovery:
                        assert not gate_returned.is_set(), 'Discovery gate returned before shutdown completed'
                        gate_release.set()
                    if gate_parsing:
                        shutdown_log = log_path.read_text()[shutdown_log_offset:]
                        assert '[server] Getting capabilities available in the system' not in shutdown_log, (
                            'Capability discovery started after shutdown began'
                        )
                        assert not trigger_entered.wait(timeout=0.2), 'Shutdown plan triggered execution'
                    if snapshot is not None:
                        deadline = time.monotonic() + 5
                        while snapshot.wait_for_service(timeout_sec=0.1) and time.monotonic() < deadline:
                            rclpy.spin_once(node, timeout_sec=0.05)
                        assert not snapshot.wait_for_service(timeout_sec=0.2), (
                            'Snapshot service remained available after Fabric unload'
                        )
                        if gate_trigger:
                            reload_log_offset = len(log_path.read_text())
                            future = load.call_async(request)
                            rclpy.spin_until_future_complete(node, future, timeout_sec=30)
                            assert future.done(), 'Component reload timed out'
                            reloaded = future.result()
                            assert reloaded.success, reloaded.error_message
                            assert snapshot.wait_for_service(timeout_sec=10), (
                                'Snapshot service did not return after Fabric reload'
                            )
                            deadline = time.monotonic() + 15
                            while time.monotonic() < deadline:
                                rclpy.spin_once(node, timeout_sec=0.1)
                                reload_log = log_path.read_text()[reload_log_offset:]
                                if 'Compatibility validation failed with error:' in reload_log:
                                    break
                            assert 'Compatibility validation failed with error:' in reload_log, (
                                'Reloaded Fabric did not finish validating the startup plan'
                            )
                            deadline = time.monotonic() + 15
                            recovered_snapshot = None
                            while time.monotonic() < deadline:
                                future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                                rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                                assert future.done(), 'Snapshot request timed out after Fabric reload'
                                recovered_snapshot = future.result()
                                if not recovered_snapshot.has_plan:
                                    break
                            assert recovered_snapshot is not None
                            assert not recovered_snapshot.has_plan, 'Reloaded Fabric did not become idle'
                            assert not recovered_snapshot.plan_id
                            assert recovered_snapshot.status.code == FabricStatus.UNKNOWN
                            assert not recovered_snapshot.instances
                            assert not recovered_snapshot.connections
                            unload_request.unique_id = reloaded.unique_id
                            future = unload.call_async(unload_request)
                            rclpy.spin_until_future_complete(node, future, timeout_sec=10)
                            assert future.done(), 'Reloaded component unload timed out'
                            assert future.result().success, future.result().error_message
            finally:
                gate_release.set()
                connect_gate_release.set()
                free_release.set()
                if gate_parsing:
                    (tmp_path / 'parser_release').touch()
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
        if experience_enabled:
            assert 'Integrated Fabric experience services are available.' in output, output
            assert 'Stopped monitor plugin: ResourceMonitor' in output, output
        else:
            assert 'Loading monitor plugin:' not in output, output
        if 'Bond started with capabilities server' in output:
            assert 'Bond stopped with capabilities server' in output, output
    finally:
        gate_release.set()
        connect_gate_release.set()
        free_release.set()
        if gate_executor is not None:
            gate_executor.shutdown(timeout_sec=2)
        if gate_thread is not None:
            gate_thread.join(timeout=2)
        if gate_node is not None:
            gate_node.destroy_node()
        for service in services:
            node.destroy_service(service)
        node.destroy_node()
        rclpy.shutdown()
