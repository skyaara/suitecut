# Playwright compatibility verification

These are functional tests against the actual SuiteCut native browser, with a
stock Playwright Chromium control. They do not measure recording performance or
prove complete upstream compatibility.

The [remaining-fixes report](2026-10-11-remaining-fixes/report.md) records the latest
locale, request-header and concurrent-page corrections. The preceding
[follow-up report](2026-10-11-followup/report.md) preserves its earlier results. The [fallback report](2026-10-11/report.md) preserves the
preceding baseline, harness limits and exact versions. Raw JSON reports, logs,
videos, HARs, exported state and failure artifacts remain in each attempt's
directory. `summary.json` and `cases.csv` include earlier failures as well as
later verification; they are not a single combined compatibility score.

## Focused matrix

Build the SDK and native runtime first. Supply a compatible codec-enabled CEF
runtime, a display, PulseAudio and FFmpeg. This workspace's dependency wrapper
owns a display/audio service and serializes runs with its existing lock.

```bash
export COMPAT_DIRECTORY="$PWD/benchmarks/playwright-compatibility/local-cef"
export MEMORY_DIRECTORY="$COMPAT_DIRECTORY"
export SUITECUT_RUNTIME_EXECUTABLE=/absolute/path/to/suitecut-browser
pnpm_config_verify_deps_before_run=warn taskset -c 0-3 \
  python3 benchmarks/native-memory/runtime.py exec \
  pnpm exec playwright test --config benchmarks/playwright-compatibility/playwright.config.ts
```

Use a new evidence directory and `COMPAT_BACKEND=chromium` for the stock control.
The native-only CDP alias cleanup test skips on stock Chromium. Authentication
combination tests retain positive assertions. The pinned Playwright patch now
resolves the earlier overwrite; see [patch setup](../../patches/README.md). All local proxy passwords and tokens are dummy fixture data.

Without the workspace wrapper, set `DISPLAY`, working audio dependencies and
`SUITECUT_NATIVE_EXECUTABLE`, then run the Playwright command directly.

## Pinned upstream tests

The source is the official Playwright `v1.62.1` archive with SHA-256
`f6b38805b0b021423bb908cd7682a455283c22742bf7d95c190927f3fb11172c`.
Download that tag's source archive from Microsoft's Playwright repository or use
the preserved archive accompanying this report.

```bash
python3 benchmarks/playwright-compatibility/prepare-upstream.py \
  /absolute/path/to/playwright-v1.62.1-source.tar.gz /tmp/new-upstream-staging
```

This stages original test bodies and assets, installs pinned extra test-harness
dependencies outside the repository, and applies only the saved browser fixture
override. Installed Playwright and Playwright Core must both be `1.62.1`.
The fixture launches SuiteCut and exposes its actual Playwright browser object.
Independent upstream launch/server/persistent-profile tests explicitly throw
outside-harness errors rather than silently testing stock Chromium.

Use `suitecut.config.ts` for selected files and `suitecut-core.config.ts` for the
saved bounded broad attempt. Launch with the same display/audio wrapper as above,
`PWTEST_UNDER_TEST=1`, and an absolute `COMPAT_DIRECTORY`. Set
`COMPAT_BACKEND=chromium` to run the same upstream test bodies with stock Chromium.
The filtered broad attempt has an eight-minute global timeout and 75-failure
limit. Its whole-file exclusions are saved with reasons; this is not the full
upstream compatibility suite.

```bash
python3 benchmarks/playwright-compatibility/summarize.py \
  benchmarks/playwright-compatibility/2026-10-11
```

The native helper accompanying the report is a replacement executable requiring
the matching complete 2.1.0 runtime resources and libraries. The previously
sealed release archives were not rebuilt or published by this investigation.

TLS fixtures in `fixtures/` are the upstream Playwright dummy test certificate
and key, copied from the pinned source archive; they are not production credentials.
