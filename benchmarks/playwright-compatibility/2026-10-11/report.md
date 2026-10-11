# SuiteCut / Playwright compatibility — October 11, 2026

## Conclusion

Several previously unverified workflows now have real, reproducible passing
tests. This investigation also found and fixed adapter defects and configuration mismatches, while retaining confirmed compatibility gaps.
SuiteCut cannot claim complete Playwright compatibility, complete authentication
state portability, or unrestricted service-worker networking support.

The final focused results and additional upstream verification are recorded in
the results table below. Every failure remains visible; nothing was relabeled as
an expected failure to improve the result.

## Tested support boundaries

| Area | Verified behavior | Remaining boundary |
| --- | --- | --- |
| Service-worker automation | Registration/control/fetch; Playwright enumeration, evaluation, console, close, first-page closure; main-script/internal-request routing, importScripts, worker HAR and advanced request attribution | Upstream worker-update interception is skipped by upstream; independent browser launches/persistent contexts are outside the harness. No universal claim across all worker/platform combinations. |
| Worker lifecycle | 80 successive worker contexts create/evaluate/dispose and return target registrations to zero; 80 native raw-CDP alias cycles detach correctly | Capacity recovery is verified, not whole-process leak freedom or full CEF sanitizer coverage. |
| Cross-origin process iframes | Separate target, evaluation/input, routing, navigation/detach, offset click after reload, mobile context options and reference screenshot | Independent headed-browser and CDP-reconnection cases are outside this native transport harness. |
| IndexedDB/authentication state | All 18 upstream storage-state cases pass, including file/inline state, IndexedDB, third-party localStorage, lone surrogates and virtual WebAuthn state. Focused tests verify indexes, key paths, dates, binary tokens and fresh-context isolation | Automation follows Playwright origin-based storage semantics with third-party partitioning disabled. Partitioned cookies, persistent profiles and state exceeding the 4 MiB command / 16 MiB event limits remain outside the claim. |
| Proxy/HTTP auth | All 10 upstream credential cases pass, including anonymous HTTP 401, origin matching and credential updates. HTTP/HTTPS, CONNECT, proxy bypass and proxy/origin auth individually pass | Distinct authenticated-proxy and origin credentials fail with HTTP 401 in both CEF and stock Chromium. Playwright replaces origin credentials with proxy credentials before they reach our CDP transport. SOCKS/provider matrices were not tested. |
| HAR | All 33 upstream HAR cases pass, including redirected back/forward history; focused JSON and ZIP capture/replay, POST/body and missing-request handling pass | Complete HAR behavior outside this pinned Chromium subset is not certified. |
| Playwright recordVideo | Real VP8 WebM decode, odd/mobile/1×1 browser views, static colour, save/delete and scaling checks pass in dedicated runs; repeated popup/default-size lifecycle tests are saved | The final video results below specify remaining timeouts and launcher exclusions. Playwright video is video-only; native synchronized audio/video remains a separate API. |
| Full upstream suite | Original pinned Chromium library/page test bodies run through actual SuiteCut fixtures; targeted groups and bounded broad attempts preserved | The full suite was not completed. No compatibility percentage or all-platform/all-browser conformance claim. |

## Native fixes made and verified

1. Promote service-worker targets to the browser session with normalized context
   IDs and stable engine-agent ownership, so closing the original page does not
   orphan worker automation. Deduplicate attachments and clear target/alias state
   on context shutdown. Target discovery includes bounded nested targets.
2. Defer attachment/cleanup commands out of CEF's DevTools observer callback.
   The intermediate reentrant implementation crashed; its failure and the
   corrected verification remain saved.
3. Keep the original worker debugger paused until Playwright enables Network and
   Fetch on the stable session. Premature resume lost main-script requests and
   interception/HAR events. Advanced upstream worker cases now pass.
4. Investigate duplicate page debugger attachment as a possible popup stall
   cause. That trial did not eliminate video timeouts and added an iframe input
   failure and a nested-worker header timeout. It was **reverted**. Trial logs and
   source-package evidence remain saved; its results are not the retained build.
5. Align automation configuration with Playwright: disable back/forward cache,
   third-party storage partitioning, paint holding and relevant Chromium input/
   redirect feature changes; allow pre-commit input. Third-party state, HAR
   history and offset iframe clicks subsequently pass. Do not infer ordinary
   Chrome privacy/default-browser semantics from these automation settings.
6. Preserve opaque argument/result JSON containing lone UTF-16 surrogates while
   using a masked copy solely for routing metadata. Test values survive arguments,
   console events, storage export and fresh-context import; literal backslash
   escapes and valid surrogate pairs remain intact.
7. Keep anonymous auth challenges under native CDP control and explicitly cancel
   unavailable windowless prompts, preserving the real HTTP 401. Auto-continue
   ordinary requests without exposing synthetic routing events. Client Fetch
   configuration takes precedence; tracked domain state is limited to registered
   agents and cleared on detach. No response status is fabricated.
8. Accept ordinary odd-sized and 1×1 browser surfaces within the physical limit;
   preserve even/fixed raw-I420 and encoder contracts.
9. Maintain windowless visibility/repaint during active DevTools screencasts with
   at most one pending pulse per source. Restore an opener after popup closure.
   Native synchronized recording remains its existing separate pipeline.
10. Emit each JS dialog through the owning CEF callback once, including explicit
    page aliases. Dropping the duplicate Chromium opening event fixes a reproduced
    double-dismissal error; three popup/dialog/upload/download repeats pass.
11. Include the new JSON helper, regression test and optional fuzz target in the
    native/CEF source packagers so packaged source remains buildable.

`native-compatibility.patch`, `source-before/` and `tested-sources/` preserve the
change and final source. Existing unrelated workspace work and previously sealed
release archives were preserved; those older archives do not contain these fixes.

## Results

| Run | Result | Interpretation |
| --- | --- | --- |
| Retained CEF focused matrix (`validated-cef-final`) | 26 passed, 2 failed | Both failures are distinct proxy/origin credentials, also failing in stock Chromium. Includes 80 worker-context and 80 native-alias lifecycle cycles. |
| Stock Chromium focused control (`release-verified-chromium`) | 25 passed, 2 failed, 1 skipped | Same 28 test bodies; native-only alias test skipped. |
| Retained upstream API group (`upstream-api-final`) | 155 passed, 5 failed, 5 skipped | All five failures are independent launcher/server/reconnection tests outside the harness, including one proxy teardown that masks the rejection. No API-group timeouts. |
| Retained upstream video group (`upstream-video-final`) | 20 passed, 7 failed, 2 timed out | Seven independent-launch/persistent-profile/crash cases outside the harness. Real timeouts: deletion and blank-popup video path. 10-second base timeout, upstream slow multiplier yields 30 seconds. |
| Retained native regression (`validated-native-final`) | 16 passed | Actual native recording/streaming/readiness, input, dialogs, uploads/downloads, shutdown and session capacity. |
| C++ CTest (`native-ctest-final.log`) | 9 passed | Includes new JSON transport regression plus existing native media/clock/pixel tests. |
| Repeated popup/default-size lifecycle (`video-lifecycle-visibility`) | 9 passed | Three cases repeated three times; does not erase failures in the whole video run. |
| Repeated popup/dialog/upload/download (`dialog-repeated`) | 3 passed | Duplicate-dialog fix verification. |
| JSON helper sanitizer fuzz (`cdp-json-sanitizer-fuzz`) | 366,054 executions, 31 seconds, zero crashes/errors | Clang 19 libFuzzer + ASAN/UBSAN. JSON helper only; not whole CEF. |

The retained executable SHA-256 is
`26029b3d5623c07c51e451ec6cc26796ce1f3405bc689497b3b1c72ba91eb0f8`.
After reverting the page-debugger trial, both its source and rebuilt executable
match the above retained-run provenance **exactly**. No rerun or favorable result
was substituted for a changed executable. Discarded trials remain in the summary.

Non-video per-file results: credentials 10/10; HAR 33/33; storage state 18/18;
service-worker policy 3/3; proxy 25 passed + 1 launcher failure; Chromium worker
checks 21 passed + 2 launcher failures + 3 upstream skips; OOPIF 22 passed + 2
launcher/reconnection failures + 1 skip; dedicated workers 23 passed + 1 skip.

**Not fixed:** Playwright `recordVideo` can still stall during deletion or blank
popup initialization. These timeouts reproduce in multiple attempts, despite
passing short/repeated cases. Windowless repaint, visibility and dialog fixes
improve observed cases but do not establish full recording lifecycle reliability.
Use the native synchronized media API for its separately verified workflows;
this investigation does not certify every native workload either.


The earlier targeted upstream run (`upstream-final`) used 194 original test cases
covering credentials, proxies, service-worker policy, storage state, Chromium
workers, OOPIFs, HAR and video: **151 passed, 26 failed, 12 timed out, 5 skipped**.
At least 11 failures explicitly reject independent browser launching/connecting
outside this harness; one proxy launcher test masks that rejection in teardown.
One screenshot failure used an incorrect snapshot path, subsequently corrected.
Three viewport failures preceded the odd-size fix. The remaining failures and
timeouts are kept as evidence, not subtracted to manufacture a pass rate.

`upstream-broad` scheduled 4,730 Chromium library/page cases but was stopped when
browser-server/launcher fixture behavior blocked the private transport harness.
Its partial log and interruption record are preserved. The follow-up
`upstream-core` scheduled 4,220 cases, excluded selected launcher/server-related
files and ended at its eight-minute global timeout: 259 passed, 27 failed,
11 timed out, 2 skipped/interrupted and 3,921 without a result. Some passes are
API-only/Node-side checks rather than CEF execution. Two global/teardown timeout
errors are saved. Exclusions and configuration remain available. Not-run/interrupted cases do
not count as passes. These scopes omit other upstream projects, browser engines,
platforms and Playwright's own test-runner tests.

`summary.json` and `cases.csv` preserve results from every attempt, including
baseline defects, control runs, failed diagnostics and corrected verification.
Do not add their counts together: many attempts repeat the same test.

## Control interpretation

The focused native and stock tests use identical local website, HTTPS and proxy
fixtures and positive success assertions. Both independently fail the two dual
credential combinations. Playwright 1.62.1's
`BrowserContext.authenticateProxyViaCredentials` assigns proxy credentials to
the context's `httpCredentials`, replacing distinct origin credentials. This is
consistent with the observed shared HTTP 401 failures. It does not prove that
Chromium cannot authenticate both at a lower level, or that another Playwright
version has the same behavior.

The native-only alias cleanup test is separate from the common worker lifecycle
test. Stock Chromium does not deliver the private shim's alias-detach event on
that public raw session. An earlier combined test therefore had an invalid
stock-control assumption; that failure is preserved and the common behavior is
now tested equally on both browsers.

## Environment and method

- SuiteCut SDK 2.1.0; Playwright/Playwright Core 1.62.1.
- Codec-enabled Linux x64 CEF
  `152.0.6+g708dc14+chromium-152.0.7977.83`; bundled stock Chromium
  `151.0.7922.34` (revision 1234). These are different Chromium revisions.
- Official upstream source tag `v1.62.1`, archive SHA-256
  `f6b38805b0b021423bb908cd7682a455283c22742bf7d95c190927f3fb11172c`.
- One test worker, zero retries. Focused timeout 20 seconds (lifecycle stress
  cases explicitly allow 60); early upstream attempts use a 30-second base; the retained upstream groups
  use a 10-second base, with original slow-test multipliers (30 seconds for
  slow video cases). CPU affinity 0–3, quota 4 CPUs, actual host memory cap 16 GiB.
  This is functional verification, **not** a rerun of the published 4 CPU / 8 GiB
  performance benchmark.
- Owned Xvfb/PulseAudio dependencies; no hardware GPU. Local HTTP/HTTPS, proxy
  and process-frame fixtures use real loopback connections. The report does not
  infer remote WebRTC, DRM or remote proxy support from those tests.
- Original upstream test bodies/assets are retained. Only the browser fixtures
  are adapted to `launchEngine`; launching separate browsers is explicitly
  rejected in native mode. Collection/configuration files and fixture patch are
  saved for reproduction.

Run instructions are in [the harness README](../README.md). Provenance is in
`metadata.json` and the manifest. The downloadable helper requires the matching
complete SuiteCut 2.1.0 codec-enabled native runtime; it is not a standalone CEF
distribution. No package, tag, release or website was published by these tests.

## Memory and recording limitations

These checks do not establish leak freedom, full Chromium/CEF memory safety,
long-running audio continuity or hardware encoding support. An attempted
partially sanitized CEF diagnostic helper failed during startup and did not
provide successful full-browser ASAN/LSAN coverage. Separately, the new JSON
transport helper passed its sanitizer regression and 366,054 libFuzzer executions
under ASAN/UBSAN without reported crashes or leaks; this narrowly scopes the
successful sanitizer evidence. All diagnostic logs remain available. Previous media and
memory investigations remain separate evidence; these browser adapter fixes do
not resolve their outstanding audio continuity issues.

## Unresolved / not certified

- **Dual independent credentials:** the standard Playwright `proxy.username` /
  `proxy.password` plus distinct `httpCredentials` combination still fails in
  both browsers. A fix requires preserving both credential sets in upstream
  Playwright and selecting them by challenge source, or an explicit scoped
  origin-auth workaround. The C++ transport cannot recover credentials already
  overwritten upstream; the two positive assertions remain failing.
- **Video lifecycle:** deletion and blank-popup video-path initialization time
  out in `upstream-video-final`. Passing short or repeated checks does not erase
  these failures in the whole video group.
- **Independent launch/server/persistent-profile flows:** the fixture rejects
  these explicitly. SuiteCut's own native launcher and external-browser connector
  are separate APIs, not a complete replacement for Chromium BrowserType launch
  options, browser servers or user profiles.
- **Full upstream coverage:** the 4,730-case and filtered 4,220-case scopes were
  bounded/partial. Broader cookie, beforeunload, popup-event, exposed-function and
  locale failures are preserved in `upstream-core`; they were not all reverified
  after the later fixes. Partitioned-cookie management and independent CDP domain
  state remain unsupported. No full-conformance claim is appropriate.
- **Large state / dependencies / memory:** protocol capacity limits apply.
  Whole CEF/Chromium sanitizer coverage, leak-free long runs, GPU encoding and
  the existing audio-continuity investigation remain separate unresolved work.

A native fix is only labeled verified when its case actually passes; skips,
harness rejections, incomplete runs and dependency limitations remain explicit.
