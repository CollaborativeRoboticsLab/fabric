#!/usr/bin/env python3

import argparse
import json
import re
import sqlite3
from collections import defaultdict
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple


NODE_TYPE_LABELS = {
    0: "CAPABILITY",
    1: "TASK_ENTITY",
    2: "CONTEXT_ENTITY",
    3: "PLAN_PATTERN",
    4: "FAULT_CONCEPT",
}

NODE_TYPE_COLORS = {
    "CAPABILITY": "#2f80ed",
    "TASK_ENTITY": "#27ae60",
    "CONTEXT_ENTITY": "#7f8c8d",
    "PLAN_PATTERN": "#f2994a",
    "FAULT_CONCEPT": "#eb5757",
    "SYNTHESIZED_ENDPOINT": "#9b51e0",
}


def normalize_id_segment(value: str) -> str:
    normalized = []
    for char in value.lower():
        if char.isalnum() or char in {"-", "_", "."}:
            normalized.append(char)
        else:
            normalized.append("_")
    return "".join(normalized) or "unknown"


def split_list(value: Optional[str]) -> List[str]:
    if not value:
        return []
    return [item for item in value.split(",") if item]


def table_exists(connection: sqlite3.Connection, table_name: str) -> bool:
    row = connection.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?;",
        (table_name,),
    ).fetchone()
    return row is not None


def table_columns(connection: sqlite3.Connection, table_name: str) -> List[str]:
    if not table_exists(connection, table_name):
        return []
    return [row[1] for row in connection.execute(f"PRAGMA table_info({table_name});")]


def fetch_dicts(connection: sqlite3.Connection, query: str) -> List[Dict[str, Any]]:
    cursor = connection.execute(query)
    columns = [description[0] for description in cursor.description]
    return [dict(zip(columns, row)) for row in cursor.fetchall()]


def load_nodes(connection: sqlite3.Connection) -> Tuple[Dict[str, Dict[str, Any]], Dict[str, str]]:
    nodes: Dict[str, Dict[str, Any]] = {}
    id_by_name: Dict[str, str] = {}
    if not table_exists(connection, "node_information"):
        return nodes, id_by_name

    for row in fetch_dicts(connection, "SELECT id, name, provider, descriptions, type FROM node_information;"):
        node_type = NODE_TYPE_LABELS.get(row.get("type"), "CONTEXT_ENTITY")
        node = {
            "id": row.get("id") or "",
            "label": row.get("name") or row.get("id") or "",
            "name": row.get("name") or "",
            "provider": row.get("provider") or "",
            "description": row.get("descriptions") or "",
            "type": node_type,
            "color": NODE_TYPE_COLORS.get(node_type, NODE_TYPE_COLORS["CONTEXT_ENTITY"]),
            "graph_present": False,
            "synthesized": False,
            "runtime": {
                "runs_started": 0,
                "runs_completed": 0,
                "runs_succeeded": 0,
                "runs_failed": 0,
                "runs_aborted": 0,
                "runs_timed_out": 0,
                "runtime_sample_count": 0,
                "runtime_sum_seconds": 0.0,
                "warning_count": 0,
                "error_count": 0,
                "active_rows": 0,
                "evidence_rows": 0,
                "planning_request_ids": [],
                "candidate_plan_ids": [],
                "plan_ids": [],
                "instance_ids": [],
                "last_outcome_label": "",
            },
        }
        nodes[node["id"]] = node
        if node["name"]:
            id_by_name[node["name"]] = node["id"]
            id_by_name[normalize_id_segment(node["name"])] = node["id"]
        if node["provider"]:
            id_by_name[node["provider"]] = node["id"]
            id_by_name[normalize_id_segment(node["provider"])] = node["id"]
    return nodes, id_by_name


def apply_graph_node_overlay(connection: sqlite3.Connection, nodes: Dict[str, Dict[str, Any]]) -> None:
    if not table_exists(connection, "graph_node"):
        return
    for row in fetch_dicts(connection, "SELECT id, level, cluster_id, feature_vector FROM graph_node;"):
        node_id = row.get("id") or ""
        if node_id not in nodes:
            nodes[node_id] = {
                "id": node_id,
                "label": node_id,
                "name": "",
                "provider": "",
                "description": "",
                "type": "SYNTHESIZED_ENDPOINT",
                "color": NODE_TYPE_COLORS["SYNTHESIZED_ENDPOINT"],
                "graph_present": True,
                "synthesized": True,
                "runtime": {},
            }
        nodes[node_id]["graph_present"] = True
        nodes[node_id]["level"] = row.get("level")
        nodes[node_id]["cluster_id"] = row.get("cluster_id")
        nodes[node_id]["feature_vector"] = split_list(row.get("feature_vector"))


def resolve_endpoint(endpoint: str, nodes: Dict[str, Dict[str, Any]], id_by_name: Dict[str, str]) -> str:
    if endpoint in nodes:
        return endpoint
    return id_by_name.get(endpoint) or id_by_name.get(normalize_id_segment(endpoint)) or endpoint


def ensure_endpoint_node(endpoint: str, nodes: Dict[str, Dict[str, Any]]) -> None:
    if endpoint in nodes:
        return
    nodes[endpoint] = {
        "id": endpoint,
        "label": endpoint,
        "name": endpoint,
        "provider": "",
        "description": "missing edge endpoint synthesized by export",
        "type": "SYNTHESIZED_ENDPOINT",
        "color": NODE_TYPE_COLORS["SYNTHESIZED_ENDPOINT"],
        "graph_present": False,
        "synthesized": True,
        "runtime": {},
    }


def load_edges(connection: sqlite3.Connection, nodes: Dict[str, Dict[str, Any]], id_by_name: Dict[str, str]) -> List[Dict[str, Any]]:
    edges: List[Dict[str, Any]] = []
    graph_edges = set()
    if table_exists(connection, "graph_edge"):
        for row in fetch_dicts(connection, "SELECT id, source, target FROM graph_edge;"):
            source = row.get("source") or ""
            target = row.get("target") or ""
            graph_edges.add((row.get("id") or "", source, target))
            ensure_endpoint_node(source, nodes)
            ensure_endpoint_node(target, nodes)

    if table_exists(connection, "edge_information"):
        for row in fetch_dicts(
            connection,
            "SELECT id, source, target, relationship, edge_type, edge_strength_class, structural_score, "
            "matched_runtime_inputs, total_runtime_inputs, matched_parameter_keys FROM edge_information;",
        ):
            source = resolve_endpoint(row.get("source") or "", nodes, id_by_name)
            target = resolve_endpoint(row.get("target") or "", nodes, id_by_name)
            ensure_endpoint_node(source, nodes)
            ensure_endpoint_node(target, nodes)
            edge_id = row.get("id") or f"{source}->{target}"
            edges.append(
                {
                    "id": edge_id,
                    "source": source,
                    "target": target,
                    "relationship": row.get("relationship") or "",
                    "edge_type": row.get("edge_type") or "",
                    "edge_strength_class": row.get("edge_strength_class") or "",
                    "structural_score": row.get("structural_score") or 0.0,
                    "matched_runtime_inputs": row.get("matched_runtime_inputs") or 0,
                    "total_runtime_inputs": row.get("total_runtime_inputs") or 0,
                    "matched_parameter_keys": split_list(row.get("matched_parameter_keys")),
                    "graph_present": any(item[0] == edge_id or (item[1] == source and item[2] == target) for item in graph_edges),
                }
            )

    known_edge_ids = {edge["id"] for edge in edges}
    for edge_id, source, target in graph_edges:
        if edge_id in known_edge_ids:
            continue
        edges.append(
            {
                "id": edge_id,
                "source": source,
                "target": target,
                "relationship": "",
                "edge_type": "graph_edge_only",
                "edge_strength_class": "",
                "structural_score": 0.0,
                "matched_runtime_inputs": 0,
                "total_runtime_inputs": 0,
                "matched_parameter_keys": [],
                "graph_present": True,
            }
        )
    return edges


def runtime_columns(connection: sqlite3.Connection) -> List[str]:
    return table_columns(connection, "skill_run_statistics")


def apply_runtime_overlay(connection: sqlite3.Connection, nodes: Dict[str, Dict[str, Any]], id_by_name: Dict[str, str]) -> None:
    if not table_exists(connection, "skill_run_statistics"):
        return

    columns = runtime_columns(connection)
    planning_columns = [column for column in ("planning_request_id", "candidate_plan_id", "graph_hash") if column in columns]
    query = "SELECT * FROM skill_run_statistics;"
    for row in fetch_dicts(connection, query):
        skill_id = row.get("skill_id") or normalize_id_segment(row.get("skill_name") or "")
        node_id = id_by_name.get(skill_id) or id_by_name.get(row.get("skill_name") or "")
        if not node_id or node_id not in nodes:
            continue
        runtime = nodes[node_id].setdefault("runtime", {})
        runtime["evidence_rows"] = runtime.get("evidence_rows", 0) + 1
        runtime["runs_started"] = runtime.get("runs_started", 0) + int(row.get("runs_started") or 0)
        runtime["runs_completed"] = runtime.get("runs_completed", 0) + int(row.get("runs_completed") or 0)
        runtime["runs_succeeded"] = runtime.get("runs_succeeded", 0) + int(row.get("runs_succeeded") or 0)
        runtime["runs_failed"] = runtime.get("runs_failed", 0) + int(row.get("runs_failed") or 0)
        runtime["runs_aborted"] = runtime.get("runs_aborted", 0) + int(row.get("runs_aborted") or 0)
        runtime["runs_timed_out"] = runtime.get("runs_timed_out", 0) + int(row.get("runs_timed_out") or 0)
        runtime["runtime_sample_count"] = runtime.get("runtime_sample_count", 0) + int(row.get("runtime_sample_count") or 0)
        runtime["runtime_sum_seconds"] = runtime.get("runtime_sum_seconds", 0.0) + float(row.get("runtime_sum_seconds") or 0.0)
        runtime["warning_count"] = runtime.get("warning_count", 0) + int(row.get("warning_count") or 0)
        runtime["error_count"] = runtime.get("error_count", 0) + int(row.get("error_count") or 0)
        runtime["active_rows"] = runtime.get("active_rows", 0) + (1 if int(row.get("is_active") or 0) else 0)
        runtime["last_outcome_label"] = row.get("last_outcome_label") or runtime.get("last_outcome_label", "")
        for list_key, column in (
            ("planning_request_ids", "planning_request_id"),
            ("candidate_plan_ids", "candidate_plan_id"),
            ("plan_ids", "plan_id"),
            ("instance_ids", "instance_id"),
        ):
            value = row.get(column) if column in columns else ""
            if value and value not in runtime.setdefault(list_key, []):
                runtime[list_key].append(value)
        for column in planning_columns:
            value = row.get(column)
            if value and value not in runtime.setdefault(column + "s", []):
                runtime[column + "s"].append(value)


def load_communities(connection: sqlite3.Connection) -> List[Dict[str, Any]]:
    communities = []
    if not table_exists(connection, "graph_community"):
        return communities
    for row in fetch_dicts(connection, "SELECT id, level, node_ids, edge_ids, occurrence FROM graph_community;"):
        communities.append(
            {
                "id": row.get("id") or "",
                "level": row.get("level"),
                "node_ids": split_list(row.get("node_ids")),
                "edge_ids": split_list(row.get("edge_ids")),
                "occurrence": row.get("occurrence") or 0.0,
            }
        )
    return communities


def build_export(db_path: Path) -> Dict[str, Any]:
    connection = sqlite3.connect(db_path)
    try:
        nodes, id_by_name = load_nodes(connection)
        apply_graph_node_overlay(connection, nodes)
        edges = load_edges(connection, nodes, id_by_name)
        apply_runtime_overlay(connection, nodes, id_by_name)
        communities = load_communities(connection)
        missing_edge_endpoints = sorted(node_id for node_id, node in nodes.items() if node.get("synthesized"))
        return {
            "source_db": str(db_path),
            "summary": {
                "node_count": len(nodes),
                "edge_count": len(edges),
                "community_count": len(communities),
                "graph_node_count": sum(1 for node in nodes.values() if node.get("graph_present")),
                "graph_edge_count": sum(1 for edge in edges if edge.get("graph_present")),
                "synthesized_endpoint_count": len(missing_edge_endpoints),
            },
            "legend": NODE_TYPE_COLORS,
            "nodes": list(nodes.values()),
            "edges": edges,
            "communities": communities,
            "missing_edge_endpoints": missing_edge_endpoints,
        }
    finally:
        connection.close()


def main() -> int:
    parser = argparse.ArgumentParser(description="Export CoreGraphRAG graph and runtime overlays as visualization-ready JSON.")
    parser.add_argument("db", type=Path, help="Path to core_graphrag.sqlite3")
    parser.add_argument("--out", type=Path, default=Path("core_graphrag_debug.json"))
    args = parser.parse_args()

    if not args.db.is_file():
        raise SystemExit(f"database not found: {args.db}")

    export = build_export(args.db)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(export, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"wrote {args.out} nodes={export['summary']['node_count']} edges={export['summary']['edge_count']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
