#!/bin/bash
set -u
export SUITECUT_RUNTIME_EXECUTABLE=/tmp/suitecut-cef-full-regression-build/suitecut-browser
export pnpm_config_verify_deps_before_run=warn
for backend in cef chromium; do
  export COMPAT_BACKEND=$backend
  export COMPAT_DIRECTORY=/tmp/suitecut-remaining-fixes/final-$backend
  export MEMORY_DIRECTORY=$COMPAT_DIRECTORY
  mkdir -p "$COMPAT_DIRECTORY"
  taskset -c 0-3 python3 benchmarks/native-memory/runtime.py exec pnpm exec playwright test --config benchmarks/playwright-compatibility/playwright.config.ts > "$COMPAT_DIRECTORY/run.log" 2>&1
  echo "$backend exit=$?"
done
unset COMPAT_BACKEND
export MEMORY_DIRECTORY=/tmp/suitecut-remaining-fixes/native-final
mkdir -p "$MEMORY_DIRECTORY"
taskset -c 0-3 python3 benchmarks/native-memory/runtime.py playwright > "$MEMORY_DIRECTORY/run.log" 2>&1
echo "native exit=$?"
/tmp/suitecut-remaining-fixes/run-upstream.sh popup-events-final library/browsercontext-events.spec.ts
echo "popup events exit=$?"
