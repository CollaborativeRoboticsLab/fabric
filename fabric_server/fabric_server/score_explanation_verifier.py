from __future__ import annotations

import argparse
import json
import sqlite3
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional


SEMANTIC_FIELDS = [
    'semantic_familiarity',
    'semantic_relevant_capabilities',
    'semantic_related_entities',
    'semantic_dependencies',
    'semantic_plan_patterns',
    'semantic_fault_concepts',
    'semantic_community_summaries',
]


def split_field(value: Optional[str]) -> List[str]:
    if not value:
        return []
    return [item for item in str(value).split(',') if item]


def load_explanation_row(
    db_path: Path,
    planning_request_id: str,
    candidate_plan_id: str,
    graph_hash: str,
    candidate_node_id: str = '',
) -> Dict[str, Any]:
    connection = sqlite3.connect(db_path)
    try:
        sql = (
            'SELECT planning_request_id, candidate_plan_id, graph_hash, candidate_node_id, skill_name, skill_id, '
            'requested_context_key, requested_context_class, selected_skill_id, selected_key, estimator_window, '
            'fallback_level, proxy_source, reason, semantic_familiarity, semantic_relevant_capabilities, '
            'semantic_related_entities, semantic_dependencies, semantic_plan_patterns, semantic_fault_concepts, '
            'semantic_community_summaries, estimator_names, estimator_values, estimator_available, estimator_reasons, '
            'recorded_at_sec, recorded_at_nanosec, provenance '
            'FROM plan_node_score_explanation '
            'WHERE planning_request_id=? AND candidate_plan_id=? AND graph_hash=? '
        )
        params: List[Any] = [planning_request_id, candidate_plan_id, graph_hash]
        if candidate_node_id:
            sql += 'AND candidate_node_id=? '
            params.append(candidate_node_id)
        sql += 'ORDER BY recorded_at_sec DESC, recorded_at_nanosec DESC, candidate_node_id ASC LIMIT 1;'
        cursor = connection.execute(sql, params)
        columns = [description[0] for description in cursor.description]
        row = cursor.fetchone()
        if row is None:
            return {}
        return dict(zip(columns, row))
    finally:
        connection.close()


def build_report(row: Dict[str, Any]) -> Dict[str, Any]:
    if not row:
        return {
            'found': False,
            'success': False,
            'message': 'no explanation row matched the requested plan or candidate node',
            'semantic_checks': {},
            'estimator_checks': {},
        }

    semantic_checks = {
        'semantic_familiarity': float(row.get('semantic_familiarity') or 0.0) > 0.0,
        'semantic_relevant_capabilities': len(split_field(row.get('semantic_relevant_capabilities'))) > 0,
        'semantic_related_entities': len(split_field(row.get('semantic_related_entities'))) > 0,
        'semantic_dependencies': len(split_field(row.get('semantic_dependencies'))) > 0,
        'semantic_plan_patterns': len(split_field(row.get('semantic_plan_patterns'))) > 0,
        'semantic_fault_concepts': len(split_field(row.get('semantic_fault_concepts'))) > 0,
        'semantic_community_summaries': len(split_field(row.get('semantic_community_summaries'))) > 0,
    }

    estimator_names = split_field(row.get('estimator_names'))
    estimator_values = split_field(row.get('estimator_values'))
    estimator_available = split_field(row.get('estimator_available'))
    estimator_checks = {
        'estimator_outputs_present': len(estimator_names) > 0,
        'estimator_values_present': len(estimator_values) > 0,
        'estimator_availability_present': len(estimator_available) > 0,
    }

    populated_semantic_fields = [field for field, ok in semantic_checks.items() if ok]
    populated_estimator_fields = [field for field, ok in estimator_checks.items() if ok]
    return {
        'found': True,
        'message': 'explanation row loaded',
        'planning_request_id': row.get('planning_request_id') or '',
        'candidate_plan_id': row.get('candidate_plan_id') or '',
        'graph_hash': row.get('graph_hash') or '',
        'candidate_node_id': row.get('candidate_node_id') or '',
        'skill_name': row.get('skill_name') or '',
        'fallback_level': row.get('fallback_level') or '',
        'reason': row.get('reason') or '',
        'semantic_checks': semantic_checks,
        'estimator_checks': estimator_checks,
        'populated_semantic_field_count': len(populated_semantic_fields),
        'populated_semantic_fields': populated_semantic_fields,
        'populated_estimator_field_count': len(populated_estimator_fields),
        'populated_estimator_fields': populated_estimator_fields,
        'success': len(populated_semantic_fields) > 0 and estimator_checks['estimator_outputs_present'],
    }


def verify_plan_score_explanation(
    db_path: Path,
    planning_request_id: str,
    candidate_plan_id: str,
    graph_hash: str,
    candidate_node_id: str = '',
) -> Dict[str, Any]:
    row = load_explanation_row(
        db_path,
        planning_request_id,
        candidate_plan_id,
        graph_hash,
        candidate_node_id=candidate_node_id,
    )
    return build_report(row)


def print_report(report: Dict[str, Any]) -> None:
    print(f"found: {report.get('found', False)}")
    print(f"message: {report.get('message', '')}")
    if not report.get('found'):
        return
    print(f"planning_request_id: {report.get('planning_request_id', '')}")
    print(f"candidate_plan_id: {report.get('candidate_plan_id', '')}")
    print(f"graph_hash: {report.get('graph_hash', '')}")
    print(f"candidate_node_id: {report.get('candidate_node_id', '')}")
    print(f"skill_name: {report.get('skill_name', '')}")
    print(f"fallback_level: {report.get('fallback_level', '')}")
    print(f"reason: {report.get('reason', '')}")
    print(f"success: {report.get('success', False)}")
    print(f"populated_semantic_fields: {report.get('populated_semantic_fields', [])}")
    print(f"populated_estimator_fields: {report.get('populated_estimator_fields', [])}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description='Verify that one persisted plan-node explanation carries populated semantic and estimator fields.'
    )
    parser.add_argument('db', type=Path)
    parser.add_argument('planning_request_id')
    parser.add_argument('candidate_plan_id')
    parser.add_argument('graph_hash')
    parser.add_argument('--candidate-node-id', default='')
    parser.add_argument('--json', action='store_true', help='Print the full verification report as JSON.')
    args = parser.parse_args()

    if not args.db.is_file():
        print(f'database not found: {args.db}', file=sys.stderr)
        return 1

    report = verify_plan_score_explanation(
        args.db,
        args.planning_request_id,
        args.candidate_plan_id,
        args.graph_hash,
        candidate_node_id=args.candidate_node_id,
    )
    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print_report(report)
    if not report.get('found'):
        return 2
    return 0 if report.get('success') else 3


__all__ = [
    'SEMANTIC_FIELDS',
    'build_report',
    'load_explanation_row',
    'main',
    'print_report',
    'split_field',
    'verify_plan_score_explanation',
]