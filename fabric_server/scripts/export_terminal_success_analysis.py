#!/usr/bin/env python3

from fabric_server.terminal_success_analysis import (
    build_band_proposal,
    build_report,
    candidate_thresholds,
    collect_analysis_rows,
    evaluate_threshold,
    export_terminal_success_analysis,
    main,
    safe_bool,
    safe_float,
    write_csv,
)


if __name__ == '__main__':
    raise SystemExit(main())