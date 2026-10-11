# SuiteCut 2.1 compatibility fixes — follow-up, October 11, 2026

## Outcome and fallback

The preceding implementation was committed and pushed before any new code changes:
[`80759aeb30cc4f6bd4ab488554569c62851985d2`](https://github.com/skyaara/suitecut/commit/80759aeb30cc4f6bd4ab488554569c62851985d2), branch
`suitecut-2.1-fallback-20261011`. It includes accumulated source, documentation,
benchmark harnesses, structured results and logs. Generated recordings, browser
profiles and large binary analysis outputs remain saved locally rather than being
pushed to GitHub. The fallback was confirmed with `git ls-remote`.

New work is on `suitecut-2.1-compatibility-fixes`. Three concrete defects were
corrected: synchronous CDP packet delivery, overwritten authentication credentials,
and incorrect host-cookie scope. The recheck also establishes passing behavior for
12 of 20 older failing cases. Eight specific cases still fail; they are listed below.
This is a tested capability boundary, not a complete Playwright conformance claim.

## Fixes

### CDP delivery and Playwright recording

The native packet decoder previously invoked Playwright for every packet in a
received batch synchronously. Playwright's own pipe/WebSocket transports yield
between packets. A frame-tree response resolves a promise whose continuation
registers frames; an execution-context event delivered in the same synchronous
batch can arrive before that continuation and be discarded. Subsequent page
JavaScript waits for a context that was already announced.

The diagnostic runs reproduced stalled evaluation while CEF had already produced
and acknowledged a video frame. In particular, the “delete video” case could
stall before it reached deletion. This did not indicate a muxer waiting to delete
a file. A focused regression reproduces the promise/event ordering independently
of browser timing.

`src/cdp-delivery.ts` now schedules each packet as its own Node task, retains
FIFO order and permits promise continuations between packets. It bounds pending
delivery to 1,024 messages / 64 MiB of **wire payload**, clears pending callbacks
and references on close/overflow, and drains final packets before a process
close notification. Native packets provide their existing byte length, avoiding
an extra JSON serialization for real traffic. Unit checks cover ordering, burst
handling, process closure, explicit cancellation, byte/message overflow and
throwing consumers. The media engine remains in C++; this scheduling belongs to
the thin automation adapter and must also be respected by other SDKs.

Native `SUITECUT_CDP_TRACE=1` enables capped metadata diagnostics on stderr.
Credentials, URLs, JavaScript arguments, bodies and encoded frames are omitted.
Normal operation leaves this tracing disabled. A speculative initial-frame wait
was removed; the retained implementation fixes delivery order instead.

### Separate proxy and website authentication

Playwright 1.62.1 replaced `httpCredentials` with proxy credentials during context
initialization and used one retry marker for all challenges on a request. Both
CEF and stock Chromium therefore failed distinct-credential combinations.

The version-specific `patches/playwright-core@1.62.1.patch` preserves both credential
sets, selects them using the CDP challenge source, applies website-origin
restrictions only to website credentials, and tracks retries per challenge.
Tracking is bounded and normal request completion releases entries. Both page
and service-worker managers receive the separate configuration.

HTTP and HTTPS success assertions now pass. Negative checks verify that proxy
credentials never reach the website, out-of-origin credentials are withheld,
incorrect credentials do not contact the website, and a fresh context does not
inherit credentials. A wrong-password response may commit HTTP 407 rather than
reject navigation, so the denial check explicitly waits for commit. The earlier
assertion failure and a separate stock-Chromium **default-load** timeout remain
saved. That timeout occurs after a real 407 and `ERR_TUNNEL_CONNECTION_FAILED`;
commit-level denial does not certify default-load completion in that case.

A fresh, separate Playwright installation applied the pnpm patch and produced the
same driver SHA-256 as the tested workspace. The initial offline installation
failed because its tarball was not cached; the subsequent online installation
succeeded. Importing SuiteCut does not patch another project's dependencies.
Downstream projects must explicitly configure the patch, pin 1.62.1 and rebuild
any remote Playwright driver that needs the fix. [Setup](../../../patches/README.md).
The shipped SDK includes the patch and its upstream Apache-2.0 license.

### Cookie semantics

Passing a nonempty domain to CEF always creates a domain cookie. CDP's plain
hostname or URL instead represents a host-only cookie. Previously, exports gained
a leading dot and cookies could apply to unintended subdomains. The bridge now
passes an empty CEF domain for host cookies and retains leading-dot domain cookies.
This fixes actual cookie scope, not just exported formatting.

Chromium also discards insecure `SameSite=None` cookies while succeeding the batch.
The native bridge now omits those invalid entries without failing unrelated valid
cookies. Partitioned-cookie management remains explicitly unsupported.

## Verification

| Saved run | Result | Boundary |
| --- | --- | --- |
| `focused-cef-final` | **36 passed** | Actual CEF, worker/context and alias capacity cycles, state, authentication, HAR and decoded WebM. |
| `focused-chromium-final` | **35 passed, 1 skipped** | Same 36 test bodies and identical local fixtures; native alias test is skipped. Uses the same explicitly patched Playwright driver. |
| `upstream-api` | **155 passed, 5 failed, 5 skipped** | Five independent launcher/server/reconnection cases outside the harness, including a masked proxy teardown failure; no API timeouts. |
| `cookies-cef` | **37 passed, 1 failed, 1 skipped** | Only failure is independent browser launching; the skip is upstream. Original upstream test bodies unchanged. |
| `video-fixed` | **22 passed, 7 failed** | All seven failures are independent launch/persistent-profile/crash cases outside the harness. No video timeout. |
| `video-repeated` | **20 passed** | Deletion and blank-popup video initialization, each repeated ten times with fresh workers. |
| `video-final-binary` | **6 passed** | Lifecycle repeats on the retained cookie-corrected native executable. |
| `previous-failures` | **12 passed, 5 timed out, 3 failed** | Selected 20 older failures, including the remaining eight cases below. |
| `native-final` | **16 passed** | Native recording/streaming/readiness, pause/page selection, input/dialogs, uploads/downloads, capacity recovery and shutdown. |
| `adapter-unit-tests.log` | **36 passed** | Six new delivery regressions and 30 existing native-browser/external-connector unit checks. |
| `native-ctest.log` | **9 passed** | Nine native C++ media/clock/pixel/JSON checks. |

The SDK build, focused ESLint and full TypeScript checks pass. Intermediate lint
and typecheck failures from a removed fixture import are preserved; the final
`*-delivery-final.log` files record corrected checks.

The native cookie change followed the whole-video and ten-repeat runs; it does
not change the video path. `video-final-binary` verifies lifecycle on the retained
executable, while `native-final`, cookies and the API group also use that helper.
`metadata.json`, `tested-sources/`, source package manifests and package checks
record exact code and binary hashes. `video-api-trace` was a diagnostic run while
the helper was rebuilt and is not used as final-build provenance. Every failed
trial remains visible in `summary.json` and `cases.csv`.

Do not add counts across attempts: they contain overlapping and repeated tests.
No failure was marked expected to improve a score. Skips, harness exclusions,
timeouts and incomplete broader coverage remain explicit.

## Eight confirmed remaining cases

| Area | Upstream case | Status |
| --- | --- | --- |
| browsercontext-events.spec.ts:55 | console event should work in popup 2 | timedOut |
| browsercontext-events.spec.ts:103 | dialog event should work in popup | timedOut |
| browsercontext-events.spec.ts:120 | dialog event should work in popup 2 | timedOut |
| browsercontext-events.spec.ts:135 | dialog event should work in immediately closed popup | timedOut |
| browsercontext-events.spec.ts:154 | dialog event should work with inline script tag | timedOut |
| browsercontext-fetch.spec.ts:693 | should add default headers | failed |
| browsercontext-fetch.spec.ts:718 | should add default headers to redirects | failed |
| browsercontext-locale.spec.ts:89 | should affect navigator.language in popups | failed |

The five event cases involve javascript-URL console delivery and dialogs opened
before normal popup initialization completes, including immediately closed and
inline-script popups. Ordinary popup automation and dialogs after initialization
pass. The exact remaining event loss/association mechanism was not fully isolated
in this pass; it requires further tracing and a verified native lifecycle fix.

The locale case observes `en-US` in the popup's initial script rather than the
parent context's `fr-FR`. Locale must be applied before that script runs. The two
request-header cases compare the default `context.request` user-agent to the page:
the shim's simplified headless user-agent differs from the real CEF page agent.
Simply changing that version response also affects Playwright's window/headful
inference, so this needs a coherent windowless-driver fix rather than a fabricated
header expectation. These three assertions remain failing.

The twelve recovered cases include seven before-unload cases, popup init-script
and exposed-function cases, and host-cookie filtering. The full upstream suite
was not rerun to completion. Independent browser launch/server/persistent profiles,
partitioned cookies and independent CDP domain state remain outside the supported
claim. `unresolved-issues.json` preserves the exact failures and additional limits.

## Environment, reproduction and delivery

SuiteCut 2.1.0; explicitly patched Playwright 1.62.1; codec-enabled Linux x64 CEF
152.0.6+g708dc14+chromium-152.0.7977.83; stock Chromium 151.0.7922.34. These are
different Chromium revisions. Tests use one worker, zero retries, CPU affinity
0–3 and a 4-CPU quota; the actual host memory cap is 16 GiB. There is no hardware
GPU. This is functional verification, **not** a rerun of the 4 CPU / 8 GB published
performance comparison. Local HTTP/HTTPS and proxy fixtures are real connections.
The portable TLS fixture is the pinned upstream dummy key/certificate.

Use [the harness README](../README.md) and the pinned upstream preparation script.
Selections and original test bodies are retained. Recording remains Playwright's
video-only WebM API; SuiteCut's synchronized native audio/video API is separate.

Prepared artifacts are under
`/workspace/shared/suitecut-playwright-fixes-2026-10-11/`: the SDK tarball, retained
helper, native source archive, evidence archive, checksums and setup README.
The helper requires the matching full codec-enabled 2.1.0 runtime resources and
libraries; it is not a standalone CEF distribution. Earlier sealed releases and
the fallback artifacts were preserved. Nothing was published to npm or tagged.

## Memory and media limits

Queue bounds and lifecycle regressions passed. This does not establish whole-CEF
leak freedom or sanitizer coverage. The preceding JSON-helper ASAN/UBSAN fuzz
result remains separate evidence; that helper was unchanged in this pass. No new
30-minute audio-continuity or hardware-encoding certification is implied. Those
previously documented limitations remain open.
