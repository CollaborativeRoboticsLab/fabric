import json
import sqlite3
import sys
from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

MODULE_PATH = (
    Path(__file__).resolve().parents[1]
    / 'scripts'
    / 'export_terminal_success_analysis.py'
)
SPEC = spec_from_file_location('export_terminal_success_analysis', MODULE_PATH)
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
    connection.commit()
    connection.close()


def test_build_report_proposes_thresholds_from_observed_feedback(tmp_path: Path):
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_feedback_db(db_path)

    feedback_root = tmp_path / 'feedback'
    for index, (request_id, score, success) in enumerate([
        ('req_1', 0.82, True),
        ('req_2', 0.68, True),
        ('req_3', 0.41, False),
        ('req_4', 0.22, False),
    ], start=1):
        execution_dir = feedback_root / f'execution_{index}'
        execution_dir.mkdir(parents=True)
        (execution_dir / 'generated_plans.json').write_text(
            json.dumps(
                {
                    'iteration_1': {
                        'selected_candidate_plan_id': f'plan_{index}',
                        f'plan_{index}': {
                            'planning_request_id': request_id,
                            'candidate_plan_id': f'plan_{index}',
                            'graph_hash': f'graph_{index}',
                            'selected': True,
                            'feasible': True,
                            'terminal_success': success,
                            'terminal_success_estimate': score,
                            'final_rapfs': score,
                            'reasoning': 'test',
                            'candidate_skill_name_list': ['demo'],
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
                        'planning_request_id': request_id,
                        'candidate_plan_id': f'plan_{index}',
                        'graph_hash': f'graph_{index}',
                        'outcome': 'success' if success else 'failure',
                        'feedback_success': success,
                        'feedback': 'operator review',
                    }
                },
                indent=2,
            ) + '\n',
            encoding='utf-8',
        )

    report = MODULE.build_report(feedback_root, db_path)

    assert report['summary']['sample_count'] == 4
    assert report['summary']['candidate_threshold_count'] >= 3
    assert report['proposal']['best_single_threshold'] is not None
    assert (
        report['proposal']['recommended_success_threshold']
        >= report['proposal']['recommended_failure_threshold']
    )
    assert report['threshold_candidates'][0]['balanced_accuracy'] >= 0.5
    assert report['summary']['calibration_label_version'] == 'terminal_success_calibration_v1'
    assert report['samples'][0]['calibrated_terminal_success_label'] in {
        'success',
        'failure',
        'marginal',
    }
    assert report['runtime_policy_update']['applied'] is False
