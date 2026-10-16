from __future__ import annotations

import argparse
import csv
import json
import math
import sqlite3
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


def safe_float(value: Any) -> Optional[float]:
    if value is None:
        return None
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


def load_json(path: Path) -> Dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def iter_execution_dirs(feedback_root: Path) -> Iterable[Path]:
    if not feedback_root.exists():
        return []
    return sorted(path for path in feedback_root.iterdir() if path.is_dir())


def stage_entries(document: Dict[str, Any]) -> Iterable[Tuple[str, Dict[str, Any]]]:
    def stage_key(item: Tuple[str, Any]) -> Tuple[int, str]:
        key, _ = item
        try:
            return int(key), key
        except ValueError:
            return 10**9, key

    for key, value in sorted(document.items(), key=stage_key):
        if isinstance(value, dict):
            yield key, value


def plan_entries(iteration: Dict[str, Any]) -> Iterable[Tuple[str, Dict[str, Any]]]:
    def plan_key(item: Tuple[str, Any]) -> Tuple[int, str]:
        key, _ = item
        if key.startswith('plan_'):
            try:
                return int(key.split('_', 1)[1]), key
            except ValueError:
                pass
        return 10**9, key

    for key, value in sorted(iteration.items(), key=plan_key):
        if key.startswith('plan_') and isinstance(value, dict):
            yield key, value


def feedback_success(entry: Dict[str, Any]) -> Optional[bool]:
    if isinstance(entry.get('feedback_success'), bool):
        return bool(entry['feedback_success'])
    outcome = str(entry.get('outcome') or entry.get('terminal_outcome') or '').lower().strip()
    if outcome in {'success', 'succeeded', 'completed'}:
        return True
    if outcome in {'failure', 'failed', 'partial', 'partial_success', 'cancelled', 'aborted', 'timeout'}:
        return False
    return None


def load_db_feedback(db_path: Optional[Path]) -> Dict[Tuple[str, str, str], Dict[str, Any]]:
    if db_path is None or not db_path.is_file():
        return {}
    connection = sqlite3.connect(db_path)
    try:
        rows = connection.execute(
            'SELECT planning_request_id, candidate_plan_id, graph_hash, selected, feasible, '
            'terminal_success_estimate, final_rapfs, terminal_outcome, terminal_success, runtime_seconds, '
            'warning_count, error_count, human_judgement, score_components, calibration_version, normalizer_version, '
            'estimator_version, weight_version, provenance FROM plan_feedback_statistics;'
        ).fetchall()
    except sqlite3.Error:
        connection.close()
        return {}

    columns = [
        'planning_request_id', 'candidate_plan_id', 'graph_hash', 'selected', 'feasible',
        'terminal_success_estimate', 'final_rapfs', 'terminal_outcome', 'terminal_success', 'runtime_seconds',
        'warning_count', 'error_count', 'human_judgement', 'score_components', 'calibration_version',
        'normalizer_version', 'estimator_version', 'weight_version', 'provenance',
    ]
    output: Dict[Tuple[str, str, str], Dict[str, Any]] = {}
    for row in rows:
        record = dict(zip(columns, row))
        key = (record['planning_request_id'], record['candidate_plan_id'], record['graph_hash'])
        output[key] = record
    connection.close()
    return output


def build_rows(feedback_root: Path, db_feedback: Dict[Tuple[str, str, str], Dict[str, Any]]) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]], List[Dict[str, Any]]]:
    candidate_rows: List[Dict[str, Any]] = []
    reasoning_rows: List[Dict[str, Any]] = []
    outcome_rows: List[Dict[str, Any]] = []

    for execution_dir in iter_execution_dirs(feedback_root):
        execution_key = execution_dir.name
        generated = load_json(execution_dir / 'generated_plans.json')
        feedback = load_json(execution_dir / 'user_feedback.json')

        selected_feedback_by_key: Dict[Tuple[str, str, str], Dict[str, Any]] = {}
        for stage_key, entry in stage_entries(feedback):
            key = (
                str(entry.get('planning_request_id') or ''),
                str(entry.get('candidate_plan_id') or ''),
                str(entry.get('graph_hash') or ''),
            )
            selected_feedback_by_key[key] = {'stage': stage_key, **entry}

        for iteration_key, iteration in stage_entries(generated):
            if not iteration_key.startswith('iteration_'):
                continue
            selected_candidate_id = str(iteration.get('selected_candidate_plan_id') or '')
            for plan_key, plan in plan_entries(iteration):
                key = (
                    str(plan.get('planning_request_id') or ''),
                    str(plan.get('candidate_plan_id') or ''),
                    str(plan.get('graph_hash') or ''),
                )
                db_record = db_feedback.get(key, {})
                user_record = selected_feedback_by_key.get(key, {})
                observed_success = feedback_success(user_record) if user_record else feedback_success(db_record)
                predicted_success = plan.get('terminal_success')
                if not isinstance(predicted_success, bool):
                    predicted_success = bool(plan.get('feasible'))
                predicted_success_probability = safe_float(plan.get('terminal_success_estimate'))

                candidate_row = {
                    'execution_key': execution_key,
                    'iteration': iteration_key,
                    'plan_key': plan_key,
                    'planning_request_id': key[0],
                    'candidate_plan_id': key[1],
                    'graph_hash': key[2],
                    'selected': bool(plan.get('selected')) or key[1] == selected_candidate_id,
                    'ranking_index': plan.get('ranking_index'),
                    'feasible': bool(plan.get('feasible')),
                    'terminal_success_predicted': predicted_success,
                    'predicted_terminal_success_label': 'success' if predicted_success else 'failure',
                    'terminal_success_observed': observed_success,
                    'terminal_success_estimate': predicted_success_probability,
                    'predicted_success_probability': predicted_success_probability,
                    'final_rapfs': safe_float(plan.get('final_rapfs')),
                    'expected_runtime_seconds': safe_float(plan.get('expected_runtime_seconds')),
                    'expected_energy_delta_percentage': safe_float(plan.get('expected_energy_delta_percentage')),
                    'expected_fault_burden': safe_float(plan.get('expected_fault_burden')),
                    'expected_resource_demand': safe_float(plan.get('expected_resource_demand')),
                    'expected_uncertainty': safe_float(plan.get('expected_uncertainty')),
                    'feasibility_reason': plan.get('feasibility_reason') or plan.get('rejection_reason') or '',
                    'terminal_outcome': user_record.get('outcome') or db_record.get('terminal_outcome') or '',
                    'runtime_seconds_observed': safe_float(db_record.get('runtime_seconds')),
                    'warning_count_observed': db_record.get('warning_count'),
                    'error_count_observed': db_record.get('error_count'),
                    'calibration_version': plan.get('calibration_version') or db_record.get('calibration_version') or '',
                    'normalizer_version': plan.get('normalizer_version') or db_record.get('normalizer_version') or '',
                    'estimator_version': plan.get('estimator_version') or db_record.get('estimator_version') or '',
                    'weight_version': plan.get('weight_version') or db_record.get('weight_version') or '',
                }
                candidate_rows.append(candidate_row)

                reasoning_rows.append(
                    {
                        'execution_key': execution_key,
                        'iteration': iteration_key,
                        'plan_key': plan_key,
                        'planning_request_id': key[0],
                        'candidate_plan_id': key[1],
                        'selected': candidate_row['selected'],
                        'feasible': candidate_row['feasible'],
                        'final_rapfs': candidate_row['final_rapfs'],
                        'reasoning': plan.get('reasoning') or '',
                        'feasibility_reason': candidate_row['feasibility_reason'],
                        'candidate_skill_name_list': ','.join(plan.get('candidate_skill_name_list') or []),
                    }
                )

                if candidate_row['selected'] or observed_success is not None:
                    outcome_rows.append(
                        {
                            'execution_key': execution_key,
                            'iteration': iteration_key,
                            'planning_request_id': key[0],
                            'candidate_plan_id': key[1],
                            'graph_hash': key[2],
                            'predicted_success': predicted_success,
                            'predicted_terminal_success_label': 'success' if predicted_success else 'failure',
                            'observed_success': observed_success,
                            'terminal_success_estimate': candidate_row['terminal_success_estimate'],
                            'predicted_success_probability': candidate_row['predicted_success_probability'],
                            'final_rapfs': candidate_row['final_rapfs'],
                            'terminal_outcome': candidate_row['terminal_outcome'],
                            'human_judgement': user_record.get('feedback') or db_record.get('human_judgement') or '',
                            'calibration_version': candidate_row['calibration_version'],
                            'normalizer_version': candidate_row['normalizer_version'],
                            'estimator_version': candidate_row['estimator_version'],
                            'weight_version': candidate_row['weight_version'],
                        }
                    )

    return candidate_rows, reasoning_rows, outcome_rows


def build_reasoning_outcome_rows(feedback_root: Path, db_path: Optional[Path]) -> Dict[str, Any]:
    db_feedback = load_db_feedback(db_path)
    candidate_rows, reasoning_rows, outcome_rows = build_rows(feedback_root, db_feedback)
    return {
        'summary': {
            'execution_count': len(list(iter_execution_dirs(feedback_root))),
            'candidate_count': len(candidate_rows),
            'reasoning_count': len(reasoning_rows),
            'outcome_count': len(outcome_rows),
            'db_feedback_rows': len(db_feedback),
        },
        'expected_score_comparisons': candidate_rows,
        'reasoning_summaries': reasoning_rows,
        'outcome_vs_prediction': outcome_rows,
    }


def write_csv(path: Path, rows: List[Dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    if not rows:
        path.write_text('', encoding='utf-8')
        return
    columns = list(rows[0].keys())
    with path.open('w', encoding='utf-8', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows)


def write_json(path: Path, payload: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def export_reasoning_outcomes(feedback_root: Path, db_path: Optional[Path], out_dir: Path) -> Dict[str, Any]:
    payload = build_reasoning_outcome_rows(feedback_root, db_path)
    write_json(out_dir / 'reasoning_outcomes.json', payload)
    write_csv(out_dir / 'expected_score_comparisons.csv', payload['expected_score_comparisons'])
    write_csv(out_dir / 'reasoning_summaries.csv', payload['reasoning_summaries'])
    write_csv(out_dir / 'outcome_vs_prediction.csv', payload['outcome_vs_prediction'])
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Export reasoning summaries and outcome-vs-prediction tables from Experience artifacts.'
    )
    parser.add_argument('feedback_root', type=Path, help='Root containing per-execution user_feedback/generated_plans files')
    parser.add_argument('--db', type=Path, default=None, help='Optional core_graphrag.sqlite3 for persisted feedback rows')
    parser.add_argument('--out-dir', type=Path, default=Path('experience_analysis_exports'))
    args = parser.parse_args()

    payload = export_reasoning_outcomes(args.feedback_root, args.db, args.out_dir)
    print(
        f"wrote {args.out_dir} candidates={payload['summary']['candidate_count']} "
        f"reasoning={payload['summary']['reasoning_count']} outcomes={payload['summary']['outcome_count']}"
    )
    return 0


__all__ = [
    'build_reasoning_outcome_rows',
    'build_rows',
    'export_reasoning_outcomes',
    'feedback_success',
    'iter_execution_dirs',
    'load_db_feedback',
    'load_json',
    'main',
    'plan_entries',
    'safe_float',
    'stage_entries',
    'write_csv',
    'write_json',
]