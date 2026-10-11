#!/bin/bash
set -eu
name=$1
shift
export COMPAT_DIRECTORY=/tmp/suitecut-remaining-fixes/$name
export MEMORY_DIRECTORY=$COMPAT_DIRECTORY
export SUITECUT_RUNTIME_EXECUTABLE=/tmp/suitecut-cef-full-regression-build/suitecut-browser
export PWTEST_UNDER_TEST=1
export pnpm_config_verify_deps_before_run=warn
mkdir -p "$COMPAT_DIRECTORY"
taskset -c 0-3 python3 benchmarks/native-memory/runtime.py exec pnpm exec playwright test --config /tmp/suitecut-playwright-upstream/playwright-1.62.1/suitecut.config.ts "$@" --timeout=10000 > "$COMPAT_DIRECTORY/run.log" 2>&1
