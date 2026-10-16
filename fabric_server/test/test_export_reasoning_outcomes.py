from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path
import json
import sqlite3
import sys


PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

MODULE_PATH = Path(__file__).resolve().parents[1] / 'scripts' / 'export_reasoning_outcomes.py'
SPEC = spec_from_file_location('export_reasoning_outcomes', MODULE_PATH)
assert SPEC is not None
assert SPEC.loader is not None
MODULE = module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def create_feedback_db(path: Path) -> None:
    connection = sqlite3.connect(path)
    connection.executescript(
        '''
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
        '''
    )
    connection.execute(
        'INSERT INTO plan_feedback_statistics VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)',
        (
            'req_1',
            'plan_1',
            'graph_1',
            1,
            1,
            0.82,
            0.76,
            'success',
            1,
            5.2,
            0,
            0,
            'operator confirmed',
            'runtime=5.2;success=0.82',
            'c1',
            'n1',
            'e1',
            'w1',
            'test',
        ),
    )
    connection.commit()
    connection.close()


def test_build_rows_combines_generated_plans_feedback_and_db(tmp_path: Path):
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_feedback_db(db_path)

    feedback_root = tmp_path / 'feedback'
    execution_dir = feedback_root / 'execution_1'
    execution_dir.mkdir(parents=True)

    (execution_dir / 'generated_plans.json').write_text(
        json.dumps(
            {
                'iteration_1': {
                    'selected_candidate_plan_id': 'plan_1',
                    'plan_1': {
                        'planning_request_id': 'req_1',
                        'candidate_plan_id': 'plan_1',
                        'graph_hash': 'graph_1',
                        'selected': True,
                        'feasible': True,
                        'terminal_success': True,
                        'terminal_success_estimate': 0.82,
                        'final_rapfs': 0.76,
                        'expected_runtime_seconds': 5.0,
                        'expected_energy_delta_percentage': 0.0,
                        'expected_fault_burden': 0.1,
                        'expected_resource_demand': 0.2,
                        'expected_uncertainty': 0.15,
                        'reasoning': 'selected because similar evidence succeeded',
                        'candidate_skill_name_list': ['detect', 'pick'],
                    }
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
                    'feedback': 'operator confirmed goal completion',
                }
            },
            indent=2,
        ) + '\n',
        encoding='utf-8',
    )

    db_feedback = MODULE.load_db_feedback(db_path)
    candidate_rows, reasoning_rows, outcome_rows = MODULE.build_rows(feedback_root, db_feedback)

    assert len(candidate_rows) == 1
    assert len(reasoning_rows) == 1
    assert len(outcome_rows) == 1

    candidate = candidate_rows[0]
    assert candidate['selected'] is True
    assert candidate['terminal_success_predicted'] is True
    assert candidate['terminal_success_observed'] is True
    assert candidate['calibration_version'] == 'c1'

    reasoning = reasoning_rows[0]
    assert 'similar evidence succeeded' in reasoning['reasoning']
    assert reasoning['candidate_skill_name_list'] == 'detect,pick'

    outcome = outcome_rows[0]
    assert outcome['terminal_outcome'] == 'success'
    assert outcome['human_judgement'] == 'operator confirmed goal completion'
    assert outcome['predicted_success_probability'] == 0.82
    assert outcome['predicted_terminal_success_label'] == 'success'
    assert outcome['calibration_version'] == 'c1'
