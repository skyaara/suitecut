# Changelog

## Unreleased

- Stop closing busy pages with "Native network metadata publication capacity
  exceeded". Each network event waited for the host to acknowledge the POST
  body before it, and on eBay an ad beacon's body went unacknowledged while
  the viewer streamed frames, so 256 events piled up behind it. Bodies and
  events share one ordered pipe, so an event now goes out as soon as its body's
  chunks are queued, and the chunks go out at once instead of on the next
  10 ms tick. As in Playwright, a POST body that still can't keep up costs
  that request its body (`postDataUnavailable`), not the page, and a body the
  host never confirms consuming no longer closes the page after 30 s.
- Retry `fill`, `check`, `selectOption`, `scrollIntoViewIfNeeded` and other
  form actions when a navigation replaces the element's document mid-action,
  as Playwright does, instead of failing with "Native form evaluation failed".
- Let a person take control from the live viewer on busy sites. The handover
  waits for native work to finish, but on a page like eBay each request kept a
  short-lived correlation in case an auth challenge arrived, and console
  messages kept their arguments, so **Take control** always failed with "could
  not transfer while native work is active". Both now count as resident state
  during a handover; challenges in flight and argument reads still wait.
- Add `network: true` to `createBrowserViewer` and `--network` to `view`: the
  viewer also listens on this machine's network addresses, so a phone, tablet
  or another computer on the same network can watch and take over. It is plain
  HTTP with the link's bearer token; loopback stays the default.
- Keep `locator.waitFor()` and other expectations waiting when a navigation
  replaces the document they were polling, as Playwright does, instead of
  failing with "Cannot find context with specified id".
- Truncate frame names longer than 1024 UTF-16 units instead of closing the
  page with "Native frame name metadata encoding or unit limit exceeded". Ad
  frames on eBay and Target carry kilobytes of JSON in their names.
- Remove the shared browser recording's `area` option. `'window'` recorded a
  dark band where the toolbar is, because the tab strip and toolbar are drawn
  on a separate surface.
- Add shopping-site examples: a narrated eBay tour (`examples/real-sites/ebay.ts`),
  Apple's iPhone store rendered at 24, 60 and 120 fps
  (`examples/engine/frame-rates.ts`), a headless eBay run with `recordVideo`
  and a phone taking over an agent through the live viewer
  (`examples/browser-control`), and an MCP host driving the shared browser
  through eBay while it records itself (`examples/shared-browser/agent.ts`).
- Record the shared browser's window with `browser.startRecording({ path, fps })`.
  Chromium hands each composited frame to the browser process (the shared browser
  now disables `RemoteCoreAnimationAPI`), where a hook on the window's layer
  crops it to the page and copies it on the GPU, and only at the requested rate.
  It is then encoded through the existing media pipeline. Pages see no
  difference. `MacCapturePool` takes a larger budget for 2x frames.
  `audio: true` adds the front tab's sound; capture is on only while such a
  recording runs, and the captured sound is played back so the page stays
  audible. The MCP server adds `browser_record_start` and `browser_record_stop`.
- Add `@suitecut/shared-browser` 0.1.0 (preview): one native browser window with
  SuiteCut's own tabs that a person and an agent use together, with a TypeScript
  SDK and the `shared-browser-mcp` stdio MCP server. Input is trusted, CDP runs
  in-process, and permission prompts and dialogs wait for an answer.
- Prepare `suitecut` and `@suitecut/browser-control` 2.1.0, and add
  `pnpm release:check`, which checks every npm package's manifest, tarball and
  registry version without publishing.
- Keep Kokoro narration word timing when espeak joins several words into one
  phoneme run, instead of failing to map token durations to words.
- Say "suitecut" as "suite cut" in Kokoro narration; espeak read the one word as
  "sooty-cut". Captions keep the written name.
- Move the site onto Deli Paper, add a page per package under `/packages` with a
  hand-drawn diagram, quick start and limits, and add an explainer film for each
  package that suitecut records, narrates and renders itself
  (`scripts/record-site-films.mjs`).
- Add a complete real Song Maker example with isolated SDK tarball installation,
  external packages, browser actions and verified 1080p60 recording with page audio.
- Preserve bounded inline resource URLs in network observations and retain
  underlying protocol validation errors for diagnostics.
- Retire late audio-capture errors on committed CEF page close while preserving
  active-stream failures and cancelable beforeunload behavior.
- Split the C++ SDK into private build modules with stable installed C/C++ APIs,
  relocatable static/shared packages, source archives and consumer checks.
- Split the CEF browser implementation into protocol, runtime, page and process
  modules with domain controllers and architecture checks.
- Add a strict local 90-case CEF regression package with bounded workers,
  source/runtime fingerprints and verified process cleanup; CI remains unchanged.
- Fix navigation-close shutdown draining, duplicate debugger completions on libc++,
  and frame metadata regression during out-of-process frame navigation.
- Add macOS ARM64 capture, VideoToolbox encoding, bounded media backpressure,
  timestamp/audio clock fixes, runtime packaging and reusable signing policy.
- Keep the native CEF recording and streaming APIs and their shared media pipeline.
- Accept integer capture/output FPS from 1 through 120 with timestamp-based hold/drop
  resampling and exact audio sample boundaries.
- Remove the legacy external browser backend, its dependencies, reporter/test
  integration, examples, generated demos and performance comparison tooling/data.
- Preserve native capture, media and lifecycle verification.

The pre-cleanup source is retained in the local checkpoint
`checkpoint/native-cef-120fps-before-playwright-benchmark-removal` at `bf9fa19`.
