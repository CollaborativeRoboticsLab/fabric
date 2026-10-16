from importlib.util import module_from_spec, spec_from_file_location
from pathlib import Path


MODULE_PATH = Path(__file__).resolve().parents[1] / 'scripts' / 'summarize_planner_prompt_audit.py'
SPEC = spec_from_file_location('summarize_planner_prompt_audit', MODULE_PATH)
assert SPEC is not None
assert SPEC.loader is not None
MODULE = module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def test_build_report_tracks_generation_and_refinement_iterations():
    documents = [
        {
            'planning_request_id': 'req-1',
            'request_uuid': 'uuid-1',
            'task_description': 'pick the apple',
            'iterations': {
                'iteration_1': {
                    'phase': 'generation',
                    'iteration': 1,
                    'selected_semantic_context_mode': 'detailed',
                    'prompt_comparison': {
                        'detailed_chars': 1500,
                        'community_chars': 900,
                        'reduction_chars': 600,
                        'reduction_ratio': 0.4,
                    },
                    'semantic_counts': {
                        'relevant_capabilities': 1,
                        'related_entities': 0,
                        'dependencies': 0,
                        'plan_patterns': 0,
                        'fault_concepts': 0,
                        'community_summaries': 0,
                    },
                },
                'iteration_2': {
                    'phase': 'refinement',
                    'iteration': 2,
                    'selected_semantic_context_mode': 'community',
                    'prompt_comparison': {
                        'detailed_chars': 1600,
                        'community_chars': 1000,
                        'reduction_chars': 600,
                        'reduction_ratio': 0.375,
                    },
                    'semantic_counts': {
                        'relevant_capabilities': 1,
                        'related_entities': 1,
                        'dependencies': 0,
                        'plan_patterns': 0,
                        'fault_concepts': 0,
                        'community_summaries': 2,
                    },
                    'refinement_context': {
                        'attempt_index': 0,
                        'draft_plan_chars': 320,
                        'critique_chars': 120,
                    },
                },
            },
        }
    ]

    report = MODULE.build_report(documents, high_prompt_chars=1200, low_signal_items=2)

    assert report['summary']['request_count'] == 1
    assert report['summary']['iteration_count'] == 2
    assert report['summary']['refinement_iteration_count'] == 1
    assert report['summary']['community_signal_iterations'] == 1
    assert report['summary']['high_prompt_low_signal_iterations'] == 1
    assert report['summary']['community_mode_viability']['status'] == 'needs_review'

    iteration_1 = report['iteration_prompt_audit'][0]
    assert iteration_1['phase'] == 'generation'
    assert iteration_1['high_prompt_low_signal'] is True
    assert iteration_1['selected_prompt_trend'] == 'n/a'

    iteration_2 = report['iteration_prompt_audit'][1]
    assert iteration_2['phase'] == 'refinement'
    assert iteration_2['community_has_signal'] is True
    assert iteration_2['selected_prompt_chars'] == 1000
    assert iteration_2['selected_prompt_delta_from_previous'] == -500
    assert iteration_2['selected_prompt_trend'] == 'shrank'

    request_row = report['request_prompt_audit'][0]
    assert request_row['refinement_iteration_count'] == 1
    assert request_row['high_prompt_low_signal_iteration_count'] == 1
    assert request_row['final_selected_prompt_trend'] == 'shrank'


def test_assess_community_mode_viability_reports_viable_when_signal_and_reduction_are_strong():
    iteration_rows = [
        {
            'selected_semantic_context_mode': 'community',
            'prompt_reduction_ratio': 0.35,
        },
        {
            'selected_semantic_context_mode': 'detailed',
            'prompt_reduction_ratio': 0.25,
        },
    ]
    summary = {
        'community_signal_iterations': 2,
        'iteration_count': 2,
    }

    verdict = MODULE.assess_community_mode_viability(summary, iteration_rows)

    assert verdict['status'] == 'viable'
    assert verdict['community_signal_ratio'] == 1.0
    assert verdict['community_selected_iteration_count'] == 1


def test_assess_community_mode_viability_reports_not_viable_when_signal_is_sparse():
    iteration_rows = [
        {
            'selected_semantic_context_mode': 'detailed',
            'prompt_reduction_ratio': 0.05,
        },
        {
            'selected_semantic_context_mode': 'detailed',
            'prompt_reduction_ratio': 0.08,
        },
    ]
    summary = {
        'community_signal_iterations': 0,
        'iteration_count': 2,
    }

    verdict = MODULE.assess_community_mode_viability(summary, iteration_rows)

    assert verdict['status'] == 'not_viable_yet'
    assert verdict['community_selected_iteration_count'] == 0