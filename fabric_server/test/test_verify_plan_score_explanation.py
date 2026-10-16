from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import sqlite3
import sys


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

MODULE_PATH = Path(__file__).resolve().parents[1] / 'scripts' / 'verify_plan_score_explanation.py'
SPEC = spec_from_file_location('verify_plan_score_explanation', MODULE_PATH)
assert SPEC is not None
assert SPEC.loader is not None
MODULE = module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def create_explanation_db(path: Path) -> None:
    connection = sqlite3.connect(path)
    connection.executescript(
        '''
        CREATE TABLE plan_node_score_explanation (
            planning_request_id TEXT,
            candidate_plan_id TEXT,
            graph_hash TEXT,
            candidate_node_id TEXT,
            skill_name TEXT,
            skill_id TEXT,
            requested_context_key TEXT,
            requested_context_class TEXT,
            selected_skill_id TEXT,
            selected_key TEXT,
            estimator_window TEXT,
            fallback_level TEXT,
            proxy_source TEXT,
            reason TEXT,
            semantic_familiarity REAL DEFAULT 0.0,
            semantic_relevant_capabilities TEXT DEFAULT '',
            semantic_related_entities TEXT DEFAULT '',
            semantic_dependencies TEXT DEFAULT '',
            semantic_plan_patterns TEXT DEFAULT '',
            semantic_fault_concepts TEXT DEFAULT '',
            semantic_community_summaries TEXT DEFAULT '',
            estimator_names TEXT DEFAULT '',
            estimator_values TEXT DEFAULT '',
            estimator_available TEXT DEFAULT '',
            estimator_reasons TEXT DEFAULT '',
            recorded_at_sec INTEGER,
            recorded_at_nanosec INTEGER,
            provenance TEXT
        );
        '''
    )
    connection.execute(
        'INSERT INTO plan_node_score_explanation VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
        (
            'req_1',
            'plan_1',
            'graph_1',
            'runner_1',
            'demo/DetectObject',
            'detect',
            'task:pick',
            'task:pick',
            'detect',
            'task:pick',
            'empirical',
            'context_key',
            'requested_skill',
            'matched semantic context',
            0.7,
            'detect,estimate',
            'apple,table',
            'camera_ready,workspace_clear',
            'detect_then_pick',
            'occlusion',
            'vision summary',
            'success_probability,runtime_seconds,uncertainty',
            '0.9,4.2,0.1',
            '1,1,1',
            'posterior,observed,interval',
            10,
            20,
            'test',
        ),
    )
    connection.commit()
    connection.close()


def test_build_report_succeeds_when_semantic_signal_is_present(tmp_path: Path):
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_explanation_db(db_path)

    row = MODULE.load_explanation_row(db_path, 'req_1', 'plan_1', 'graph_1', 'runner_1')
    report = MODULE.build_report(row)

    assert report['found'] is True
    assert report['success'] is True
    assert report['candidate_node_id'] == 'runner_1'
    assert 'semantic_relevant_capabilities' in report['populated_semantic_fields']
    assert 'semantic_community_summaries' in report['populated_semantic_fields']
    assert 'estimator_outputs_present' in report['populated_estimator_fields']


def test_build_report_fails_when_no_row_matches():
    report = MODULE.build_report({})

    assert report['found'] is False
    assert report['success'] is not True
