#!/usr/bin/env python3

from fabric_server import terminal_success_analysis


build_band_proposal = terminal_success_analysis.build_band_proposal
build_report = terminal_success_analysis.build_report
candidate_thresholds = terminal_success_analysis.candidate_thresholds
collect_analysis_rows = terminal_success_analysis.collect_analysis_rows
evaluate_threshold = terminal_success_analysis.evaluate_threshold
export_terminal_success_analysis = terminal_success_analysis.export_terminal_success_analysis
main = terminal_success_analysis.main
safe_bool = terminal_success_analysis.safe_bool
safe_float = terminal_success_analysis.safe_float
write_csv = terminal_success_analysis.write_csv


if __name__ == '__main__':
    raise SystemExit(main())
