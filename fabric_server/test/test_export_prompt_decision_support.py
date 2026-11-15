import json
import sqlite3
import sys
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

MODULE_PATH = Path(__file__).resolve().parents[1] / 'scripts' / 'export_prompt_decision_support.py'
SPEC = spec_from_file_location('export_prompt_decision_support', MODULE_PATH)
assert SPEC is not None
assert SPEC.loader is not None
MODULE = module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def create_feedback_db(path: Path) -> None:
    connection = sqlite3.connect(path)
    connection.executescript(
        """
        CREATE TABLE plan_feedback_statistics (
            planning_request_id TEXT,
            candidate_plan_id TEXT,
            graph_hash TEXT,
            selected INTEGER,
            feasible INTEGER,
            terminal_success_estimate REAL,
            final_rapfs REAL,
            terminal_outcome TEXT,
            terminal_success INTEGER,
            runtime_seconds REAL,
            warning_count INTEGER,
            error_count INTEGER,
            human_judgement TEXT,
            score_components TEXT,
            calibration_version TEXT,
            normalizer_version TEXT,
            estimator_version TEXT,
            weight_version TEXT,
            provenance TEXT
        );
        """
    )
    connection.execute(
        (
            'INSERT INTO plan_feedback_statistics VALUES '
            '(?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)'
        ),
        (
            'req_1',
            'plan_1',
            'graph_1',
            1,
            1,
            0.83,
            0.77,
            'success',
            1,
            5.0,
            0,
            0,
            'operator confirmed',
            '{}',
            'c1',
            'n1',
            'e1',
            'w1',
            'test',
        ),
    )
    connection.commit()
    connection.close()


def test_build_report_joins_prompt_iterations_with_selected_plan_feedback(tmp_path: Path):
    audit_root = tmp_path / 'planner_prompt_audit'
    audit_root.mkdir()
    (audit_root / 'req_1_10_20.json').write_text(
        json.dumps(
            {
                'planning_request_id': 'req_1',
                'request_uuid': 'uuid_1',
                'task_description': 'pick the apple',
                'iterations': {
                    'iteration_1': {
                        'phase': 'generation',
                        'selected_semantic_context_mode': 'detailed',
                        'prompt_comparison': {
                            'detailed_chars': 1500,
                            'community_chars': 900,
                            'reduction_ratio': 0.4,
                        },
                        'semantic_counts': {
                            'community_summaries': 1,
                            'relevant_capabilities': 2,
                        },
                    },
                    'iteration_2': {
                        'phase': 'refinement',
                        'selected_semantic_context_mode': 'community',
                        'prompt_comparison': {
                            'detailed_chars': 1600,
                            'community_chars': 950,
                            'reduction_ratio': 0.40625,
                        },
                        'semantic_counts': {
                            'community_summaries': 2,
                            'relevant_capabilities': 2,
                        },
                    },
                },
            },
            indent=2,
        ) + '\n',
        encoding='utf-8',
    )

    feedback_root = tmp_path / 'feedback'
    execution_dir = feedback_root / 'execution_1'
    execution_dir.mkdir(parents=True)
    (execution_dir / 'generated_plans.json').write_text(
        json.dumps(
            {
                'iteration_2': {
                    'selected_candidate_plan_id': 'plan_1',
                    'plan_1': {
                        'planning_request_id': 'req_1',
                        'candidate_plan_id': 'plan_1',
                        'graph_hash': 'graph_1',
                        'selected': True,
                        'feasible': True,
                        'terminal_success': True,
                        'terminal_success_estimate': 0.83,
                        'final_rapfs': 0.77,
                        'reasoning': 'community context was sufficient',
                        'candidate_skill_name_list': ['detect', 'pick'],
                    },
                }
            },
            indent=2,
        ) + '\n',
        encoding='utf-8',
    )
    (execution_dir / 'user_feedback.json').write_text(
        json.dumps(
            {
                '1': {
                    'planning_request_id': 'req_1',
                    'candidate_plan_id': 'plan_1',
                    'graph_hash': 'graph_1',
                    'outcome': 'success',
                    'feedback_success': True,
                    'feedback': 'operator confirmed',
                }
            },
            indent=2,
        ) + '\n',
        encoding='utf-8',
    )

    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_feedback_db(db_path)

    report = MODULE.build_report(audit_root, feedback_root, db_path)

    assert report['summary']['request_count'] == 1
    request = report['request_prompt_decision_support'][0]
    assert request['planning_request_id'] == 'req_1'
    assert request['final_selected_mode'] == 'community'
    assert request['selected_candidate_plan_id'] == 'plan_1'
    assert request['selected_observed_success'] is True
    assert request['selected_predicted_success_probability'] == 0.83
    assert request['selected_predicted_terminal_success_label'] == 'success'
    assert request['recommendation'] == 'favor_community'
