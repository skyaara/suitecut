# SuiteCut 2.1: additional compatibility fixes

Prepared 2026-10-11, starting from `7ae8bb76a9b1c0a27220183841e4f10e1ba3525f`.
The pushed fallback `80759aeb30cc4f6bd4ab488554569c62851985d2` is unchanged.

This is a functional compatibility investigation using CEF 152.0.6 / Chromium
152.0.7977.83 and the explicitly patched Playwright 1.62.1 driver. Browser runs
use CPU affinity 0–3 on a host limited to four CPUs and 16 GiB RAM. These results
are not the published four-CPU/eight-GiB recording and streaming benchmark.

## Changes and validation

1. **Downloads:** denied downloads emit the expected event and failure, cancel
   stalled HTTP responses, and never select a destination file. `download.cancel()`
   works for pending downloads and is harmless after completion. Closing the
   source reports cancellation and releases download callback ownership.
2. **Independent page CDP sessions:** explicit sessions now attach to separate
   Chromium debugger sessions. Playwright coverage can disable its debugger
   without disabling the caller's debugger. Events are delivered to the correct
   session; detach closes the actual session before acknowledging completion.
   Pending attaches count against the session limit. Raw-session window lookup
   works, including the 3840×2160 regression. The iframe observer test now enables
   its own target attachment on both backends, as independent sessions require.
3. **Local-network permissions:** the legacy protocol permission and Chromium's
   separate local-network/loopback permissions map to CEF policy. Tests cover a
   public-origin request, grant, denial, reset and isolation between contexts.
4. **Desktop input and automation metadata:** windowless CEF now uses Chromium's
   headless desktop hover/fine-pointer defaults and enables automation reporting.
   Mobile touch overrides still pass. `navigator.webdriver` reports `true`.
5. **Before-unload dialogs:** the message matches Chromium's empty message
   instead of supplying an invented string. Renderer failures also include the
   termination status and exit code for diagnosis.

| Final check | Result |
| --- | --- |
| Focused CEF compatibility matrix | 43 passed |
| Same matrix on stock Chromium with the pinned driver patch | 42 passed, 1 native-only skip |
| Selected upstream downloads, before-unload, CDP sessions, permissions, mobile viewport, media queries and webdriver | 104 passed, 4 platform skips |
| Native engine recording, streaming, pause, context/session capacity and shutdown | 16 passed |
| SDK command/delivery/native/browser-connection unit tests | 41 passed |
| Native CTest | 9 passed |
| TypeScript, targeted ESLint, formatting and native build | Passed |

The explicit upstream CDP-session file also passes all 15 cases. Those cases are
included in the final 104-case run and must not be added again. Repetitions and
matrix overlap are not additional compatibility coverage. No upstream test body
was edited to obtain a pass. `metadata.json` lists the eight excluded case-title
patterns; unsupported cases remain visible in the initial inventories.

## Scope of the exploratory runs

The initial library inventory schedules 4,221 tests (including one pre-existing
local diagnostic case that was not reached), but stops at its 60-failure
limit before reaching the page files. Its 814 passes include API-request and
utility tests; they are not all browser-rendering coverage. Twenty-two tests
were skipped and 3,325 were not run. The complete JSON and case inventory retain
failures from unsupported independent-launch fixtures rather than counting them
as SuiteCut browser failures.

A separate page inventory has a ten-minute global budget. It is exploratory:
helpers were rebuilt during the run, and new workers can use a later helper.
It must not be treated as a single-binary release qualification. Its JSON records
1,419 expected outcomes, 24 unexpected outcomes, and 1,062 skipped/not-run cases;
the deadline also interrupted teardown. Expected-failure tests and the deadline
make raw execution-status counts differ from the reporter summary. Final regression
runs are separate and their source and helper hashes are recorded.

Neither inventory runs the entire upstream compatibility suite. Tests not run
are separate from passed and intentionally skipped tests in `cases.csv`.

## Discarded experiments

Enabling the page domain earlier did not resolve popup prompts that block the
initial frame-tree response. A private Chromium debugging pipe successfully
returned browser metadata, attached to an existing CEF page, and evaluated an
expression after `Runtime.runIfWaitingForDebugger`. However, popup probes stalled
without delivering the required new-target attachment. The pipe is diagnostic
only; no debugging port or new pipe implementation is shipped by these changes.

An expanded permission/content-setting experiment crashed the native browser.
A reduced probe identified `WAKE_LOCK_SCREEN` as an unsafe content-setting write
for this runtime. Storage-access grants also failed the upstream test. Both
experiments were removed; unsupported grants continue to fail explicitly.
The existing clipboard bitmask already merges aliases correctly and is retained.

Restricting automatic authentication interception to HTTP/WebSocket patterns and
then disabling it entirely did not fix the iframe crash; both changes were
reverted. Neither automatic interception experiment is shipped.

## Memory and lifecycle limits

Download bookkeeping is bounded to 64 active downloads per source. Terminal
updates release cancellation callbacks and IDs; source closure moves the maps
out before cancelling callbacks, avoiding iterator invalidation if callbacks
reenter. A repeated cancellation regression exceeds that capacity over time and
checks that cancelled HTTP requests close. This is a lifecycle regression, not
proof of zero leaks in CEF or indefinitely long sessions. No new full-CEF
sanitizer, fuzzing or recording-performance claim is made by this report.


## Remaining issues

- **Immediate popup initialization:** early console/dialog events, first-request
  routing and some inherited settings still race initialization. The private-pipe
  probes do not establish a fix.
- **Inline iframe HTML on a blank origin:** `page.setContent()` containing a
  `srcdoc` iframe crashes the renderer in the reproduced cases (native status 2,
  exit 5). A smaller native-only reproduction fails on the initial `about:blank`
  page, even without Playwright or automatic Fetch interception. Navigating or
  reloading another blank page does not help. The same HTML works after HTTP
  navigation. Installed stock CEF 152.0.13 and 154.0.34 helpers reproduce this
  distinction too. This narrows the problem to the blank-origin embedding path;
  it does not prove whether the root cause is upstream CEF or shared SuiteCut
  setup. The implementation does not silently change the page's origin to hide it.
- **Storage-access and screen-wake-lock permission overrides:** still explicitly
  unsupported. Website API availability is different from automation of grants.
- **Partitioned cookies (CHIPS):** the CEF cookie-manager bridge still rejects
  explicit partition keys.
- **Oversized-element screenshot borders and a stable-position wait:** failures
  remain in the exploratory page inventory; they need targeted investigation.
- **Full upstream coverage:** launch/server/multiclient fixtures need a different
  adapter, a Playwright-client bundle is absent, and many tests were not run.
  Raw worker-session domain independence was not established by the page-session
  fix. No complete Playwright-conformance claim is made.

Some inventory differences are intentional or host-dependent: inline PDFs open
in the embedded PDF viewer rather than always downloading; autoplay is enabled
for recording; software WebGL requires the existing explicit SwiftShader option
on this GPU-less host. A 100 MB page-to-Node transfer exceeds the bridge's 16 MiB
CDP-message bound. These must not be presented as passed upstream behavior.

## Saved evidence and reproduction

Repository text logs have trailing whitespace normalized; the archive preserves
raw output. `cases.csv`, `summary.json`, `inventory-resolution.json` and
`unresolved-cases.json` retain case-level outcomes, including failed experiments.
`tested-sources/` and `metadata.json` identify the final source, native helper and
patched driver. `upstream-harness/` records the fixture adapter, configurations,
runner and exclusions; the test bodies come from Playwright v1.62.1. The earlier
[compatibility report](../2026-10-11/report.md) records upstream setup details.

The complete logs, JSON reports, screenshot differences, diagnostic scripts,
failed attempts and final recording artifacts are in `evidence.tar.gz`, with a
copy and the matching native helper under
`/workspace/shared/suitecut-suite-fixes-2026-10-11/`. The helper requires its
matching CEF/FFmpeg runtime; it is not a standalone distribution. No npm package,
release tag, runtime archive or benchmark website was published by this change.

For the focused matrix, supply a matching native executable and the existing
pinned driver patch, then run:

```sh
COMPAT_DIRECTORY=/tmp/suitecut-verify \
MEMORY_DIRECTORY=/tmp/suitecut-verify \
SUITECUT_RUNTIME_EXECUTABLE=/absolute/path/to/suitecut-browser \
pnpm_config_verify_deps_before_run=warn \
taskset -c 0-3 python3 benchmarks/native-memory/runtime.py exec \
  pnpm exec playwright test --config benchmarks/playwright-compatibility/playwright.config.ts
```

This workspace's dependency metadata warns that the lockfile patch setting is
out of sync; the installed driver hash matches the explicit pinned patch verified
in the previous fix. No automatic patching of downstream installations is added.
