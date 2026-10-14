import os
import sqlite3
import sys
import json
from pathlib import Path

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')

PACKAGE_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PACKAGE_ROOT))

from python_qt_binding import QtWidgets

from fabric_experience_inspector.app import InspectorMainWindow, InspectorWidget, ensure_application
from fabric_experience_inspector.audits.planner_prompt_audit_summary import export_planner_prompt_audit_summary
from fabric_experience_inspector.audits.retrieval_prompt_audit import export_retrieval_prompt_audit
from test_sqlite_reader import create_minimal_db


def test_qt_widget_smoke(tmp_path):
    app = ensure_application([])
    widget = InspectorWidget()

    db_path = tmp_path / 'core_graphrag.sqlite3'
    audit_root = tmp_path / 'planner_prompt_audit'
    audit_root.mkdir()

    widget.set_db_path(str(db_path))
    widget.set_audit_root(str(audit_root))

    assert widget.current_db_path() == str(db_path)
    assert widget.current_audit_root() == str(audit_root)

    widget.close()
    app.processEvents()


def test_qt_widget_filters_and_graph_view(tmp_path):
    app = ensure_application([])
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_minimal_db(db_path)

    widget = InspectorWidget(initial_db_path=str(db_path))

    assert 'nodes: 2' in widget.graph_summary_box.toPlainText().lower()
    assert 'edges: 1' in widget.graph_summary_box.toPlainText().lower()

    widget.filter_edits['Nodes'].setText('DetectObject')
    widget.apply_filter('Nodes')
    nodes_table = widget.tables['Nodes']
    assert nodes_table.isRowHidden(0) is False

    widget.filter_columns['Nodes'].setCurrentText('provider')
    widget.filter_edits['Nodes'].setText('demo')
    widget.apply_filter('Nodes')
    assert nodes_table.isRowHidden(0) is False

    widget.secondary_filter_columns['Nodes'].setCurrentText('name')
    widget.secondary_filter_edits['Nodes'].setText('DetectObject')
    widget.apply_filter('Nodes')
    assert widget.proxies['Nodes'].rowCount() == 1

    widget.graph_node_filter_edit.setText('DetectObject')
    widget.apply_graph_filter()
    assert widget.graph_node_list.count() >= 1
    assert len(widget.graph_node_items) >= 2

    first_visible = widget._first_visible_graph_item()
    assert first_visible is not None
    widget.graph_node_list.setCurrentItem(first_visible)
    app.processEvents()

    assert 'incident_edges: 1' in widget.graph_node_preview.toPlainText()
    assert widget.graph_neighbor_table.rowCount() == 1
    selected_node_id = first_visible.data(0x0100)
    widget.select_graph_node_by_id(selected_node_id)
    app.processEvents()
    assert widget.current_graph_node_id == selected_node_id
    assert widget.graph_node_items[selected_node_id].pen().widthF() >= 3.0

    widget.close()
    app.processEvents()


def test_qt_widget_export_helpers(tmp_path):
    app = ensure_application([])
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_minimal_db(db_path)

    audit_root = tmp_path / 'planner_prompt_audit'
    audit_root.mkdir()
    (audit_root / 'req_1_10_20.json').write_text(
        '{\n'
        '  "planning_request_id": "req_1",\n'
        '  "request_uuid": "uuid_1",\n'
        '  "iterations": {\n'
        '    "iteration_1": {\n'
        '      "phase": "generation",\n'
        '      "selected_semantic_context_mode": "detailed",\n'
        '      "prompt_comparison": {\n'
        '        "detailed_chars": 1500,\n'
        '        "community_chars": 900,\n'
        '        "reduction_chars": 600,\n'
        '        "reduction_ratio": 0.4\n'
        '      },\n'
        '      "semantic_counts": {\n'
        '        "community_summaries": 1,\n'
        '        "relevant_capabilities": 2\n'
        '      }\n'
        '    }\n'
        '  }\n'
        '}\n',
        encoding='utf-8',
    )

    widget = InspectorWidget(initial_db_path=str(db_path), initial_audit_root=str(audit_root))

    retrieval_out = tmp_path / 'retrieval_prompt_audit'
    summary_out = tmp_path / 'planner_prompt_audit_exports'

    retrieval_report = export_retrieval_prompt_audit(db_path, retrieval_out)
    summary_report = export_planner_prompt_audit_summary(audit_root, summary_out)

    assert retrieval_report['summary']['plan_count'] == 1
    assert summary_report['summary']['request_count'] == 1
    assert (retrieval_out / 'retrieval_prompt_audit.json').exists()
    assert (summary_out / 'planner_prompt_audit_summary.json').exists()

    widget.close()
    app.processEvents()


def test_analysis_jobs_panel_defaults(tmp_path):
    app = ensure_application([])
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_minimal_db(db_path)
    audit_root = tmp_path / 'planner_prompt_audit'
    audit_root.mkdir()

    window = InspectorMainWindow(initial_db_path=str(db_path), initial_audit_root=str(audit_root))

    assert window.jobs_panel.retrieval_out_edit.text().endswith('retrieval_prompt_audit')
    assert window.jobs_panel.summary_out_edit.text().endswith('planner_prompt_audit_exports')
    assert window.jobs_panel.calibration_out_edit.text().endswith('terminal_success_analysis')
    assert window.jobs_panel.plot_path_edit.text().endswith('_3d_graph.png')
    assert '_proposal_' in window.jobs_panel.proposal_path_edit.text()

    window.close()
    app.processEvents()


def test_plan_sandbox_parse_and_overlay(tmp_path):
    app = ensure_application([])
    widget = InspectorWidget()

    widget.plan_xml_edit.setPlainText(
        '<Plan>'
        '<Control type="sequential">'
        '<Runner name="detect" />'
        '<Runner name="pick" />'
        '</Control>'
        '</Plan>'
    )
    widget.plan_score_edit.setPlainText(
        '{'
        '"final_rapfs": 0.72,'
        '"expected_runtime_seconds": 5.5,'
        '"score_components": "runtime=5.5;success=0.8",'
        '"node_explanations": ['
        '  {'
        '    "candidate_node_id": "runner_1",'
        '    "skill_name": "detect",'
        '    "reason": "matched evidence"'
        '  }'
        ']'
        '}'
    )

    widget.parse_plan_sandbox()
    app.processEvents()

    assert len(widget.plan_nodes) >= 4
    assert len(widget.plan_edges) >= 3
    assert 'expected_runtime_seconds: 5.5' in widget.plan_summary_box.toPlainText()
    assert len(widget.plan_node_items) >= 4

    widget.select_plan_node_by_id('runner_1')
    app.processEvents()

    assert widget.current_plan_node_id == 'runner_1'
    assert 'matched evidence' in widget.plan_preview_box.toPlainText()
    assert widget.plan_node_items['runner_1'].pen().widthF() >= 3.0

    widget.close()
    app.processEvents()


def test_tuning_proposal_export(tmp_path):
    app = ensure_application([])
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_minimal_db(db_path)

    widget = InspectorWidget(initial_db_path=str(db_path))
    proposal_path = tmp_path / 'exports' / 'review_proposal.json'

    written_path = widget.run_tuning_proposal_export(
        proposal_path,
        proposal_kind='threshold_recalibration',
        summary='Review scoring threshold drift',
        notes='Observed mismatch between offline review and runtime decisions.',
    )

    assert written_path == proposal_path
    proposal = json.loads(proposal_path.read_text(encoding='utf-8'))
    assert proposal['proposal_kind'] == 'threshold_recalibration'
    assert proposal['read_only_policy']['mutates_database'] is False
    assert proposal['graph_snapshot']['node_count'] == 2

    widget.close()
    app.processEvents()


def test_request_review_joins_prompt_audit_explanations_and_generated_plan(tmp_path):
    app = ensure_application([])
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_minimal_db(db_path)

    audit_root = tmp_path / 'planner_prompt_audit'
    audit_root.mkdir()
    (audit_root / 'req_1_10_20.json').write_text(
        '{\n'
        '  "planning_request_id": "req_1",\n'
        '  "request_uuid": "uuid_1",\n'
        '  "iterations": {\n'
        '    "iteration_1": {\n'
        '      "phase": "generation",\n'
        '      "selected_semantic_context_mode": "detailed",\n'
        '      "prompt_comparison": {\n'
        '        "detailed_chars": 1500,\n'
        '        "community_chars": 900,\n'
        '        "reduction_chars": 600,\n'
        '        "reduction_ratio": 0.4\n'
        '      },\n'
        '      "semantic_counts": {\n'
        '        "community_summaries": 1,\n'
        '        "relevant_capabilities": 2\n'
        '      }\n'
        '    }\n'
        '  }\n'
        '}\n',
        encoding='utf-8',
    )

    generated_root = tmp_path / 'generated'
    execution_root = generated_root / 'execution_1'
    execution_root.mkdir(parents=True)
    (execution_root / 'generated_plans.json').write_text(
        '{\n'
        '  "iteration_1": {\n'
        '    "selected_candidate_plan_id": "plan_1",\n'
        '    "plan_1": {\n'
        '      "planning_request_id": "req_1",\n'
        '      "candidate_plan_id": "plan_1",\n'
        '      "graph_hash": "graph_1",\n'
        '      "selected": true,\n'
        '      "feasible": true,\n'
        '      "terminal_success_estimate": 0.9,\n'
        '      "final_rapfs": 0.72,\n'
        '      "reasoning": "selected because evidence matched",\n'
        '      "plan": "<Plan><Runner name=\'detect\' /></Plan>"\n'
        '    }\n'
        '  }\n'
        '}\n',
        encoding='utf-8',
    )

    widget = InspectorWidget(initial_db_path=str(db_path), initial_audit_root=str(audit_root))
    widget.set_generated_root(str(generated_root))
    widget.load_generated_plan_artifacts()
    app.processEvents()

    review = widget.request_review_widget
    assert review is not None
    assert review.request_list.count() == 1
    assert 'prompt_iterations: 1' in review.summary_box.toPlainText()
    assert 'iteration_1' in review.audit_box.toPlainText()
    assert 'selected because evidence matched' in review.generated_plan_box.toPlainText()
    assert review.explanation_table.rowCount() == 1
    assert 'semantic_community_summaries' in review.explanation_preview.toPlainText()

    widget.close()
    app.processEvents()


def test_decision_support_tab_summarizes_request_level_recommendations(tmp_path):
    app = ensure_application([])
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_minimal_db(db_path)

    audit_root = tmp_path / 'planner_prompt_audit'
    audit_root.mkdir()
    (audit_root / 'req_1_10_20.json').write_text(
        '{\n'
        '  "planning_request_id": "req_1",\n'
        '  "request_uuid": "uuid_1",\n'
        '  "task_description": "pick the apple",\n'
        '  "iterations": {\n'
        '    "iteration_1": {\n'
        '      "phase": "generation",\n'
        '      "selected_semantic_context_mode": "community",\n'
        '      "prompt_comparison": {\n'
        '        "detailed_chars": 1500,\n'
        '        "community_chars": 900,\n'
        '        "reduction_ratio": 0.4\n'
        '      },\n'
        '      "semantic_counts": {\n'
        '        "community_summaries": 1,\n'
        '        "relevant_capabilities": 2\n'
        '      }\n'
        '    },\n'
        '    "iteration_2": {\n'
        '      "phase": "refinement",\n'
        '      "selected_semantic_context_mode": "community",\n'
        '      "prompt_comparison": {\n'
        '        "detailed_chars": 1600,\n'
        '        "community_chars": 950,\n'
        '        "reduction_ratio": 0.40625\n'
        '      },\n'
        '      "semantic_counts": {\n'
        '        "community_summaries": 2,\n'
        '        "relevant_capabilities": 2\n'
        '      }\n'
        '    }\n'
        '  }\n'
        '}\n',
        encoding='utf-8',
    )

    generated_root = tmp_path / 'feedback'
    execution_root = generated_root / 'execution_1'
    execution_root.mkdir(parents=True)
    (execution_root / 'generated_plans.json').write_text(
        '{\n'
        '  "iteration_2": {\n'
        '    "selected_candidate_plan_id": "plan_1",\n'
        '    "plan_1": {\n'
        '      "planning_request_id": "req_1",\n'
        '      "candidate_plan_id": "plan_1",\n'
        '      "graph_hash": "graph_1",\n'
        '      "selected": true,\n'
        '      "feasible": true,\n'
        '      "terminal_success": true,\n'
        '      "terminal_success_estimate": 0.83,\n'
        '      "final_rapfs": 0.77,\n'
        '      "reasoning": "community context was sufficient",\n'
        '      "candidate_skill_name_list": ["detect", "pick"]\n'
        '    }\n'
        '  }\n'
        '}\n',
        encoding='utf-8',
    )
    (execution_root / 'user_feedback.json').write_text(
        '{\n'
        '  "1": {\n'
        '    "planning_request_id": "req_1",\n'
        '    "candidate_plan_id": "plan_1",\n'
        '    "graph_hash": "graph_1",\n'
        '    "outcome": "success",\n'
        '    "feedback_success": true,\n'
        '    "feedback": "operator confirmed"\n'
        '  }\n'
        '}\n',
        encoding='utf-8',
    )

    widget = InspectorWidget(initial_db_path=str(db_path), initial_audit_root=str(audit_root))
    widget.set_generated_root(str(generated_root))
    widget.load_generated_plan_artifacts()
    app.processEvents()

    panel = widget.decision_support_widget
    assert panel is not None
    assert panel.request_table.rowCount() == 1
    assert 'favor_community: 1' in panel.summary_box.toPlainText()
    assert 'selected_predicted_success_probability' in panel.detail_box.toPlainText()
    assert 'selected_predicted_terminal_success_label' in panel.detail_box.toPlainText()
    assert 'community context was sufficient' in panel.detail_box.toPlainText()

    widget.close()
    app.processEvents()


def test_calibration_review_tab_summarizes_threshold_candidates(tmp_path):
    app = ensure_application([])
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_minimal_db(db_path)

    generated_root = tmp_path / 'feedback'
    for index, (request_id, score, success) in enumerate([
        ('req_1', 0.82, True),
        ('req_2', 0.68, True),
        ('req_3', 0.41, False),
        ('req_4', 0.22, False),
    ], start=1):
        execution_root = generated_root / f'execution_{index}'
        execution_root.mkdir(parents=True)
        (execution_root / 'generated_plans.json').write_text(
            '{\n'
            '  "iteration_1": {\n'
            f'    "selected_candidate_plan_id": "plan_{index}",\n'
            f'    "plan_{index}": {{\n'
            f'      "planning_request_id": "{request_id}",\n'
            f'      "candidate_plan_id": "plan_{index}",\n'
            f'      "graph_hash": "graph_{index}",\n'
            '      "selected": true,\n'
            '      "feasible": true,\n'
            f'      "terminal_success": {str(success).lower()},\n'
            f'      "terminal_success_estimate": {score},\n'
            f'      "final_rapfs": {score},\n'
            '      "reasoning": "test",\n'
            '      "candidate_skill_name_list": ["demo"]\n'
            '    }\n'
            '  }\n'
            '}\n',
            encoding='utf-8',
        )
        outcome = 'success' if success else 'failure'
        (execution_root / 'user_feedback.json').write_text(
            '{\n'
            '  "1": {\n'
            f'    "planning_request_id": "{request_id}",\n'
            f'    "candidate_plan_id": "plan_{index}",\n'
            f'    "graph_hash": "graph_{index}",\n'
            f'    "outcome": "{outcome}",\n'
            f'    "feedback_success": {str(success).lower()},\n'
            '    "feedback": "operator review"\n'
            '  }\n'
            '}\n',
            encoding='utf-8',
        )

    widget = InspectorWidget(initial_db_path=str(db_path))
    widget.set_generated_root(str(generated_root))
    widget.load_generated_plan_artifacts()
    app.processEvents()

    panel = widget.calibration_review_widget
    assert panel is not None
    assert panel.threshold_table.rowCount() >= 1
    assert panel.sample_table.rowCount() == 4
    assert 'best_single_threshold' in panel.summary_box.toPlainText()
    assert 'calibration_label_version' in panel.summary_box.toPlainText()
    assert 'recommended_success_threshold' in panel.detail_box.toPlainText()
    assert 'Runtime Policy Update' in panel.detail_box.toPlainText()

    widget.close()
    app.processEvents()


def test_review_tabs_can_jump_into_request_review(tmp_path):
    app = ensure_application([])
    db_path = tmp_path / 'core_graphrag.sqlite3'
    create_minimal_db(db_path)
    connection = sqlite3.connect(db_path)
    connection.execute(
        "INSERT INTO plan_node_score_explanation VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
        (
            'req_1', 'plan_2', 'graph_2', 'runner_2', 'demo/PickObject', 'pick', 'task:pick', 'task:pick', 'pick',
            'task:pick', 'empirical', 'context_key', 'requested_skill', 'picked after detection', 0.8, 'pick,grasp',
            'apple,gripper', 'gripper_ready', 'detect_then_pick', 'slip', 'manipulation summary', 'success_probability',
            '0.95', '1', 'posterior', 11, 21, 'test'
        ),
    )
    connection.commit()
    connection.close()

    audit_root = tmp_path / 'planner_prompt_audit'
    audit_root.mkdir()
    (audit_root / 'req_1_10_20.json').write_text(
        '{\n'
        '  "planning_request_id": "req_1",\n'
        '  "request_uuid": "uuid_1",\n'
        '  "task_description": "pick the apple",\n'
        '  "iterations": {\n'
        '    "iteration_1": {\n'
        '      "phase": "generation",\n'
        '      "selected_semantic_context_mode": "community",\n'
        '      "prompt_comparison": {\n'
        '        "detailed_chars": 1500,\n'
        '        "community_chars": 900,\n'
        '        "reduction_ratio": 0.4\n'
        '      },\n'
        '      "semantic_counts": {\n'
        '        "community_summaries": 1,\n'
        '        "relevant_capabilities": 2\n'
        '      }\n'
        '    }\n'
        '  }\n'
        '}\n',
        encoding='utf-8',
    )

    generated_root = tmp_path / 'feedback'
    execution_root = generated_root / 'execution_1'
    execution_root.mkdir(parents=True)
    (execution_root / 'generated_plans.json').write_text(
        '{\n'
        '  "iteration_1": {\n'
        '    "selected_candidate_plan_id": "plan_2",\n'
        '    "plan_1": {\n'
        '      "planning_request_id": "req_1",\n'
        '      "candidate_plan_id": "plan_1",\n'
        '      "graph_hash": "graph_1",\n'
        '      "selected": false,\n'
        '      "feasible": true,\n'
        '      "terminal_success": false,\n'
        '      "terminal_success_estimate": 0.31,\n'
        '      "final_rapfs": 0.28,\n'
        '      "reasoning": "fallback plan",\n'
        '      "candidate_skill_name_list": ["detect"]\n'
        '    },\n'
        '    "plan_2": {\n'
        '      "planning_request_id": "req_1",\n'
        '      "candidate_plan_id": "plan_2",\n'
        '      "graph_hash": "graph_2",\n'
        '      "selected": true,\n'
        '      "feasible": true,\n'
        '      "terminal_success": true,\n'
        '      "terminal_success_estimate": 0.83,\n'
        '      "final_rapfs": 0.77,\n'
        '      "reasoning": "community context was sufficient",\n'
        '      "candidate_skill_name_list": ["detect", "pick"]\n'
        '    }\n'
        '  }\n'
        '}\n',
        encoding='utf-8',
    )
    (execution_root / 'user_feedback.json').write_text(
        '{\n'
        '  "1": {\n'
        '    "planning_request_id": "req_1",\n'
        '    "candidate_plan_id": "plan_2",\n'
        '    "graph_hash": "graph_2",\n'
        '    "outcome": "success",\n'
        '    "feedback_success": true,\n'
        '    "feedback": "operator confirmed"\n'
        '  }\n'
        '}\n',
        encoding='utf-8',
    )

    widget = InspectorWidget(initial_db_path=str(db_path), initial_audit_root=str(audit_root))
    widget.set_generated_root(str(generated_root))
    widget.load_generated_plan_artifacts()
    app.processEvents()

    assert widget.decision_support_widget is not None
    widget.decision_support_widget.open_selected_request()
    app.processEvents()
    assert widget.request_review_widget is not None
    assert 'planning_request_id: req_1' in widget.request_review_widget.summary_box.toPlainText()
    assert 'candidate_plan_id: plan_2' in widget.request_review_widget.generated_plan_box.toPlainText()
    assert 'community context was sufficient' in widget.request_review_widget.generated_plan_box.toPlainText()

    assert widget.calibration_review_widget is not None
    for row_index in range(widget.calibration_review_widget.sample_table.rowCount()):
        item = widget.calibration_review_widget.sample_table.item(row_index, 1)
        if item is not None and item.text() == 'plan_2':
            widget.calibration_review_widget.sample_table.selectRow(row_index)
            break
    widget.calibration_review_widget.open_selected_sample_request()
    app.processEvents()
    assert 'planning_request_id: req_1' in widget.request_review_widget.summary_box.toPlainText()
    assert 'candidate_plan_id: plan_2' in widget.request_review_widget.generated_plan_box.toPlainText()

    assert widget.open_request_review_for_request('req_1', candidate_node_id='runner_2') is True
    app.processEvents()
    assert 'picked after detection' in widget.request_review_widget.explanation_preview.toPlainText()

    widget.close()
    app.processEvents()


def test_plan_sandbox_live_scoring_success(tmp_path):
    app = ensure_application([])
    widget = InspectorWidget()

    widget.plan_task_description_edit.setText('pick object')
    widget.plan_request_id_edit.setText('req_1')
    widget.plan_candidate_id_edit.setText('plan_1')
    widget.plan_candidate_node_id_edit.setText('runner_1')
    widget.plan_xml_edit.setPlainText('<Plan><Runner name="detect" /></Plan>')

    def fake_score(task_description, plan_xml, planning_request_id='', candidate_plan_id='', candidate_node_id=''):
        assert task_description == 'pick object'
        assert planning_request_id == 'req_1'
        assert candidate_plan_id == 'plan_1'
        assert candidate_node_id == 'runner_1'
        return {
            'plan': {
                'final_rapfs': 0.72,
                'expected_runtime_seconds': 5.5,
                'planning_request_id': 'req_1',
                'candidate_plan_id': 'plan_1',
            },
            'node_explanations': [
                {
                    'candidate_node_id': 'runner_1',
                    'skill_name': 'detect',
                    'reason': 'matched evidence',
                }
            ],
        }

    widget._score_plan_live = fake_score
    widget.score_plan_sandbox_live()
    app.processEvents()

    assert '"final_rapfs": 0.72' in widget.plan_score_edit.toPlainText()
    assert 'expected_runtime_seconds: 5.5' in widget.plan_summary_box.toPlainText()
    assert widget.status_label.text() == 'Loaded live score response into the Plan Sandbox overlay.'

    widget.close()
    app.processEvents()


def test_plan_sandbox_live_scoring_failure_preserves_offline_state(monkeypatch):
    app = ensure_application([])
    widget = InspectorWidget()

    widget.plan_task_description_edit.setText('pick object')
    widget.plan_xml_edit.setPlainText('<Plan><Runner name="detect" /></Plan>')
    widget.plan_score_edit.setPlainText('{}')
    widget.parse_plan_sandbox()
    baseline_node_count = len(widget.plan_nodes)

    captured = []

    def fake_critical(_parent, _title, text):
        captured.append(text)
        return QtWidgets.QMessageBox.Ok

    def fail_score(*_args, **_kwargs):
        raise RuntimeError('live scoring unavailable: missing ROS score client dependencies (rclpy and experience_msgs)')

    monkeypatch.setattr(QtWidgets.QMessageBox, 'critical', staticmethod(fake_critical))
    widget._score_plan_live = fail_score
    widget.score_plan_sandbox_live()
    app.processEvents()

    assert captured
    assert 'live scoring unavailable' in captured[0]
    assert widget.plan_score_edit.toPlainText() == '{}'
    assert len(widget.plan_nodes) == baseline_node_count

    widget.close()
    app.processEvents()