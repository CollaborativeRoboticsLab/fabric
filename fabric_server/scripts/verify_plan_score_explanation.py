#!/usr/bin/env python3

from fabric_server import score_explanation_verifier


SEMANTIC_FIELDS = score_explanation_verifier.SEMANTIC_FIELDS
build_report = score_explanation_verifier.build_report
load_explanation_row = score_explanation_verifier.load_explanation_row
main = score_explanation_verifier.main
print_report = score_explanation_verifier.print_report
split_field = score_explanation_verifier.split_field
verify_plan_score_explanation = score_explanation_verifier.verify_plan_score_explanation


if __name__ == '__main__':
    raise SystemExit(main())
