# SuiteCut 2.1: locale, headers and concurrent page fixes

Prepared 2026-10-11. Base commit `416e15263af2810f79c2c4cb8c574665b98d19ea`;
the pushed fallback `80759aeb30cc4f6bd4ab488554569c62851985d2` is unchanged.
These are functional tests on 4 CPU cores with a 16 GiB host memory limit, not
the published 4 CPU / 8 GiB recording/streaming performance comparison.

## Retained fixes

1. **Popup language before initial script.** Playwright's per-target locale
   emulation arrived after a popup could execute its first script. The native
   bridge now accepts `suitecutLocale` during request-context creation and uses
   CEF's `accept_language_list` setting. The explicitly patched driver forwards
   the context locale only when the native capability is present. This also
   avoids changing sibling pages through a target-scoped CDP emulation command.
   Tests cover initial popup language, two simultaneous contexts, worker locale,
   HTTP and WebSocket headers, and restoring an unmodified default context.
2. **Real user-agent metadata.** The old `Browser.getVersion` shim returned an
   abbreviated headless user agent solely to prevent Playwright adding window
   borders. API requests inherited that fake value while page requests used
   Chromium's real UA. The bridge now forwards the real version response and
   adds `suitecutWindowless: true`. The pinned driver recognizes the capability
   without changing the UA. Default and redirected API request headers match
   the page; explicit overrides remain supported. A 3840×2160 test verifies
   viewport and window bounds with no toolbar/border pixels.
3. **Concurrent page creation.** The broader locale test opens eight pages at
   once. This reproduced `Too many pending native commands`: each page emitted
   a startup burst, exhausting the native SDK's 64-command limit and closing
   the automation transport. The SDK now submits at most 16 native CDP commands
   concurrently and preserves FIFO submission. A native IPC acknowledgement
   frees a slot; the queue never waits for the renderer's eventual CDP response.
   Queued/in-flight commands are limited to 1,024 and 64 MiB of serialized CDP
   payload. Overflow/rejection closes the transport and releases queued work.
   Unit tests exercise 250-command bursts, both capacity limits, delayed
   acknowledgements after close, and failed native submissions.

The media engine remains native C++; the new SDK queue only routes automation
commands. Incoming per-packet task yielding from the previous fix is retained.

## Verification

| Check | Result |
| Focused CEF matrix, retained source (`final-cef-verified`) | 38 passed |
| Same matrix, patched-driver stock Chromium (`final-chromium`) | 37 passed, 1 native-only skip |
| Upstream locale file + default header selection (`locale-headers-fixed`) | 16 passed |
| Popup language and eight-page isolation, five repetitions each (`locale-repeated`) | 10 passed |
| Native engine recording/streaming/lifecycle (`native-final`) | 16 passed |
| Native CTest | 9 passed |
| SDK command/delivery/native/connection unit tests | 41 passed |
| Full upstream browser-context event file (`popup-events-final`) | 14 passed, 4 timed out |
| Inline popup prompt, ten fresh-worker repetitions (`inline-popup-repeated`) | 7 passed, 3 timed out |

SDK build, full TypeScript check, targeted ESLint and formatting checks pass.
Earlier failing build/type/lint logs are retained; `*-clean.log` and
`format-final.log` are the successful retained-source checks.
All attempts, including failed experiments, are in `summary.json` and
`cases.csv`. Repetitions are independent runs, not additional compatibility
coverage, and must not be summed into a conformance percentage.

## Remaining popup work

The final 18-case upstream browser-context event file still fails four cases:
`console event should work in popup 2` (a javascript URL closed immediately),
`dialog event should work in popup`, `dialog event should work in popup 2`,
and `dialog event should work in immediately closed popup`.
The ordinary page dialogs, console handles, popup events, web errors and ordinary
immediately-closed popup console case pass in that run.

An immediate native prompt can arrive before the popup's frame-tree reply and
renderer listeners. In the trace, initialization commands remain unanswered
while the prompt is open, and the test waits for a usable popup before accepting
it. This establishes an initialization/event-ordering failure; it does not
establish a complete fix. Returning dialog ownership to Chromium was tested,
regressed dialog behavior, and was reverted. A native probe also found that
`Target.attachToBrowserTarget` returns `Not allowed` through this CEF page agent;
that probe is diagnostic and is not counted as compatibility coverage.

The inline-script popup prompt remains intermittent: 7 passes and 3 timeouts
in ten repetitions. A further early-listener/dialog-buffer experiment produced
5 passes and 5 timeouts and was discarded; its exact diff is saved as
`discarded-dialog-buffer.patch`. The tested retained driver was restored and
its hash rechecked against the clean dependency install. Thus five distinct
early popup cases remain unresolved. No expected-failure annotations were added.

## Delivery and limits

The driver patch must be installed explicitly; importing SuiteCut does not
modify Playwright. Use Playwright 1.62.1 and the current patch and lockfile.
A fresh isolated dependency install produced a byte-identical patched driver;
`patch-install-verification.json` records the hashes. Stock Chromium controls
use that same driver, with SuiteCut-specific behavior gated by the native
capability. Root-workspace pnpm metadata still warns that the lockfile changed;
the separate clean install verifies patch reproducibility.

The full upstream Playwright suite is still unfinished. Independent launching,
persistent profiles, partitioned cookies and independently isolated raw CDP
domain state remain outside current support. These tests do not establish
whole-CEF sanitizer coverage, leak freedom, GPU encoding or long-run audio
continuity. Native CTest and queue bounds are narrower evidence.

Earlier sealed release archives and the fallback branch are unchanged. No npm
package, release tag or website update was published by this work. The matching
native helper and complete raw evidence are saved alongside the source report.
