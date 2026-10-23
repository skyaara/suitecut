# Browser SDK fuzzing, memory and performance

Completed on Linux, 17 October 2026. This covers the browser-control SDK and
owned native transport/lifecycle code. Playwright Test compatibility and other
language bindings are outside this scope. No external Playwright dependency was
introduced.

The installed native binary has SHA-256
`a5e8f8a376e8d710bcbbbc0a9e65d5fcdd969a723bfc34f76103ae417d5bf6b6`.
The matching browser SDK was emitted from
`87d3e513ff82f325d9271b5e7161bc33e0cff692`. Its 226 emitted files install and
its 43 runtime exports import successfully. Subsequent changes only adjust test
selection and record results.

## Executed scope

| Surface | Completed checks |
| --- | --- |
| Browser SDK regressions | 773 passing tests across 110 browser-control files |
| Packet decoder | 1,000,000 seeded cases, 3,250,987 checks, fragmented/coalesced/reentrant and invalid packets |
| Value/string/glob boundaries | 1,200,000 graph, handle, malformed/tag mutation, UTF-16 and independent glob-oracle cases; 12 typed views |
| HTTP/WebSocket routing | 1,345,120 schedule operations over three seeds; 322,967 controlled-protocol WebSocket events; 480 staged messages |
| SDK lifecycle | 364,032 schedule operations, 891,131 modeled commands/replies and 6,144 page-close events over three seeds |
| Native controls | 4,000,000 framing, operation/deadline and pause cases; 100,000 WebSocket payload schedules |
| Ownership and debugger cache | 10,000 closed-owner cycles; 51 debugger component groups, including 1,000 role transitions |
| Real browser comparison | Identical seeded 1,966-operation workloads with 128 context/page create-navigate-close cycles per run |
| Final installed artifacts | 14 live feature probes passed; separate 128-context helper churn, settle and memory-pressure checks |

These counts describe different kinds of cases and operations; they are not a
percentage of all possible browser behavior. The merged SDK campaigns were run
again through the repository driver. Native components passed ASan, UBSan and
LSan checks. CEF-value components disable UBSan's vptr check because the existing
CEF wrapper was built without RTTI; other enabled checks remained active.

Live probes include context/page/popup/frame lifecycle, locators, evaluation and
handles, dialogs, workers, routes, HTTP and multipart file streams, storage,
permissions, accessibility, uploads, downloads, WebSocket observation, trusted
keyboard/mouse/touch input, current-document clock/coverage, HAR replay, tracing,
PDF, crash recovery, screenshots and finalized video. The short video decoded as
125 VP9 frames at 320×240 and 60 FPS. This is not a sustained 120 FPS recording
or audio-encoding stress result.

## Fixed defects

- Decoder callbacks could corrupt parser state through reentrant delivery.
  Nested input is now owned, bounded and ordered behind earlier coalesced bytes.
- Malformed/truncated decoder input retained partial payload storage. The
  regression retained 192 MiB before disposal; failed decoding now releases it.
- Closed SDK transports retained two subscriber families and accepted six kinds
  of new observer registrations. Actual closure now releases/rejects them.
- Throwing context-close listeners could interrupt mandatory cleanup. Remaining
  observers and cleanup now finish before authored failures are reported.
- Screenshot deadlines could retire work before its physical response. Ownership
  now follows the actual response or process closure.
- Concurrent HTTP route removal could erase a later registration. Removal now
  uses the registration set captured for that operation.
- URL glob matchers accumulated their work budget across calls and eventually
  rejected healthy long-lived routes. Each lookup now has its own budget.
- Native operation admission accepted invalid clock/deadline values. Non-finite,
  negative and overflowing inputs are rejected.
- A stored request-body callback formed a closed-page strong-reference cycle.
  Closing preparation now retires admission hooks while preserving actual reads.
- Internal Inspector factory sessions retained collected script sources without
  a limit. They now use an acknowledged 8 MiB cache policy. Explicit debugger or
  coverage ownership preserves historical sources; actual release restores the
  internal bound. See [the cache contract](../features/native-debugger-script-cache.md).
- The dispatch optimization initially dropped the JSON-object envelope check.
  Five adversarial regressions exposed it; the final SDK preserves that check
  without rebuilding a Zod schema for every immediate command.

## Memory measurements

The matched live workload used seed `123456789`, eight batches and 5 FPS capture.
Aggregate renderer PSS grew from **139 to 222 MiB** before the fixes and from
**136 to 138 MiB** afterward. The measured renderer growth-rate slope fell
**97.8%**. Actual diagnostics showed the internal bounded cache active and no
historical observer cache throughout the steady workload.

The helper's PSS did not improve in the mixed workload; no helper-memory saving
is claimed. A separate context-only probe stabilized after warmup, with late
samples around 123–125 MiB and no pending native work after cleanup. That probe
did not demonstrate unbounded retention driven by closed-context count.

The decoder processed **5.25 GiB** of payloads with zero retained external or
ArrayBuffer growth after warmup. Modeled lifecycle callbacks became collectible
after actual closure, and routing callbacks/ownership maps balanced and emptied.
The Source-cycle component regression also proves owner release; these facts do
not establish whole-browser leak freedom.

Successful supervised live runs exited with native status 0 and actual descendant
reaping receipts. The initial unsupervised baseline's two unreaped zygote zombies
remain recorded as a cleanup failure rather than being relabeled a pass.

## Measured performance

| Paired microbenchmark | Result |
| --- | --- |
| SDK control dispatch, 30,000 commands | 905.29 → 243.68 ms median; 73.1% less time, final envelope validation included |
| Route candidate capture/matching, 1,000,000 iterations | 165.33 → 45.04 ms median; 72.8% less time |
| Small-packet decoding, 10,000 packets | 21.45 → 15.71 ms median |
| Buffer allocations for 10,000 empty response packets | 30,002 → 10,001 calls |
| Large UTF-16 encoding / decoding | 2.45× / 2.48× faster |
| Representative origin / alternative URL globs | 3.20× / 12.93× faster |
| Native command framing | 3.73–35.49× throughput across measured line/chunk sizes |

These are scoped microbenchmarks on a shared cloud host, not whole-browser
speedup claims. Raw paired samples and exact input hashes are retained in the
hardening proof directory.

Click latency also depends on actual presentation cadence: the unchanged
two-animation-frame actionability check measured approximately 600 ms at 5 FPS,
50 ms at 60 FPS and 30 ms at 120 FPS. This configuration effect is separate from
the code optimizations. Actionability checks were preserved.

## Repeatable commands

```sh
node scripts/verify-browser-sdk-hardening.mjs .suitecut/sdk-hardening
node scripts/verify-browser-sdk-hardening.mjs .suitecut/sdk-hardening-native --native
node scripts/verify-browser-sdk-hardening.mjs --help
```

The driver emits an isolated browser SDK and runs checks sequentially with
bounded Node heaps and one unit-test worker. It records commands, logs, timings,
exit status and supervisor cleanup receipts without wiping the installed SDK.
It requires the existing Linux proof supervisor; `--help` documents the override
and separate supervised live-browser command. Individual fuzz, performance and
live harnesses are under `scripts/sdk-*.mjs`.

## Limits and further scope

- These are finite seeded property/schedule campaigns, not exhaustive path
  coverage or a claim that arbitrary authored callbacks cannot consume CPU.
- The prebuilt CEF/Chromium library was not rebuilt with sanitizers. Native
  component sanitizers and live PSS/GC measurements cover different boundaries.
- Long-duration, multiple-machine, GPU/hardware encoder and sustained audio/video
  load testing remain separate from this run.
- The stock library's unsupported custom WebSocket interception, retained-coverage
  lifecycle, isolated clocks, directory upload/populated-input reset, ordinary MIDI and input receipt
  capabilities remain explicit backend limits. Controlled-protocol tests do not
  establish those capabilities in stock CEF.
- Low-FPS capture still lowers animation cadence. Separately configurable
  presentation and capture rates are a potential future latency tradeoff.

Machine-readable live evidence is in
[the comparison receipt](sdk-browser-stress-postfix-20261017.json). The current
installed build receipt is `.suitecut/diagnostics/native-browser-build.json`;
the full local hardening report is
`/workspace/suitecut-sdk-hardening-proof-20261017/hardening-summary.json`.
