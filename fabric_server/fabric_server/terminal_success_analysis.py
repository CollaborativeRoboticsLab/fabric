from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
from typing import Any, Dict, List, Optional

from fabric_server.reasoning_outcomes import build_reasoning_outcome_rows


SUCCESS_LABEL = 'success'
MARGINAL_LABEL = 'marginal'
FAILURE_LABEL = 'failure'


def safe_bool(value: Any) -> Optional[bool]:
    if isinstance(value, bool):
        return value
    return None


def safe_float(value: Any) -> Optional[float]:
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def collect_analysis_rows(feedback_root: Path, db_path: Optional[Path]) -> List[Dict[str, Any]]:
    report = build_reasoning_outcome_rows(feedback_root, db_path)
    rows: List[Dict[str, Any]] = []
    for entry in report.get('outcome_vs_prediction', []):
        final_rapfs = safe_float(entry.get('final_rapfs'))
        observed_success = safe_bool(entry.get('observed_success'))
        if final_rapfs is None or observed_success is None:
            continue
        rows.append(
            {
                'execution_key': str(entry.get('execution_key') or ''),
                'planning_request_id': str(entry.get('planning_request_id') or ''),
                'candidate_plan_id': str(entry.get('candidate_plan_id') or ''),
                'graph_hash': str(entry.get('graph_hash') or ''),
                'final_rapfs': final_rapfs,
                'terminal_success_estimate': safe_float(entry.get('terminal_success_estimate')),
                'predicted_success_probability': safe_float(entry.get('predicted_success_probability')),
                'predicted_terminal_success_label': str(entry.get('predicted_terminal_success_label') or ''),
                'observed_success': observed_success,
                'terminal_outcome': str(entry.get('terminal_outcome') or ''),
                'human_judgement': str(entry.get('human_judgement') or ''),
                'calibration_version': str(entry.get('calibration_version') or ''),
                'normalizer_version': str(entry.get('normalizer_version') or ''),
                'estimator_version': str(entry.get('estimator_version') or ''),
                'weight_version': str(entry.get('weight_version') or ''),
            }
        )
    rows.sort(key=lambda row: (row['final_rapfs'], row['planning_request_id'], row['candidate_plan_id']))
    return rows


def candidate_thresholds(rows: List[Dict[str, Any]]) -> List[float]:
    scores = sorted({float(row['final_rapfs']) for row in rows})
    if not scores:
        return []
    candidates = {scores[0], scores[-1]}
    for left, right in zip(scores, scores[1:]):
        candidates.add((left + right) / 2.0)
    return sorted(candidates)


def evaluate_threshold(rows: List[Dict[str, Any]], threshold: float) -> Dict[str, Any]:
    true_positive = true_negative = false_positive = false_negative = 0
    for row in rows:
        predicted_success = float(row['final_rapfs']) >= threshold
        observed_success = bool(row['observed_success'])
        if predicted_success and observed_success:
            true_positive += 1
        elif predicted_success and not observed_success:
            false_positive += 1
        elif not predicted_success and observed_success:
            false_negative += 1
        else:
            true_negative += 1

    total = max(1, len(rows))
    accuracy = (true_positive + true_negative) / total
    precision = true_positive / max(1, true_positive + false_positive)
    recall = true_positive / max(1, true_positive + false_negative)
    specificity = true_negative / max(1, true_negative + false_positive)
    balanced_accuracy = (recall + specificity) / 2.0
    f1 = 0.0 if precision + recall == 0.0 else 2.0 * precision * recall / (precision + recall)
    return {
        'threshold': threshold,
        'predicted_success_count': true_positive + false_positive,
        'predicted_failure_count': true_negative + false_negative,
        'true_positive': true_positive,
        'true_negative': true_negative,
        'false_positive': false_positive,
        'false_negative': false_negative,
        'accuracy': accuracy,
        'precision': precision,
        'recall': recall,
        'specificity': specificity,
        'balanced_accuracy': balanced_accuracy,
        'f1': f1,
    }


def build_band_proposal(rows: List[Dict[str, Any]], best_threshold: float) -> Dict[str, Any]:
    success_scores = sorted(float(row['final_rapfs']) for row in rows if bool(row['observed_success']))
    failure_scores = sorted(float(row['final_rapfs']) for row in rows if not bool(row['observed_success']))
    success_floor = success_scores[0] if success_scores else None
    failure_ceiling = failure_scores[-1] if failure_scores else None
    overlap = False
    if success_floor is not None and failure_ceiling is not None:
        overlap = failure_ceiling >= success_floor
    return {
        'recommended_success_threshold': success_floor if success_floor is not None else best_threshold,
        'recommended_failure_threshold': failure_ceiling if failure_ceiling is not None else best_threshold,
        'best_single_threshold': best_threshold,
        'supports_marginal_band': not overlap and success_floor is not None and failure_ceiling is not None,
        'score_overlap_detected': overlap,
    }


def build_calibration_metadata(rows: List[Dict[str, Any]], proposal: Dict[str, Any]) -> Dict[str, Any]:
    version_columns = [
        'calibration_version',
        'normalizer_version',
        'estimator_version',
        'weight_version',
    ]
    distinct_versions = {
        column: sorted({str(row.get(column) or '') for row in rows if str(row.get(column) or '')})
        for column in version_columns
    }
    resolved_versions = {
        column: values[0] if len(values) == 1 else ''
        for column, values in distinct_versions.items()
    }
    mixed_scope = any(len(values) > 1 for values in distinct_versions.values())
    band_mode = 'banded' if proposal.get('supports_marginal_band') else 'single_threshold'
    return {
        'label_version': 'terminal_success_calibration_v1',
        'label_strategy': 'rapfs_threshold_analysis',
        'band_mode': band_mode,
        'applied_to_runtime_scoring': False,
        'requires_explicit_config_update': True,
        'resolved_versions': resolved_versions,
        'distinct_versions': distinct_versions,
        'mixed_version_scope': mixed_scope,
    }


def classify_terminal_success_label(score: Optional[float], proposal: Dict[str, Any]) -> str:
    if score is None:
        return ''
    success_threshold = safe_float(proposal.get('recommended_success_threshold'))
    failure_threshold = safe_float(proposal.get('recommended_failure_threshold'))
    best_threshold = safe_float(proposal.get('best_single_threshold'))
    supports_marginal_band = bool(proposal.get('supports_marginal_band'))

    if supports_marginal_band and success_threshold is not None and failure_threshold is not None:
        if score >= success_threshold:
            return SUCCESS_LABEL
        if score <= failure_threshold:
            return FAILURE_LABEL
        return MARGINAL_LABEL

    if best_threshold is None:
        return ''
    return SUCCESS_LABEL if score >= best_threshold else FAILURE_LABEL


def annotate_samples(rows: List[Dict[str, Any]], proposal: Dict[str, Any], metadata: Dict[str, Any]) -> List[Dict[str, Any]]:
    annotated_rows: List[Dict[str, Any]] = []
    for row in rows:
        annotated = dict(row)
        annotated['calibrated_terminal_success_label'] = classify_terminal_success_label(
            safe_float(row.get('final_rapfs')),
            proposal,
        )
        annotated['calibrated_success_threshold'] = safe_float(proposal.get('recommended_success_threshold'))
        annotated['calibrated_failure_threshold'] = safe_float(proposal.get('recommended_failure_threshold'))
        annotated['calibrated_best_single_threshold'] = safe_float(proposal.get('best_single_threshold'))
        annotated['calibrated_band_mode'] = str(metadata.get('band_mode') or '')
        annotated['calibration_label_version'] = str(metadata.get('label_version') or '')
        annotated_rows.append(annotated)
    return annotated_rows


def build_runtime_policy_update(metadata: Dict[str, Any], proposal: Dict[str, Any]) -> Dict[str, Any]:
    return {
        'applied': False,
        'requires_manual_review': True,
        'target_parameter': 'RapfsScorerPlugin.calibration_version',
        'label_version': metadata.get('label_version'),
        'band_mode': metadata.get('band_mode'),
        'recommended_success_threshold': proposal.get('recommended_success_threshold'),
        'recommended_failure_threshold': proposal.get('recommended_failure_threshold'),
        'best_single_threshold': proposal.get('best_single_threshold'),
    }


def build_report(feedback_root: Path, db_path: Optional[Path]) -> Dict[str, Any]:
    rows = collect_analysis_rows(feedback_root, db_path)
    thresholds = [evaluate_threshold(rows, threshold) for threshold in candidate_thresholds(rows)]
    thresholds.sort(
        key=lambda row: (
            -float(row['balanced_accuracy']),
            -float(row['accuracy']),
            -float(row['f1']),
            abs(float(row['threshold'])),
        )
    )
    best = thresholds[0] if thresholds else None
    proposal = build_band_proposal(rows, float(best['threshold'])) if best is not None else {
        'recommended_success_threshold': None,
        'recommended_failure_threshold': None,
        'best_single_threshold': None,
        'supports_marginal_band': False,
        'score_overlap_detected': False,
    }
    metadata = build_calibration_metadata(rows, proposal)
    annotated_rows = annotate_samples(rows, proposal, metadata)
    runtime_policy_update = build_runtime_policy_update(metadata, proposal)
    return {
        'summary': {
            'sample_count': len(annotated_rows),
            'observed_success_count': sum(1 for row in annotated_rows if bool(row['observed_success'])),
            'observed_failure_count': sum(1 for row in annotated_rows if not bool(row['observed_success'])),
            'candidate_threshold_count': len(thresholds),
            'recommended_success_threshold': proposal['recommended_success_threshold'],
            'recommended_failure_threshold': proposal['recommended_failure_threshold'],
            'best_single_threshold': proposal['best_single_threshold'],
            'calibration_label_version': metadata['label_version'],
            'calibration_band_mode': metadata['band_mode'],
        },
        'calibration_metadata': metadata,
        'proposal': proposal,
        'runtime_policy_update': runtime_policy_update,
        'samples': annotated_rows,
        'threshold_candidates': thresholds,
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


def export_terminal_success_analysis(feedback_root: Path, db_path: Optional[Path], out_dir: Path) -> Dict[str, Any]:
    report = build_report(feedback_root, db_path)
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / 'terminal_success_analysis.json').write_text(
        json.dumps(report, indent=2, sort_keys=True) + '\n',
        encoding='utf-8',
    )
    (out_dir / 'terminal_success_calibration_proposal.json').write_text(
        json.dumps(
            {
                'calibration_metadata': report['calibration_metadata'],
                'proposal': report['proposal'],
                'runtime_policy_update': report['runtime_policy_update'],
            },
            indent=2,
            sort_keys=True,
        ) + '\n',
        encoding='utf-8',
    )
    write_csv(out_dir / 'terminal_success_samples.csv', report['samples'])
    write_csv(out_dir / 'terminal_success_threshold_candidates.csv', report['threshold_candidates'])
    return report


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Export offline terminal-success threshold analysis from persisted feedback and generated plan artifacts.'
    )
    parser.add_argument('feedback_root', type=Path)
    parser.add_argument('--db', type=Path, default=None)
    parser.add_argument('--out-dir', type=Path, default=Path('terminal_success_analysis'))
    args = parser.parse_args()

    report = export_terminal_success_analysis(args.feedback_root, args.db, args.out_dir)
    print(
        f"wrote {args.out_dir} samples={report['summary']['sample_count']} "
        f"best_threshold={report['summary']['best_single_threshold']}"
    )
    return 0


__all__ = [
    'FAILURE_LABEL',
    'MARGINAL_LABEL',
    'SUCCESS_LABEL',
    'annotate_samples',
    'build_calibration_metadata',
    'build_band_proposal',
    'build_report',
    'build_runtime_policy_update',
    'candidate_thresholds',
    'classify_terminal_success_label',
    'collect_analysis_rows',
    'evaluate_threshold',
    'export_terminal_success_analysis',
    'main',
    'safe_bool',
    'safe_float',
    'write_csv',
]
