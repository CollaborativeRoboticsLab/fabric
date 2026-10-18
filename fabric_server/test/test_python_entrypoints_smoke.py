from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import types
import sys


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

SCRIPT_PATHS = [
    PACKAGE_ROOT / 'scripts' / 'export_core_graph_debug.py',
    PACKAGE_ROOT / 'scripts' / 'export_prompt_decision_support.py',
    PACKAGE_ROOT / 'scripts' / 'export_reasoning_outcomes.py',
    PACKAGE_ROOT / 'scripts' / 'export_retrieval_prompt_audit.py',
    PACKAGE_ROOT / 'scripts' / 'export_terminal_success_analysis.py',
    PACKAGE_ROOT / 'scripts' / 'get_plan_score_explanation.py',
    PACKAGE_ROOT / 'scripts' / 'get_scoring_epoch.py',
    PACKAGE_ROOT / 'scripts' / 'ingest_plan_feedback.py',
    PACKAGE_ROOT / 'scripts' / 'record_plan_feedback.py',
    PACKAGE_ROOT / 'scripts' / 'record_scoring_epoch.py',
    PACKAGE_ROOT / 'scripts' / 'replay_plan_score.py',
    PACKAGE_ROOT / 'scripts' / 'summarize_planner_prompt_audit.py',
    PACKAGE_ROOT / 'scripts' / 'verify_plan_score_explanation.py',
]


def install_ros_stubs() -> None:
    if 'rclpy' not in sys.modules:
        rclpy_module = types.ModuleType('rclpy')
        rclpy_module.init = lambda *args, **kwargs: None
        rclpy_module.shutdown = lambda *args, **kwargs: None
        rclpy_module.spin_until_future_complete = lambda *args, **kwargs: None
        sys.modules['rclpy'] = rclpy_module

    if 'rclpy.node' not in sys.modules:
        node_module = types.ModuleType('rclpy.node')

        class Node:
            def __init__(self, *args, **kwargs):
                pass

        node_module.Node = Node
        sys.modules['rclpy.node'] = node_module

    if 'fabric_msgs' not in sys.modules:
        sys.modules['fabric_msgs'] = types.ModuleType('fabric_msgs')

    if 'fabric_msgs.srv' not in sys.modules:
        srv_module = types.ModuleType('fabric_msgs.srv')

        def make_service_type(name: str):
            request_type = type('Request', (), {})
            return type(name, (), {'Request': request_type})

        for service_name in [
            'GetPlanScoreExplanation',
            'GetScoringEpoch',
            'RecordPlanFeedback',
            'RecordScoringEpoch',
            'ScorePlan',
        ]:
            setattr(srv_module, service_name, make_service_type(service_name))

        sys.modules['fabric_msgs.srv'] = srv_module


def load_module(script_path: Path):
    spec = spec_from_file_location(script_path.stem, script_path)
    assert spec is not None
    assert spec.loader is not None
    module = module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def test_python_entrypoints_import_and_expose_main():
    install_ros_stubs()
    for script_path in SCRIPT_PATHS:
        module = load_module(script_path)
        assert callable(getattr(module, 'main', None)), (
            f'{script_path.name} does not expose main()'
        )
