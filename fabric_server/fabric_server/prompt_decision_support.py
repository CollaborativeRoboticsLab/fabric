from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
import sqlite3
from typing import Any, Dict, Iterable, List, Optional, Tuple


def load_json(path: Path) -> Dict[str, Any]:
    try:
        data = json.loads(path.read_text(encoding='utf-8'))
    except (OSError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def iter_audit_files(audit_root: Path) -> Iterable[Path]:
    if not audit_root.exists():
        return []
    return sorted(path for path in audit_root.iterdir() if path.is_file() and path.suffix == '.json')


def iter_execution_dirs(feedback_root: Path) -> Iterable[Path]:
    if not feedback_root.exists():
        return []
    return sorted(path for path in feedback_root.iterdir() if path.is_dir())


def iteration_sort_key(item: Tuple[str, Any]) -> Tuple[int, str]:
    key, _ = item
    if key.startswith('iteration_'):
        try:
            return int(key.split('_', 1)[1]), key
        except ValueError:
            pass
    return 10**9, key


def stage_key(item: Tuple[str, Any]) -> Tuple[int, str]:
    key, _ = item
    try:
        return int(key), key
    except ValueError:
        return 10**9, key


def plan_key(item: Tuple[str, Any]) -> Tuple[int, str]:
    key, _ = item
    if key.startswith('plan_'):
        try:
            return int(key.split('_', 1)[1]), key
        except ValueError:
            pass
    return 10**9, key


def to_int(value: Any, default: int = 0) -> int:
    try:
        return int(value)
    except (TypeError, ValueError):
        return default


def to_float(value: Any, default: float = 0.0) -> float:
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def safe_float(value: Any) -> Optional[float]:
    try:
        number = float(value)
    except (TypeError, ValueError):
        return None
    return number if math.isfinite(number) else None


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
        'planning_request_id',
        'candidate_plan_id',
        'graph_hash',
        'selected',
        'feasible',
        'terminal_success_estimate',
        'final_rapfs',
        'terminal_outcome',
        'terminal_success',
        'runtime_seconds',
        'warning_count',
        'error_count',
        'human_judgement',
        'score_components',
        'calibration_version',
        'normalizer_version', 'estimator_version', 'weight_version', 'provenance',
    ]
    output: Dict[Tuple[str, str, str], Dict[str, Any]] = {}
    for row in rows:
        record = dict(zip(columns, row))
        key = (record['planning_request_id'], record['candidate_plan_id'], record['graph_hash'])
        output[key] = record
    connection.close()
    return output


def load_selected_plans(feedback_root: Path) -> Dict[str, Dict[str, Any]]:
    selected_by_request: Dict[str, Dict[str, Any]] = {}
    for execution_dir in iter_execution_dirs(feedback_root):
        generated = load_json(execution_dir / 'generated_plans.json')
        feedback = load_json(execution_dir / 'user_feedback.json')
        feedback_by_key: Dict[Tuple[str, str, str], Dict[str, Any]] = {}
        for stage_name, entry in sorted(feedback.items(), key=stage_key):
            if not isinstance(entry, dict):
                continue
            key = (
                str(entry.get('planning_request_id') or ''),
                str(entry.get('candidate_plan_id') or ''),
                str(entry.get('graph_hash') or ''),
            )
            feedback_by_key[key] = entry

        for iteration_key, iteration in sorted(generated.items(), key=iteration_sort_key):
            if not iteration_key.startswith('iteration_') or not isinstance(iteration, dict):
                continue
            selected_candidate_plan_id = str(iteration.get('selected_candidate_plan_id') or '')
            for candidate_key, plan in sorted(iteration.items(), key=plan_key):
                if not candidate_key.startswith('plan_') or not isinstance(plan, dict):
                    continue
                candidate_plan_id = str(plan.get('candidate_plan_id') or '')
                if candidate_plan_id != selected_candidate_plan_id and not bool(plan.get('selected')):
                    continue
                planning_request_id = str(plan.get('planning_request_id') or '')
                if not planning_request_id:
                    continue
                graph_hash = str(plan.get('graph_hash') or '')
                feedback_entry = feedback_by_key.get(
                    (planning_request_id, candidate_plan_id, graph_hash),
                    {},
                )
                predicted_success = (
                    plan.get('terminal_success')
                    if isinstance(plan.get('terminal_success'), bool)
                    else bool(plan.get('feasible'))
                )
                selected_by_request[planning_request_id] = {
                    'execution_key': execution_dir.name,
                    'iteration_key': iteration_key,
                    'plan_key': candidate_key,
                    'planning_request_id': planning_request_id,
                    'candidate_plan_id': candidate_plan_id,
                    'graph_hash': graph_hash,
                    'selected_mode_supported': True,
                    'feasible': bool(plan.get('feasible')),
                    'predicted_success': predicted_success,
                    'predicted_terminal_success_label': (
                        'success' if predicted_success else 'failure'
                    ),
                    'terminal_success_estimate': safe_float(plan.get('terminal_success_estimate')),
                    'predicted_success_probability': safe_float(plan.get('terminal_success_estimate')),
                    'final_rapfs': safe_float(plan.get('final_rapfs')),
                    'reasoning': str(plan.get('reasoning') or ''),
                    'candidate_skill_name_list': ','.join(plan.get('candidate_skill_name_list') or []),
                    'observed_success': feedback_success(feedback_entry),
                    'terminal_outcome': str(feedback_entry.get('outcome') or feedback_entry.get('terminal_outcome') or ''),
                    'human_judgement': str(feedback_entry.get('feedback') or ''),
                    'calibration_version': str(plan.get('calibration_version') or ''),
                    'normalizer_version': str(plan.get('normalizer_version') or ''),
                    'estimator_version': str(plan.get('estimator_version') or ''),
                    'weight_version': str(plan.get('weight_version') or ''),
                }
    return selected_by_request


def community_recommendation(
    community_signal_ratio: float,
    mean_reduction_ratio: float,
    final_selected_mode: str,
) -> str:
    if community_signal_ratio > 0.5 and mean_reduction_ratio >= 0.20:
        return 'favor_community'
    if community_signal_ratio < 0.25 or mean_reduction_ratio < 0.10:
        return 'favor_detailed'
    if final_selected_mode == 'community':
        return 'community_needs_review'
    return 'needs_review'


def build_report(audit_root: Path, feedback_root: Path, db_path: Optional[Path]) -> Dict[str, Any]:
    documents = [load_json(path) for path in iter_audit_files(audit_root)]
    selected_plans = load_selected_plans(feedback_root)
    db_feedback = load_db_feedback(db_path)

    request_rows: List[Dict[str, Any]] = []
    for document in documents:
        planning_request_id = str(document.get('planning_request_id') or '')
        request_uuid = str(document.get('request_uuid') or '')
        task_description = str(document.get('task_description') or '')
        iterations = document.get('iterations')
        if not planning_request_id or not isinstance(iterations, dict):
            continue

        rows: List[Dict[str, Any]] = []
        for iteration_key, entry in sorted(iterations.items(), key=iteration_sort_key):
            if not isinstance(entry, dict):
                continue
            prompt_comparison = (
                entry.get('prompt_comparison')
                if isinstance(entry.get('prompt_comparison'), dict)
                else {}
            )
            semantic_counts = (
                entry.get('semantic_counts')
                if isinstance(entry.get('semantic_counts'), dict)
                else {}
            )
            selected_mode = str(entry.get('selected_semantic_context_mode') or 'detailed')
            detailed_chars = to_int(prompt_comparison.get('detailed_chars'), 0)
            community_chars = to_int(prompt_comparison.get('community_chars'), 0)
            selected_prompt_chars = community_chars if selected_mode == 'community' else detailed_chars
            reduction_ratio = to_float(prompt_comparison.get('reduction_ratio'), 0.0)
            community_summary_count = to_int(semantic_counts.get('community_summaries'), 0)
            rows.append(
                {
                    'iteration_key': iteration_key,
                    'phase': str(entry.get('phase') or ''),
                    'selected_semantic_context_mode': selected_mode,
                    'selected_prompt_chars': selected_prompt_chars,
                    'prompt_reduction_ratio': reduction_ratio,
                    'community_summary_count': community_summary_count,
                    'community_has_signal': community_summary_count > 0,
                }
            )
        if not rows:
            continue

        selected_plan = selected_plans.get(planning_request_id, {})
        if selected_plan:
            db_key = (
                planning_request_id,
                str(selected_plan.get('candidate_plan_id') or ''),
                str(selected_plan.get('graph_hash') or ''),
            )
            db_record = db_feedback.get(db_key, {})
        else:
            db_record = {}

        final_row = rows[-1]
        community_signal_iteration_count = sum(
            1 for row in rows if row['community_has_signal']
        )
        community_selected_iteration_count = sum(
            1
            for row in rows
            if row['selected_semantic_context_mode'] == 'community'
        )
        mean_reduction_ratio = sum(float(row['prompt_reduction_ratio']) for row in rows) / len(rows)
        max_reduction_ratio = max(float(row['prompt_reduction_ratio']) for row in rows)
        community_signal_ratio = community_signal_iteration_count / max(1, len(rows))
        request_rows.append(
            {
                'planning_request_id': planning_request_id,
                'request_uuid': request_uuid,
                'task_description': task_description,
                'iteration_count': len(rows),
                'community_signal_iteration_count': community_signal_iteration_count,
                'community_selected_iteration_count': community_selected_iteration_count,
                'community_signal_ratio': community_signal_ratio,
                'mean_prompt_reduction_ratio': mean_reduction_ratio,
                'max_prompt_reduction_ratio': max_reduction_ratio,
                'final_selected_mode': final_row['selected_semantic_context_mode'],
                'final_selected_prompt_chars': final_row['selected_prompt_chars'],
                'recommendation': community_recommendation(
                    community_signal_ratio,
                    mean_reduction_ratio,
                    final_row['selected_semantic_context_mode'],
                ),
                'selected_candidate_plan_id': str(selected_plan.get('candidate_plan_id') or ''),
                'selected_graph_hash': str(selected_plan.get('graph_hash') or ''),
                'selected_final_rapfs': selected_plan.get('final_rapfs'),
                'selected_terminal_success_estimate': selected_plan.get('terminal_success_estimate'),
                'selected_predicted_success_probability': selected_plan.get('predicted_success_probability'),
                'selected_predicted_success': selected_plan.get('predicted_success'),
                'selected_predicted_terminal_success_label': selected_plan.get(
                    'predicted_terminal_success_label'
                ),
                'selected_observed_success': selected_plan.get('observed_success'),
                'selected_terminal_outcome': str(
                    selected_plan.get('terminal_outcome')
                    or db_record.get('terminal_outcome')
                    or ''
                ),
                'selected_human_judgement': str(
                    selected_plan.get('human_judgement')
                    or db_record.get('human_judgement')
                    or ''
                ),
                'selected_reasoning': str(selected_plan.get('reasoning') or ''),
                'selected_candidate_skill_name_list': str(
                    selected_plan.get('candidate_skill_name_list') or ''
                ),
                'selected_calibration_version': str(
                    selected_plan.get('calibration_version')
                    or db_record.get('calibration_version')
                    or ''
                ),
                'selected_normalizer_version': str(
                    selected_plan.get('normalizer_version')
                    or db_record.get('normalizer_version')
                    or ''
                ),
                'selected_estimator_version': str(
                    selected_plan.get('estimator_version')
                    or db_record.get('estimator_version')
                    or ''
                ),
                'selected_weight_version': str(
                    selected_plan.get('weight_version')
                    or db_record.get('weight_version')
                    or ''
                ),
            }
        )

    summary = {
        'request_count': len(request_rows),
        'favor_community_count': sum(1 for row in request_rows if row['recommendation'] == 'favor_community'),
        'favor_detailed_count': sum(1 for row in request_rows if row['recommendation'] == 'favor_detailed'),
        'needs_review_count': sum(
            1
            for row in request_rows
            if row['recommendation'] not in {'favor_community', 'favor_detailed'}
        ),
        'requests_with_selected_plan': sum(1 for row in request_rows if row['selected_candidate_plan_id']),
    }
    return {
        'summary': summary,
        'request_prompt_decision_support': request_rows,
    }


def build_prompt_decision_support_report(
    audit_root: Path,
    feedback_root: Path,
    db_path: Optional[Path],
) -> Dict[str, Any]:
    return build_report(audit_root, feedback_root, db_path)


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


def export_prompt_decision_support(
    audit_root: Path,
    feedback_root: Path,
    db_path: Optional[Path],
    out_dir: Path,
) -> Dict[str, Any]:
    report = build_report(audit_root, feedback_root, db_path)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / 'prompt_decision_support.json').write_text(
        json.dumps(report, indent=2, sort_keys=True) + '\n',
        encoding='utf-8',
    )
    write_csv(out_dir / 'request_prompt_decision_support.csv', report['request_prompt_decision_support'])
    return report


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            'Export request-level decision-support rows for detailed versus '
            'community semantic prompt selection.'
        )
    )
    parser.add_argument(
        'audit_root',
        type=Path,
        help='Directory containing planner prompt audit JSON files',
    )
    parser.add_argument(
        'feedback_root',
        type=Path,
        help=(
            'Root containing per-execution generated_plans.json and optional '
            'user_feedback.json'
        ),
    )
    parser.add_argument(
        '--db',
        type=Path,
        default=None,
        help='Optional core_graphrag.sqlite3 for persisted feedback rows',
    )
    parser.add_argument('--out-dir', type=Path, default=Path('prompt_decision_support'))
    args = parser.parse_args()

    report = export_prompt_decision_support(args.audit_root, args.feedback_root, args.db, args.out_dir)
    print(
        f"wrote {args.out_dir} requests={report['summary']['request_count']} "
        f"favor_community={report['summary']['favor_community_count']} "
        f"favor_detailed={report['summary']['favor_detailed_count']}"
    )
    return 0


__all__ = [
    'build_prompt_decision_support_report',
    'build_report',
    'community_recommendation',
    'export_prompt_decision_support',
    'feedback_success',
    'iter_audit_files',
    'iter_execution_dirs',
    'iteration_sort_key',
    'load_db_feedback',
    'load_json',
    'load_selected_plans',
    'main',
    'plan_key',
    'safe_float',
    'stage_key',
    'to_float',
    'to_int',
    'write_csv',
]
