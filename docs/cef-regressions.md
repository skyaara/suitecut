# CEF real-browser regression suite

This suite runs the actual embedded CEF executable through SuiteCut's public
JavaScript and C++ SDKs. It owns local application servers and fresh browser
profiles. It does not use public websites, external accounts, CAPTCHA solvers,
fingerprint patches, or third-party uptime as a release gate.

**The runner is ready for future CI use; no CI configuration is changed or
enabled by this work. A failing regression is a failure, including shutdown.**

## Run locally

Use Node 22 or newer, Python 3.11 or newer, and OpenSSL CLI on PATH on macOS or Linux. Build the
JavaScript SDK first. Select an actual CEF executable explicitly; the runner
does not download a different browser or silently use Chrome.

```sh
pnpm install --frozen-lockfile
pnpm build
python3 tests/cef --list
python3 tests/cef \
  --engine /absolute/path/to/suitecut-browser \
  --profile smoke
```

On macOS the executable is inside
`suitecut-browser.app/Contents/MacOS/suitecut-browser`. Use a local signed app
with its helper apps and CEF framework in place. Use the configured fixed signing identity for repeated local runs; the build and
packager share `scripts/native_macos_signing.py`. A configured missing certificate
is an error. See `docs/native-full-runtime-macos.md` for the signing policy. Linux needs the browser's runtime
libraries and a working display, for example an existing Xvfb session. The
supervisor inherits `DISPLAY`; it does not configure the machine for you.

Build the standalone C++ client and its real-browser test binaries without
rebuilding Chromium:

```sh
cmake -S native/client -B .suitecut/cef-regression-client \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/cef-regression-client --parallel 2 --target \
  native-client-real-browser \
  suitecut-native-cross_browser_features \
  suitecut-native-cef_artifact_workflows \
  suitecut-native-cef_restricted_bootstrap \
  suitecut-native-cef_media_workflows
python3 tests/cef \
  --engine /absolute/path/to/suitecut-browser \
  --client-build .suitecut/cef-regression-client \
  --ffmpeg /absolute/path/to/ffmpeg \
  --ffprobe /absolute/path/to/ffprobe \
  --profile full --workers 2 --seed 23
```

The media tools default to executables on `PATH`. Missing required binaries
fail preflight rather than skipping their tests. They must support VP9, Opus,
and the codecs used by the selected native runtime. Prefer the tools shipped
in the runtime being qualified. Source/build identities are recorded; this is
not a substitute for building the chosen binaries from the intended source.

## Selection, repetition and future CI usage

| Profile    | Scope                                                                 |
| ---------- | --------------------------------------------------------------------- |
| `smoke`    | Eight high-value application, frame, bootstrap and artifact scenarios |
| `journeys` | All 29 new real-world application, race and media scenarios           |
| `races`    | Race-tagged new scenarios and existing ownership/lifecycle proofs     |
| `proofs`   | 56 existing real-CEF proofs, each isolated in its own workspace       |
| `full`     | 29 new scenarios, 56 existing proofs, five real C++ workflows         |

Explicit `--case` selects from the entire catalogue, overriding the profile:

```sh
python3 tests/cef \
  --engine /absolute/path/to/suitecut-browser \
  --case 'race.*' --case 'bootstrap.*' \
  --seed 100 --repeat 10 --workers 2
```

Each repetition gets a fresh process/profile and a separate result. The seed
controls reproducible scheduling order and test data/init versions. It is not
a deterministic Chromium thread scheduler or exhaustive concurrency fuzzer.
Races also have explicit server/callback/native-diagnostic barriers so their
critical transitions do not depend on a guessed sleep duration.

Workers default to **one** and are capped at **four**; use one or two on a
small Mac. A test may create a bounded number of browser pages, C++ fixture
threads, and native workers internally. This runner does not alter the
production worker pool. Run larger load/soak tests on a dedicated machine.

`--shard 1/4` through `--shard 4/4` assign each `(case, seed, repetition)` to
exactly one shard using a stable SHA-256 hash. Start each shard with identical
selection/seed/repeat arguments and a distinct output directory. Combine the
JUnit files in the future CI system. An empty selection or empty shard is an
error. `--timeout SECONDS` overrides the documented per-attempt hard budget;
the default budgets are in the catalogue. No automatic retries are enabled.

Each invocation creates a new output directory under
`.suitecut/cef-regressions/`. `--output /new/absolute/directory` selects a fresh
destination and refuses an existing one. Expected CI usage is simply to run
the command, require exit status zero, and retain the directory and JUnit file
**even on failure**. Do not add `|| true` or convert timeout failures into skips.

## Structure and correctness boundaries

The package entry point is `python3 tests/cef`; the existing
`scripts/run-cef-regressions.py` remains a thin compatibility wrapper.

| Component | Responsibility |
| --- | --- |
| `tests/cef/harness/catalogue.py` | Explicit inventory, schema checks and strict selection |
| `tests/cef/harness/contracts.py` and `evidence.py` | Typed report contracts, assertion coverage and input inventories |
| `tests/cef/harness/runner.py` | Bounded scheduling, immutable plan and attempt identity accounting |
| `tests/cef/harness/supervisor.py` and `processes.py` | Deadlines, log limits and verified owned-process retirement |
| `tests/cef/harness/reporting.py` | JSON/HTML/JUnit, including global integrity failures |
| `tests/cef/harness/worker.mjs` | Browser/server lifetime, first-failure artifacts, cleanup assertions |
| `tests/cef/fixtures/app.mjs` | Authenticated local app, media, frames, workers and held gates |
| `tests/cef/scenarios/` | Separate application, frames, network, race, artifact and media modules |
| `tests/cef/manifests/` | Proof/C++ cases, resource budgets and mandatory evidence contracts |
| `tests/cef/unit/` | Supervisor, process ownership, signing and false-green negative controls |
| `native/client/tests/{unit,integration,support}/` | Explicit standalone C++ targets and test-only fixtures |

See [the package contribution rules](../tests/cef/CONTRIBUTING.md).

Application setup may use `evaluate` to create authored DOM, workers or
test-owned load. User actions use native locators/input, not synthetic clicks
or direct writes to final form values. Assertions inspect actual DOM, server
commits, bytes, frame identities, callback receipts and native diagnostics.

The race tests explicitly distinguish acceptable outcomes. A popup's own
`window.close()` can beat its final click receipt: the approval must still
reach the opener and a real close event must occur. A late reply to a retired
route may reject or become a no-op; it must drain and leave the owner healthy.
An arbitrary timeout is not an acceptable substitute for a required detach or
navigation cancellation. The SDK allows a bounded 75-second process drain so
the native 60-second recorder/publisher finalization deadline can settle; a
nonzero exit or forced termination still fails the attempt.

Frame-owner navigation fixtures wait for the new URL before querying a control
that also exists in the old document. Storage-cancellation fixtures finish their
actual upload before starting the restore-admission observation budget; the
required native pending snapshot and physical lease are still asserted. Child
screenshot preparation waits for a real HTML root in an already-created utility
realm under the original operation deadline, with one cleanup record per realm.


Before teardown, failed attempts retain page screenshots, HTML when available,
native diagnostics, and the original error/stack. Then the worker releases
owned gates, closes fixture sockets/contexts, checks selected native counters,
and requires a clean browser exit. Native stderr and unhandled rejections fail
new scenarios. The outer supervisor also fails a leaked process even when it
successfully cleans it up. It signals its newly created process group and
verified owned descendants, including browsers placed in separate groups by
the C++ SDK. PID and start identity are rechecked before signaling descendants.
It does not kill browsers by application name or touch the user's profiles.

On macOS, the persistent-profile proof intentionally exercises real Keychain-backed
cookie persistence. Use an unlocked test account whose Keychain permits the
qualified application's identity. An unapproved ad-hoc rebuild can block inside
`SecItemCopyMatching`, stalling navigation and clean shutdown; this is a failure,
not a skip. Disposable profiles use the mock keychain. The profile proof records
its failing stage and original exception before attempting cleanup so a later
shutdown failure cannot hide the first error. Never disable encryption for a
real persistent profile just to make this check pass.

Existing proofs keep their own assertions and failure artifacts. Their working
directories are private per attempt, so their `.suitecut` paths do not
overwrite other attempts or prior release evidence. The launcher records the
exact engine, renderer helper, SDK modules, test files and C++ binary hashes
before and after the run. Input changes make the whole run fail, including
JUnit, even if individual cases passed.

## Coverage mapped to implementation

| Real scenario / invariant                                                                                                            | Implementation families exercised                                                                           |
| ------------------------------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------- |
| Login, HttpOnly sessions, separate users, logout, exported auth state                                                                | `native-context.ts`, `native-storage-state.ts`, CEF context/storage controllers, C++ facade                 |
| Validated/idempotent checkout, editor, Unicode, shadow DOM, native keyboard and detached input                                       | `native-page.ts`, form/input/selector/edit/focus controllers and retained handles                           |
| Native uploads, Unicode download names, byte hashes, screenshots, clips, traces and PDF                                              | file chooser/download/artifact/trace ownership, C++ artifact workflows                                      |
| Main/local/remote/nested first-author init and repeated accepted mutations under `sync-xhr=()`                                       | `renderer_app.h`, `renderer_script_sources.h`, `pristine_dispatch_source.inc`, context snapshot publication |
| Popup approval, redirects/history, held navigation close, full command capacity                                                      | page/context events, navigation identity, reserved cleanup admission, transport command receipts            |
| Search response inversion, binary bodies, incomplete chunks, route close/removal, HAR replay and teardown                            | network/body/HAR/route sessions, C++ network features                                                       |
| Local TLS/custom CA trust, origin boundaries, Basic auth, CORS preflight, multipart transfer, WebSocket close and persistent profile | Native HTTP/OpenSSL cookie/origin/CA policy, request/preflight interception, socket and profile lifecycle   |
| Service-worker offline app, worker replacement, target/session destruction                                                           | worker brokers/generations, service routes, native target and session lifecycle                             |
| Frame removal during host callback, navigation during evaluation, stale retained handles                                             | binding leases, realm/frame owners, evaluation groups and physical retirement                               |
| Crash/recovery with pending work and listener errors                                                                                 | recovery generation, deadlines, network/input/artifact cleanup                                              |
| Owned/unowned debugger pauses, coverage owner/OOPIF lifetime                                                                         | debugger-domain leases, pristine dispatch, coverage targets/sessions                                        |
| Live I420 and PCM under bounded load; recording start/pause/resume/cancel; decoded frame and audio checks                            | capture/audio/media pipeline, recorder controls, C++ and JavaScript protocol contracts                      |

The five C++ scenarios add strict restricted-frame bootstrap, 50 context/page
lifecycle cycles, a real HTTP feature suite, screenshot/PDF/frame/popup checks,
and real media playback/recording. The existing proofs additionally saturate
binding/route capacity and validate physical ownership beyond logical close.

## Supervisor self-tests

```sh
python3 -m unittest discover -s tests/cef/unit -p 'test_*.py'
```

These launch real short-lived Python processes and verify: normal completion,
nonzero exit, deadline enforcement, interruption, orphan cleanup, rejecting
failed/missing assertions despite exit zero, preserving failed repetitions,
reflecting suite integrity errors in JUnit, tracking children across process-group
changes, leaving unrelated processes alone, and reaping the child even when
process inspection fails.

## CMake / CTest entry points

The client owns explicit source inventories in `native/client/tests/CMakeLists.txt`.
Missing sources fail configuration. Configure/build/test presets `release` and
`sanitizers` are in `native/client/CMakePresets.json`; unit tests have CTest labels
and timeouts. The `client-unit-binaries` and `client-cef-binaries` targets build
just the required test groups.

The harness also has a standalone CTest project:

```sh
cmake -S tests/cef -B .suitecut/build/cef-harness
ctest --test-dir .suitecut/build/cef-harness --output-on-failure
```

To register the real-browser full suite, configure with
`-DSUITECUT_CEF_REAL_BROWSER_TESTS=ON`, `-DSUITECUT_CEF_ENGINE=/absolute/executable`,
`-DSUITECUT_CLIENT_BUILD=/absolute/build`, `-DSUITECUT_FFMPEG=/absolute/ffmpeg` and
`-DSUITECUT_FFPROBE=/absolute/ffprobe`. Missing dependencies fail configuration.
The full CTest uses two workers, runs serially relative to other CTest cases, and
has a 1,800-second outer deadline. Every invocation gets a fresh evidence directory.
These entry points do not enable or change CI.

## Strict qualification

Each proof declares the exact report path, terminal success conditions and minimum
assertion inventory. Unrelated JSON cannot make a case pass. Journey receipts must
match the selected case and seed, identify CEF, contain assertions, report clean
process exit and have empty cleanup/unhandled-error arrays. Reports are parsed
without duplicate keys or non-finite values; booleans cannot be replaced with strings
or integers. Checks remain enabled under Python `-O`.

Every `--case` selector must match. `plan.json` records intended attempt identities;
missing, duplicate or unexpected attempts fail final qualification. Nested source
inventories are pinned before/after; adding or removing an input also fails. The
full result remains red until every planned attempt and suite integrity check pass.
Static media artifacts are required and decoded; absent recordings are failures.

## Adding a regression

1. Give the case a stable ID and map it to the implementation family and actual
   incident. Put related cases in a small domain module.
2. Start from an isolated local app/context. Use an explicit request/callback
   admission barrier before teardown, reordering or replacement.
3. Assert the application effect and owner identity. For a race, specify both
   the allowed outcome and forbidden side effect. Assert cleanup/late delivery
   behavior where observable.
4. Retain first-failure evidence. Do not broaden an assertion to accept generic
   timeouts just to make a failure green. Use bounded resources and a per-case
   hard budget.
5. Run it against the intended compiled runtime and SDK. Where possible, run
   the old failing artifact as a negative control, keeping that evidence.

## Explicit limits

This is broad, finite regression coverage. It cannot prove all websites, all
race interleavings, every device or the absence of all leaks. It is not the
entire Web Platform Tests conformance suite. The catalogue does not treat
unsupported native capabilities as passing implementations: directory upload,
full clock control, WebSocket routing, some cross-navigation coverage and
offscreen OOPIF capture are still separate capability limits.

The new capture test checks actual packets and load responsiveness; the media
tests decode short recordings and compare counts/audio. They do **not** replace
the sustained 1080p120 unique-frame/A-V continuity qualification in
`scripts/verify-native-macos-recording.py`, long-duration soak tests, sanitizer
runs, codec licensing checks, or release packaging/signing qualification.
Turnstile and other public bot checks should remain explicit opt-in manual
integration checks, not an Internet-dependent CI gate.

Design inspiration: [Playwright test isolation and user-visible assertions](https://playwright.dev/docs/best-practices),
[Playwright fixture lifetimes](https://playwright.dev/docs/test-fixtures), and
[Web Platform Tests asynchronous harness](https://web-platform-tests.org/writing-tests/testharness-api.html).
These inform the design; this suite drives SuiteCut's own SDK and does not
claim to run those upstream conformance suites.

## Cache-policy and platform fixtures

The worker lifetime proof uses `SUITECUT_NATIVE_WORKER_LIFETIME_CACHE_POLICY=disabled`
explicitly: its immediate worker-death assertions require destruction of the old
owning document. A separate worker-cache-policy proof exercises omitted, preserved,
and disabled policies against real targets and retained author state. Reports
retain these selected case settings. No runtime default is changed.

The keyboard fixture uses the platform's select-all modifier and independently
checks literal Control events. The screenshot geometry fixture hides scrollbars
in its own CSS so a native scrollbar cannot occupy an asserted page-content pixel.
The HAR ownership proof captures its own genuine replay archive in each attempt.
AI reference checks follow backend-node identity across snapshots and verify stale
references after actual node replacement. Route fallback covers supported chunked
900 KB bodies and rejects bodies exceeding 2 MiB before advancing the next handler.

Service-worker request-body acknowledgement is asynchronous: its ownership proof
records native diagnostics until both body owners and physical controls reach
zero, with a fixed five-second deadline. This observes actual acknowledgements;
it never clears counters to satisfy the assertion. Publisher admission also
discards valid pre-start captures while preserving their source timestamps;
malformed frame byte lengths still fail.

The fallback proof observes the actual native continuation ACK under a five-second
monotonic retirement budget. A completed fetch is not itself proof that the
DevTools command owner has been released. Every observed ownership sample is
retained; all five physical counters must reach zero without resetting state.
