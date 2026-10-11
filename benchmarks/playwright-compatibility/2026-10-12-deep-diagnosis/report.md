# SuiteCut 2.1: deep failing-case diagnosis

Prepared October 12, 2026 (Asia/Kolkata). The user-requested checkpoint was pushed **before** changes: branch `suitecut-2.1-deep-audit-checkpoint-20261012`, commit `a0af11ef78cdf94f93b5a4da10c44b13cfb6114a`. The older fallback at `80759aeb30cc4f6bd4ab488554569c62851985d2` remains separate.

## Outcome and scope

Reviewed **all 431 unexpected cases plus seven serial-blocked heap cases**, **438 unique cases**, from the [4,945-case pinned upstream audit](../2026-10-12-upstream-audit/report.md). The ledger preserves each original native/stock outcome, error, source location, targeted observations, diagnosis, confidence, decision and next step:

- [Readable case decisions](case-decisions.md)
- [CSV ledger](cases.csv)
- [JSON ledger with individual source excerpts and all targeted observations](cases.json)
- [Counts and individual run summaries](summary.json)

**15 cases are resolved by native/adapter/configuration fixes**: 14 of the original 62 native-path mismatches and the separately rerun 2,000-handle heap case. **Three more original failures are resolved as harness problems**: two inherited-proxy certificate controls and a missing generated browser-client bundle. Three previously serial-blocked heap cases pass independently without a new fix. This is a case count, not a count of distinct defects.

**48 of the original 62 native-path mismatches remain unresolved**, grouped below. A separate shared trace-viewer stdin failure and stock remote-connect timeout remain open. Repetition also exposed an additional intermittent cross-site iframe click that was absent from the original failing inventory; it is documented separately.

The full 4,945-case audit was not rerun after these changes. Do not subtract fixes from old totals to manufacture a new pass percentage. The 343 launcher/connection-blocked tests have not established their subsequent feature assertions. This work does not certify full Playwright compatibility or produce rebuilt/published release archives.

## Practical fixes

| Failing condition | Change | Verification |
| --- | --- | --- |
| An automation page crash killed the whole helper and lost its crash event | Emit one page crash, fail its outputs/pending commands, retain healthy siblings; primary control-source crashes remain fatal | All seven active upstream crash cases; three repetitions in the confirmation batch. Native engine test records a healthy sibling after the crash |
| Windowless Ctrl/Shift navigation had no native tab owner | Create an independent tab in the same CEF request context; wait for Playwright initialization before loading its destination; retain pending creations through context/runtime shutdown | Both upstream modifier cases repeated; codegen case 339 passes 5/5. Native routing/teardown regression performs 12 iterations |
| A popup download arrived before Playwright knew its frame | Track the main frame ID and attribute an initial download to its live opener | Old helper: 9 failures in 20 ordinary upstream repetitions. A deliberate 300 ms frame-tree delay fails on old helper and passes 5/5 on new helper; committed engine regression retains that fault injection |
| 2,000 matched elements exceeded the SDK command count | Increase command admission from 1,024 to 8,192; retain 16 concurrent submissions and the 64 MiB byte cap | Upstream heap case 304 passes; standalone CEF already handled 2,000 elements. Unit burst checks 4,000 commands; overflow checks still enforce configured bounds |
| JSON reserialization sorted user object/DOM attribute properties | Forward original CDP JSON, rewriting bridge IDs/session metadata only | Original meta trace-viewer case passes; engine test checks nested objects and console argument insertion order |
| Upstream internal tracing lacked its worker artifact directory | Expose `artifactsDir` on `launchEngine`/`connectCEF` and supply the worker path in the audited fixture | Unmodified tracing case 716 and trace-viewer case pass. This was artifact configuration, not absent tracing support |
| OOPIF detach replied before Playwright retired its child session | CEF-only driver patch normalizes the precise missing-child-session error; main-frame/other errors retain failure behavior | Before: 1 failure in 20. After: 30/30, plus confirmation repetitions |
| CEF retained intercepted body headers after POST→GET redirect | CEF-only driver patch drops body headers on method-changing GET/HEAD redirects | Independent raw-CDP CEF and stock controls; all 39 request-continuation tests pass, including same-method/header and body-preserving redirects |

The explicit [Playwright 1.62.1 patch](../../../patches/README.md) is required for its driver workarounds. Importing SuiteCut never edits a consumer's Playwright installation. A separate clean pnpm consumer applied the final patch with a frozen lockfile; its active `coreBundle.js` exactly matches the tested workspace SHA-256 `0481bf91a6241245e42de427be147d01d99c45c934d657ea580c8fd0c95413d5`.

## Remaining native-path mismatches

| Cases | Issue and established layer | Decision |
| ---: | --- | --- |
| 20 | Initial blank-origin `srcdoc` frame crash family. Minimal standalone CEF reproduces the origin-calculation renderer crash in both windowless and windowed modes. Individual cases share the trigger; not all have separately captured stacks | Runtime-level fix/new CEF build required. Do not silently navigate blank documents to a different origin or replace their iframe content |
| 1 | Intercepted iframe-abort race. Direct page CDP writes HTTP iframes on an initial blank page, then changes their URL to `about:blank`; standalone CEF reports renderer status 2 / exit 5 without SuiteCut or Playwright | Fix/reduce the CEF runtime crash before asserting `requestfailed` event semantics |
| 23 | Early popup first-script/request, event, routing, binding, clock and emulation races. Some reruns pass intermittently. SuiteCut currently attaches the page agent after activity can begin | **SuiteCut–CEF architecture gap**, not proof of an inherent CEF impossibility. Establish an earlier paused-target/native attachment hook; consider a narrow CEF integration change if public APIs cannot provide it |
| 2 | Storage-access and screen-wake-lock overrides. CEF has related APIs, but simple content-setting mappings do not provide validated context-scoped query/request/reset behavior | Permission-controller integration required; failed experiments were reverted. Do not fake `permissions.query()` or grant the wrong global profile |
| 1 | A never-resolving evaluated promise remains pending after 20 GC requests in standalone CEF; stock Chromium 151 emits the collection error | CEF/V8 DevTools behavior difference established; exact CEF vs Chromium-version attribution remains open. Do not invent a GC error; investigate promise ownership with matching Chromium 152 |
| 1 | `webkitdirectory` input completion times out in standalone CEF while stock returns real files/relative paths | Investigate Chromium directory selection/confirmation and CEF dialog completion. Ordinary upload remains verified; a fake `FileList` would not establish directory support |

The standalone CEF control is a small independent embedder linked only to CEF/wrapper/X11, with no SuiteCut bridge or media engine. Promise/folder probes use Playwright connected directly to that browser's own CDP endpoint; redirect and iframe-abort controls use raw page WebSocket CDP without Playwright. The control's renderer-termination callback is the crash evidence—its process exit code alone does not indicate renderer health. Matching helper/control sources, commands and logs are archived.

CEF is **152.0.6+g708dc14+chromium-152.0.7977.83**; the stock Playwright headless shell is **151.0.7922.34**. That version/runtime difference prevents claiming every observed difference is a CEF-specific regression. Standalone CEF popup/root-routing attempts timed out and are **inconclusive**, not proof that CEF cannot support early popup automation.

## Harness, policy and product differences

- **343 independent launcher/connection blocks:** the native fixture deliberately rejects `browserType.launch`, persistent launch, server launch, `connect` and `connectOverCDP`. Some tests catch that rejection and later time out. The individual ledger retains each test/source/API excerpt. Adding a complete launcher, Chrome command-line product surface or multi-client remote server purely to turn these green would expand the package's product scope. Public external `connectBrowser()` support is a separate API, not a substitute for these fixtures.
- **One remote reconnect case:** native depends on the same denied remote-server workflow; stock also timed out. Its stock-side cause remains unresolved.
- **Ten bootstrap/context/dispatcher/heap assumptions:** CDP attaches to an existing primary page/default context, so strict empty-browser or object-tree assertions differ. Static extra objects are not measured growing leaks. The engine's public primary-page behavior is retained.
- **One fixture identity watchdog:** `browser.browserType()` returns the real Chromium object while the fixture holds a Proxy. Assertion inspection/worker watchdog does not establish a CEF memory leak. The SDK does not change browser identity to satisfy the wrapper.
- **Five unauthorized-navigation expectations:** native returns actual HTTP 401 responses with prompts denied; upstream full-Chrome assertions expect a navigation error. Successful credentials and rejected credentials retain functional coverage.
- **Four intentional resource-limit cases:** 16 MiB encoded CDP replies and 8,294,400 screenshot pixels / 16,384-axis limits remain enforced. A 100 MiB evaluate result and excessive screenshots are outside this policy.
- **Three default graphics cases:** the CPU-only host does not supply expected default WebGL rendering. Explicit SwiftShader has separate real pixel tests and remains opt-in; unsafe software rendering is not silently enabled.
- **One autoplay expectation:** browser-source automatic playback deliberately runs AudioContext where upstream Chrome policy expects suspension.
- **Two client-certificate cases:** both unmodified bodies pass on both backends when inherited proxy variables are removed only for these local controls. One needs its fixture proxy rather than inherited loopback bypass; the HTTP/2 alias must not be routed through the cloud proxy. Production network settings are not changed.
- **One in-page Playwright client case:** build the missing official browser-targeted ESM bundle. The unmodified case then passes on native and stock. A concurrent fixed-port collision attempt is retained but excluded from the successful control claim.
- **One shared trace-viewer stdin case:** independent native and stock reruns show no action titles after sending a trace path. The exact CLI/assets/fixture cause remains unresolved. Other capture and viewer checks pass; this is not established as a CEF-only recording bug.

## Additional intermittent iframe input

The focused matrix's cross-site iframe click at `compatibility.spec.ts:410` returned, but the button stayed `Click`. Updated helper repetition: **18/20 pass**, checkpoint helper: **17/20 pass**. A second complete focused run passes **53/53**, so one successful matrix is insufficient to certify the race fixed.

A reduced standalone CEF input probe passes 30/30 in one browser and 20/20 in fresh browser starts; stock passes 30/30. An instrumented native reduction passes 35/35. These reductions alter lifecycle/timing and **do not identify the faulty layer**. This remains a SuiteCut–CEF integration investigation: correlate native input, frame attachment and compositor hit-test readiness in the exact flaky fixture. No click retries, forced clicks or arbitrary pre-input sleeps were added to the production adapter or the assertion.

This new issue is tracked in [additional-findings.json](additional-findings.json), outside the preserved 438-case original inventory.

## Validation and limits

| Check | Result |
| --- | --- |
| Original 85-case targeted baseline | 4 pass / 81 fail |
| Same 85-case batch after initial fixes | 15 pass / 70 fail; before the final redirect/download corrections |
| Final confirmation batch | 42 pass / 3 upstream skips; three repetitions of affected cases |
| Request-continuation spec | 39/39 pass |
| OOPIF detach repetition | 30/30 pass |
| Modifier/codegen and delayed download controls | Saved individual repetitions and before/after failures |
| Native engine regressions | 22/22 pass, including recording, crashes, pending shutdown, modifier tabs and delayed popup downloads |
| Focused CEF matrix, first final run | 52 pass / 1 intermittent iframe-click fail |
| Focused CEF matrix, follow-up | 53/53 pass; intermittent limitation retained above |
| Focused stock matrix | 49 pass / 4 native-only skips |
| Eight isolated heap cases | Four bootstrap-count failures; workers, released handles, frames and bulk 2,000 handles pass |
| Queue/native protocol unit tests | 38/38 pass |
| Native CTest | 9/9 pass |
| TypeScript / lint / changed-file format | Pass |
| Clean final-patch consumer install | Frozen-lockfile install and exact active bundle hash match |
| Official upstream test body integrity | All 285 official library/page spec files still match the original archive; local diagnostics are listed separately |

These are functional and lifecycle checks, **not a new performance benchmark, long-duration soak, sanitizer fuzz campaign or proof of leak-free Chromium/CEF**. No new 4-core/8-GB performance comparison is claimed. CPU affinity/quota is four cores on this host; host memory is not capped to the earlier requested 8 GB for these diagnostic runs. Bounds remain in force, and fault/cleanup tests release the owned runtime; deliberately crashing independent controls can leave zero-RSS zombie entries for host PID 1 to reap. The cleanup snapshot distinguishes live processes from zombies.

The wake-lock experiment terminated the helper and contaminated several contemporaneous heap attempts. They are preserved as rejected experiments, **excluded from final heap conclusions**. Two early manual-link runs had an incorrect loader path and are also excluded. Failed runs, port collisions and diagnostic non-reproductions remain in the evidence; no claim silently aggregates only green attempts.

## Evidence and reproduction

[evidence.tar.gz](evidence.tar.gz) contains raw reports, logs, failure artifacts, exact tested fixture/config, diagnostics, standalone controls, source excerpts and failed experiments. [reproduction/](reproduction/) contains readable entry points and integrity metadata. [SHA256SUMS](SHA256SUMS) seals the report files and archive. [tested-binaries/suitecut-browser](tested-binaries/suitecut-browser) is the tested replacement helper; it requires the matching full codec-enabled CEF runtime resources/libraries and host FFmpeg libraries. It is not a standalone installation or a rebuilt release archive.

Use the same pinned source/dependencies and display/PulseAudio wrapper as the full audit. Apply the saved audited fixture and config in a fresh staging tree; set helper paths and artifact directories for that tree. `direct.py` runs exact upstream file/line selectors; `check-suites.py` runs committed engine/focused regressions. These scripts retain this workspace's absolute paths as evidence and must be adjusted when replayed elsewhere. Build the missing in-page client bundle before selecting its test; isolate proxy variables only for the local certificate controls. The original staged browser fixture was restored after testing.

The remaining runtime/architecture work is explicitly documented rather than masked by JavaScript compatibility shims, widened memory limits, altered upstream bodies or replacing failing behavior with stock Chromium.
