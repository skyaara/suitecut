# CEF real-browser regression suite

This suite runs the actual embedded CEF executable through suitecut's C++ SDK.
Its cases are the five C++ workflows listed in `tests/cef/manifests/cpp.json`;
a standard-library Python supervisor schedules them, enforces deadlines and
checks their evidence. The workflows own their local fixtures and fresh browser
profiles. They do not use public websites, external accounts, CAPTCHA solvers,
fingerprint patches, or third-party uptime as a release gate.

**The full suite runs locally; CI runs only its harness unit tests
([CI and releases](ci.md)). A failing regression is a failure, including shutdown.**

## Run locally

Use Python 3.11 or newer and OpenSSL CLI on PATH on macOS or Linux. Select an
actual CEF executable explicitly; the runner does not download a different
browser or silently use Chrome.

On macOS the executable is inside
`suitecut-browser.app/Contents/MacOS/suitecut-browser`. Use a local signed app
with its helper apps and CEF framework in place. Use the configured fixed signing identity for repeated local runs; the build and
packager share `scripts/native_macos_signing.py`. A configured missing certificate
is an error. See `docs/native-full-runtime-macos.md` for the signing policy. Linux needs the browser's runtime
libraries and a working display, for example an existing Xvfb session. The
supervisor inherits `DISPLAY`; it does not configure the machine for you.

Build the standalone C++ client and its real-browser test binaries without
rebuilding Chromium, then run the suite:

```sh
cmake -S native/client -B .suitecut/cef-regression-client \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/cef-regression-client --parallel 2 --target \
  native-client-real-browser \
  suitecut-native-cross_browser_features \
  suitecut-native-cef_artifact_workflows \
  suitecut-native-cef_restricted_bootstrap \
  suitecut-native-cef_media_workflows
python3 tests/cef --list
python3 tests/cef \
  --engine /absolute/path/to/suitecut-browser \
  --client-build .suitecut/cef-regression-client \
  --ffmpeg /absolute/path/to/ffmpeg \
  --ffprobe /absolute/path/to/ffprobe \
  --workers 2 --seed 23
```

The `client-cef-binaries` target builds the same five binaries. `--engine` and
`--client-build` are required for every run, and a selected case whose binary
is missing fails before anything starts.

The media tools default to executables on `PATH`. Missing required binaries
fail preflight rather than skipping their tests. They must support VP9, Opus,
and the codecs used by the selected native runtime. Prefer the tools shipped
in the runtime being qualified. Source/build identities are recorded; this is
not a substitute for building the chosen binaries from the intended source.

## Cases

| Case                       | Binary                                    | What it checks                                                                 |
| -------------------------- | ----------------------------------------- | ------------------------------------------------------------------------------ |
| `cpp.restricted-bootstrap` | `suitecut-native-cef_restricted_bootstrap` | Strict restricted-frame bootstrap                                              |
| `cpp.facade-lifecycle`     | `native-client-real-browser`              | 50 context/page lifecycle cycles through the C++ facade, with no pending or dropped transport work |
| `cpp.features-network`     | `suitecut-native-cross_browser_features`  | The HTTP feature workflow: downloads, emulation, HAR, network and storage      |
| `cpp.artifact-workflows`   | `suitecut-native-cef_artifact_workflows`  | Screenshot, PDF, frame and popup artifacts                                     |
| `cpp.decoded-media`        | `suitecut-native-cef_media_workflows`     | Media playback and recording, decoded with FFmpeg and FFprobe                  |

Each case declares its report file, success predicates and minimum assertion
inventory in the manifest; see [the evidence contract](../tests/cef/CONTRIBUTING.md#evidence-contract).

## Selection, repetition and future CI usage

`--profile full` (the default) selects every case. `--profile smoke` and
`--profile races` select cases tagged `smoke` or `race`; run `--list` with a
profile to see what it selects, since an empty selection is an error. Explicit
`--case` selects from the whole catalogue, overriding the profile:

```sh
python3 tests/cef \
  --engine /absolute/path/to/suitecut-browser \
  --client-build .suitecut/cef-regression-client \
  --case 'cpp.facade-*' --case 'cpp.restricted-*' \
  --seed 100 --repeat 10 --workers 2
```

Each repetition gets a fresh process/profile and a separate result. The seed
controls the reproducible scheduling order. It is not a deterministic Chromium
thread scheduler or exhaustive concurrency fuzzer.

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

| Component                                            | Responsibility                                                           |
| ---------------------------------------------------- | ------------------------------------------------------------------------ |
| `tests/cef/harness/catalogue.py`                     | Explicit inventory, schema checks and strict selection                   |
| `tests/cef/harness/contracts.py` and `evidence.py`   | Typed report contracts, assertion coverage and input inventories         |
| `tests/cef/harness/runner.py`                        | Bounded scheduling, immutable plan and attempt identity accounting       |
| `tests/cef/harness/supervisor.py` and `processes.py` | Deadlines, log limits and verified owned-process retirement              |
| `tests/cef/harness/native_media.py`                  | Runs the media workflow and decodes its recordings                       |
| `tests/cef/harness/reporting.py`                     | JSON/HTML/JUnit, including global integrity failures                     |
| `tests/cef/manifests/cpp.json`                       | C++ cases, resource budgets and mandatory evidence contracts             |
| `tests/cef/unit/`                                    | Supervisor, process ownership, signing and false-green negative controls |
| `native/client/tests/{unit,integration,support}/`    | Explicit standalone C++ targets and test-only fixtures                   |

See [the package contribution rules](../tests/cef/CONTRIBUTING.md).

The outer supervisor fails a leaked process even when it successfully cleans it
up. It signals its newly created process group and verified owned descendants,
including browsers placed in separate groups by the C++ SDK. PID and start
identity are rechecked before signaling descendants. It does not kill browsers
by application name or touch the user's profiles.

Working directories are private per attempt, so their `.suitecut` paths do not
overwrite other attempts or prior release evidence. The launcher records the
exact engine, renderer helper, test inputs and C++ binary hashes before and
after the run. Input changes make the whole run fail, including JUnit, even if
individual cases passed.

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

Each case declares the exact report path, terminal success conditions and minimum
assertion inventory. Unrelated JSON cannot make a case pass. Reports are parsed
without duplicate keys or non-finite values; booleans cannot be replaced with strings
or integers. Checks remain enabled under Python `-O`.

Every `--case` selector must match. `plan.json` records intended attempt identities;
missing, duplicate or unexpected attempts fail final qualification. Nested source
inventories are pinned before/after; adding or removing an input also fails. The
full result remains red until every planned attempt and suite integrity check pass.
Static media artifacts are required and decoded; absent recordings are failures.

## Adding a regression

1. Add the C++ workflow under `native/client/tests/integration/`, give its case a
   stable ID in `manifests/cpp.json`, and map it to the implementation family and
   actual incident.
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

This is finite regression coverage. It cannot prove all websites, all race
interleavings, every device or the absence of all leaks. It is not the entire
Web Platform Tests conformance suite. The catalogue does not treat unsupported
native capabilities as passing implementations: directory upload, full clock
control, WebSocket routing, some cross-navigation coverage and offscreen OOPIF
capture are still separate capability limits.

The media workflow decodes short recordings and compares counts and audio. It
does **not** replace the sustained 1080p120 unique-frame/A-V continuity
qualification in `scripts/verify-native-macos-recording.py`, long-duration soak
tests, sanitizer runs, codec licensing checks, or release packaging/signing
qualification. Turnstile and other public bot checks should remain explicit
opt-in manual integration checks, not an Internet-dependent CI gate.
