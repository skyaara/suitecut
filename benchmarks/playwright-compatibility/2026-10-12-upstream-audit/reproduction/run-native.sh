#!/bin/bash
set -u
cd /workspace/suitecut
export PWTEST_UNDER_TEST=1
export COMPAT_BACKEND=cef
export COMPAT_DIRECTORY=/tmp/suitecut-upstream-full-2026-10-12/native
export MEMORY_DIRECTORY="$COMPAT_DIRECTORY"
export SUITECUT_RUNTIME_EXECUTABLE=/tmp/suitecut-cef-full-regression-build/suitecut-browser-upstream-audit
export pnpm_config_verify_deps_before_run=warn
sha256sum "$SUITECUT_RUNTIME_EXECUTABLE" native/browser/browser.cc src/engine.ts src/native-browser.ts patches/playwright-core@1.62.1.patch /tmp/suitecut-playwright-upstream/playwright-1.62.1/tests/config/browserTest.ts /tmp/suitecut-playwright-upstream/playwright-1.62.1/suitecut-audit.config.ts > /tmp/suitecut-upstream-full-2026-10-12/frozen-sha256.txt
taskset -c 0-3 python3 /tmp/suitecut-upstream-full-2026-10-12/runtime.py exec pnpm exec playwright test --config /tmp/suitecut-playwright-upstream/playwright-1.62.1/suitecut-audit.config.ts > /tmp/suitecut-upstream-full-2026-10-12/native-run.log 2>&1
printf '%s\n' "$?" > /tmp/suitecut-upstream-full-2026-10-12/native-exit.txt
