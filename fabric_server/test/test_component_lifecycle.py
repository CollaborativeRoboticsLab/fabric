import os
from pathlib import Path
import signal
import subprocess
import threading
import time

from ament_index_python.packages import get_package_share_directory
from capabilities2_msgs.srv import GetRunnableSpecs
from capabilities2_msgs.srv import FreeCapability as CapabilitiesFreeCapability
from capabilities2_msgs.srv import TriggerCapability
from composition_interfaces.srv import LoadNode, UnloadNode
from fabric_msgs.msg import FabricStatus
from fabric_msgs.srv import CancelFabricPlan, CompleteFabric, GetFabricPlanSnapshot, GetPlanStatus, ParsePlan, SubmitFabricPlan
from prompt_msgs.msg import Embedding as PromptEmbeddingMessage
from prompt_msgs.srv import Embedding, Prompt, Tokenize
import pytest
import rclpy
from rclpy.executors import SingleThreadedExecutor
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


@pytest.mark.parametrize('unload_first,use_default_plan,experience_enabled,gate_runnable_specs,gate_trigger', [
    (True, False, True, False, False), (True, False, True, True, False),
    (True, False, True, False, True), (False, False, True, False, False),
    (True, True, True, False, False), (True, False, False, False, False),
])
def test_component_initializes_and_cleans_up(
    tmp_path, unload_first, use_default_plan, experience_enabled, gate_runnable_specs, gate_trigger,
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
        if gate_runnable_specs or gate_trigger:
            gate_node = rclpy.create_node('fabric_runnable_specs_gate')

            if gate_runnable_specs:
                def hold_runnable_specs(_request, response):
                    if gate_active.is_set():
                        gate_entered.set()
                        gate_release.wait(timeout=30)
                    return response

                gate_node.create_service(
                    GetRunnableSpecs,
                    '/fabric_test/gated_get_runnable_specs',
                    hold_runnable_specs,
                )
            if gate_trigger:
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
                )
                gate_node.create_service(
                    CapabilitiesFreeCapability,
                    '/fabric_test/gated_free_capability',
                    hold_capability_cleanup,
                )
            gate_executor = SingleThreadedExecutor()
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
        capability_config = capability_share / 'config/capabilities.yaml'
        if gate_trigger:
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
                if gate_runnable_specs:
                    gate_active.set()
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

                    if not gate_runnable_specs and not gate_trigger:
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
                    if not gate_trigger:
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

                    if gate_runnable_specs:
                        active_request = SubmitFabricPlan.Request()
                        active_request.plan = (
                            '<Plan><Runner interface="demo/Gated" '
                            'provider="demo/GatedProvider"/></Plan>'
                        )
                        future = submit.call_async(active_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Active-plan submission timed out'
                        active = future.result()
                        assert active.plan_id, active.error
                        assert gate_entered.wait(timeout=5), 'Runtime did not reach gated capability discovery'

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked during capability discovery'
                        active_snapshot = future.result()
                        assert active_snapshot.has_plan
                        assert active_snapshot.plan_id == active.plan_id
                        assert active_snapshot.status.code == FabricStatus.VALIDATING
                        assert active_snapshot.connections_complete
                        assert any(
                            instance.interface == 'demo/Gated'
                            for instance in active_snapshot.instances
                        )

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
                        assert active_snapshot.has_plan
                        assert active_snapshot.plan_id == active.plan_id
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
                    elif gate_trigger:
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
                        assert trigger_entered.wait(timeout=10), 'Runtime did not trigger the active plan'

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked while trigger was in flight'
                        running_snapshot = future.result()
                        assert running_snapshot.has_plan
                        assert running_snapshot.plan_id == active.plan_id
                        assert running_snapshot.status.code == FabricStatus.RUNNING

                        active_status_request = GetPlanStatus.Request()
                        active_status_request.plan_id = active.plan_id
                        future = status.call_async(active_status_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Active plan status request timed out'
                        assert future.result().status.code == FabricStatus.RUNNING

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
                        assert completed_snapshot.has_plan
                        assert completed_snapshot.plan_id == active.plan_id
                        assert completed_snapshot.status.code == FabricStatus.COMPLETED
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
                        assert running_snapshot.has_plan
                        assert running_snapshot.plan_id == cancelled_plan.plan_id
                        assert running_snapshot.status.code == FabricStatus.RUNNING

                        active_cancel_request = CancelFabricPlan.Request()
                        active_cancel_request.plan_id = cancelled_plan.plan_id
                        future = cancel.call_async(active_cancel_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Active-plan cancellation timed out'
                        assert future.result().success
                        assert free_entered.wait(timeout=5), 'Active-plan cleanup did not reach the test gate'

                        future = snapshot.call_async(GetFabricPlanSnapshot.Request())
                        rclpy.spin_until_future_complete(node, future, timeout_sec=1)
                        assert future.done(), 'Snapshot read blocked during cancellation cleanup'
                        cancelled_snapshot = future.result()
                        assert cancelled_snapshot.has_plan
                        assert cancelled_snapshot.plan_id == cancelled_plan.plan_id
                        assert cancelled_snapshot.status.code == FabricStatus.CANCELLED
                        free_release.set()
                    if not gate_runnable_specs and not gate_trigger:
                        submit_request = SubmitFabricPlan.Request()
                        submit_request.plan = '<NotPlan/>'
                        future = submit.call_async(submit_request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        assert future.done(), 'Plan submission timed out'
                        submitted = future.result()
                        assert submitted.plan_id, submitted.error

                    if not gate_runnable_specs and not gate_trigger:
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
                        while observed.has_plan and observed.plan_id != submitted.plan_id and time.monotonic() < deadline:
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
                    and name.lstrip('/') != 'bond_disable_heartbeat_timeout'
                    and not name.startswith('qos_overrides.')
                }
                assert not missing, f'Parameters absent from Fabric configuration: {sorted(missing)}'
                if unload_first:
                    unload_request = UnloadNode.Request()
                    unload_request.unique_id = response.unique_id
                    future = unload.call_async(unload_request)
                    rclpy.spin_until_future_complete(node, future, timeout_sec=10)
                    assert future.done(), 'Component unload timed out'
                    assert future.result().success, future.result().error_message
                    if snapshot is not None:
                        deadline = time.monotonic() + 5
                        while snapshot.wait_for_service(timeout_sec=0.1) and time.monotonic() < deadline:
                            rclpy.spin_once(node, timeout_sec=0.05)
                        assert not snapshot.wait_for_service(timeout_sec=0.2), (
                            'Snapshot service remained available after Fabric unload'
                        )
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
        if experience_enabled:
            assert 'Integrated Fabric experience services are available.' in output, output
            assert 'Stopped monitor plugin: ResourceMonitor' in output, output
        else:
            assert 'Loading monitor plugin:' not in output, output
        if 'Bond started with capabilities server' in output:
            assert 'Bond stopped with capabilities server' in output, output
    finally:
        gate_release.set()
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
