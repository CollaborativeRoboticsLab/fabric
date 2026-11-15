from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import sys


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

MODULE_PATH = Path(__file__).resolve().parents[1] / 'scripts' / 'export_retrieval_prompt_audit.py'
SPEC = spec_from_file_location('export_retrieval_prompt_audit', MODULE_PATH)
assert SPEC is not None
assert SPEC.loader is not None
MODULE = module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def test_split_field_ignores_empty_values():
    assert MODULE.split_field('') == []
    assert MODULE.split_field(None) == []
    assert MODULE.split_field('a,b,,c') == ['a', 'b', 'c']


def test_build_audit_reports_prompt_reduction_and_signal():
    rows = [
        {
            'planning_request_id': 'req-1',
            'candidate_plan_id': 'plan-1',
            'graph_hash': 'graph-1',
            'candidate_node_id': 'node-1',
            'skill_name': 'demo/DetectObject',
            'fallback_level': 'direct',
            'semantic_familiarity': 0.8,
            'semantic_relevant_capabilities': 'detect_object,estimate_pose',
            'semantic_related_entities': 'apple,table',
            'semantic_dependencies': 'camera_ready,workspace_clear',
            'semantic_plan_patterns': 'detect_then_pick',
            'semantic_fault_concepts': 'occlusion',
            'semantic_community_summaries': 'vision pipeline summary',
            'reason': 'matched prior graph evidence',
        },
        {
            'planning_request_id': 'req-1',
            'candidate_plan_id': 'plan-1',
            'graph_hash': 'graph-1',
            'candidate_node_id': 'node-2',
            'skill_name': 'demo/GraspObject',
            'fallback_level': 'context_class',
            'semantic_familiarity': 0.4,
            'semantic_relevant_capabilities': 'grasp_object',
            'semantic_related_entities': 'gripper',
            'semantic_dependencies': 'object_pose',
            'semantic_plan_patterns': 'pick_sequence',
            'semantic_fault_concepts': 'slip',
            'semantic_community_summaries': 'manipulation summary',
            'reason': 'used community fallback',
        },
    ]

    audit = MODULE.build_audit(rows)

    assert audit['summary']['node_explanation_count'] == 2
    assert audit['summary']['plan_count'] == 1
    assert audit['summary']['nodes_with_community_signal'] == 2
    assert len(audit['node_prompt_audit']) == 2
    assert len(audit['plan_prompt_audit']) == 1

    first_node = audit['node_prompt_audit'][0]
    assert first_node['community_has_signal'] is True
    assert first_node['prompt_reduction_chars'] > 0
    assert first_node['prompt_reduction_ratio'] > 0.0

    plan_row = audit['plan_prompt_audit'][0]
    assert plan_row['node_count'] == 2
    assert plan_row['community_coverage_ratio'] == 1.0
    assert plan_row['prompt_reduction_chars'] > 0
