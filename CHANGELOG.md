# Changelog

## Unreleased

- Make AI snapshots faster and smaller. A 25,000-element page takes 3-5 s
  instead of 17.6 s, 100,000 elements finish instead of timing out, and a
  default `ariaSnapshot()` of 100,000 elements takes 6-10 s instead of 75 s.
  Names were computed with a whole-document `<label>` walk per element, styles
  were read again for every check, and the reference forest was parsed three
  times. AI snapshots also use fewer tokens: refs are short (`s1e5` instead of
  `r` plus 32 hex digits), a name that only repeats its children's text is left
  out (rows, cells, card links), and same-origin links are written as paths.
  Hacker News is 18% smaller, BBC News 26%, Wikipedia and GitHub 6%. Default
  and `toMatchAriaSnapshot()` output are unchanged. Refs from earlier versions
  no longer resolve.
- Snapshot pages of any size and depth. The 8 MiB YAML, 16 MiB JSON and
  1,000,000-node caps are gone, the `depth` option is no longer capped at 64,
  and an AI snapshot covers up to 1,000 iframes, Blink's own per-page limit,
  where it used to fail past 31.
- Stop an AI snapshot from waiting on an iframe the page has removed. BBC News
  removes one after load, and about one snapshot in three waited a third of
  its timeout (20 s) for it; it now shows that iframe empty, as Playwright does.
- Take inputs of any size, as Playwright does. A command to the browser used
  to stop at 64 KiB, so `page.evaluate()` arguments, `setContent()` HTML,
  `fill()` and `insertText()` text, `getByText()` strings, ARIA templates and
  `addInitScript()` sources larger than that failed. All of them now pass
  16 MiB. Init scripts stop only at 64 MiB per context, so their JSON fits
  Chromium's 128 MiB IPC message, and the error names that limit. Large
  commands sent from inside a DevTools callback no longer fail with "Could not
  dispatch DevTools command", which also lets a default `ariaSnapshot()` of a
  100,000-element page finish.
- Keep a page open when a busy renderer takes more than 30 s to confirm a new
  frame's init scripts. The page used to be closed; the frame still runs its
  scripts, and `diagnostics` counts it as `initScriptLateAcks`.
- Add `locator.click({ fallback: 'js' })`, an opt-in suitecut extension for
  agents. When the element stays covered, off-screen, hidden or moving for
  `fallbackAfter` ms (default 1000), the click stops waiting and runs
  `element.click()` instead, as browser-use does. Disabled elements, refs
  whose element is gone and every other timeout still fail as usual, and a
  click that may already have reached the page is never sent again. Without
  the option `click()` behaves as in Playwright.
- Say why a click, hover or fill timed out. On real sites most of suitecut's
  10 s click timeouts ended with only "waiting for element actionability" or a
  bare "Native operation timed out", so an agent could not tell a covered link
  from a disabled button or a stale ref. Like Playwright's call log, the
  timeout now names what it waited for: the element to be attached, visible,
  enabled, inside the viewport or editable, or the element that intercepts
  pointer events, such as the Guardian's consent `<iframe>` or Goodreads'
  `<div class="themeBgImage">`. A ref whose element a carousel or re-render
  removed says so and asks for a new snapshot. A deadline noticed partway
  through resolving a ref keeps the same call log. The click still waits and
  lands once the cover goes away, as in Playwright. A ref snapshot through the
  DOM driver also takes up to 1,000,000 refs, up from 65,536.
- Stop suitecut's own bookkeeping from closing pages that Chrome loads fine.
  In a 1,200-action soak across busy real sites, Chrome rendered every page.
  Every page or browser death came from one of suitecut's internal checks
  escalating a timing or size anomaly. Now:
  - An internal error on one page closes only that page, with its reason. It
    used to end the browser and every other page.
  - Cancelled work stays tracked until Chromium answers. If Chromium hasn't
    answered 30 s after cancellation, the work gives back its slot and is
    counted in the page's native `degraded` diagnostics, as an unanswered CDP
    call is in Playwright. This covers selector waits, element state, handle
    disposal, owner frames, snapshot references, page errors,
    authentication, and WebSocket and body output. On IKEA and
    OpenStreetMap, this check closed the page 30 s after a slow wheel.
  - A second debugger pause without `Debugger.resumed` supersedes the first.
    Chromium sends no resume when a navigation replaces the paused document,
    which closed Allrecipes pages after a click led to a sign-in page.
  - Chromium's "Session with given id not found" reply for an iframe or
    worker that has gone now retires that session. Before, it ended the
    browser.
  - Content a site controls is clipped or dropped instead of failing:
    - WebSocket frames over 16 MiB keep their first 16 MiB and are marked
      `truncated`.
    - Console messages over 8 MiB and source URLs over 8 KiB are clipped.
    - Exceptions over 24 MiB and bursts of page errors are dropped.
    - Requests past 8,192 open ones push out the oldest.
    - Too many sockets or auth challenges are no longer tracked; a challenge
      that can't be tracked is answered with CancelAuth.
    - At 4,096 requests, the request table evicts its oldest request even if
      it is unfinished (long polls, streams), and a network event over 1 MiB
      is dropped.
    - A HAR recording that reaches 2,048 entries stops growing instead of
      ending the browser; worker and context journals drop a record they
      can't hold.
    - A frame suitecut can't track is left to Chromium.
    - A widget mouse lease whose document navigated away is dropped, and
      the next input takes a new one.
    - Request and response header bookkeeping forgets its oldest request
      when full, and that request reports its headers as unavailable.
    - A request whose inline POST body doesn't fit is published without it.
    - A frame created past the init-script tracking limit still gets its
      scripts.
    - Page errors past 64 in flight are still reported, in order, from
      their description.
    - A Debugger command Chromium leaves unanswered, including suitecut's
      clean-world dispatch, resets that Debugger session instead of closing
      the page.
    - HAR recordings hold 16,384 entries and 64 MiB of metadata, up from
      2,048 and 16 MiB, and stop growing past either instead of failing.
    - Trace aria snapshots can be 8 MiB, up from 64 KiB, so busy pages keep
      them. A snapshot that can't be taken is skipped instead of failing the
      whole trace.
  - Deliver what Chrome and Playwright deliver instead of clipping it at
    suitecut's own limits. The limits left only bound memory;
    `native/browser/LIMITS.md` lists each one still to raise and how a page
    reaches it.
    - WebSocket frames up to 16 MiB arrive whole, up from 2 MiB, and so do
      `waitForEvent` predicates on them. A frame over 2 MiB used to end the
      browser, because the host rejected its `truncated` marker.
    - Console messages up to 8 MiB and page errors up to 24 MiB arrive whole,
      up from 1 MB and 1 MiB. A page error's description splits into `name`
      and `message` as in Playwright.
    - `request.postData()` returns bodies up to 64 MiB, up from 2 MiB. HAR
      recordings keep request and response bodies up to 64 MiB, and 512 MiB
      of them in all, up from 2 MiB and 16 MiB.
    - HAR files hold 65,535 entries and 256 MiB, and trace archives 65,535
      entries and 1 GiB, up from 4,096 entries and 32 MiB and 128 MiB.
    - Event predicates, as in `waitForResponse(fn)`, read from a journal of
      65,536 page events, up from 128. On a busy page they failed with
      "event journal cursor expired" while the host checked one response.
    - A request body the host has no room for reads as unavailable instead of
      ending the browser.
    - A page whose main thread stays busy past 30 s, as IKEA product pages
      do, stays open. A slow DevTools reply used to close it, and the soak's
      reload could not bring it back, so every later step failed. Calls made
      while the page is busy time out as they would in Chrome, and a
      `response.body()` that timed out can be read again once it answers.
    - A document whose init script bootstrap fails, usually because a click
      navigated away while it started, no longer closes the page. It is
      reported as a console error and in diagnostics, as a child frame's
      already was. lobste.rs closed the soak's page this way.
    - Locators and `ariaSnapshot()` work on pages of up to 1,000,000
      elements nested up to 512 levels, up from 100,000 elements for every
      CSS query and 65,536 nodes and 64 levels for snapshots. The full Python
      docs index, 44,000 elements, used to fail every snapshot. An accessible
      name over 64 KiB is cut there instead of failing the snapshot.
    - `tests/cef` `resilience.*` cases reproduce each of these volumes
      locally, and `pnpm soak:real-sites` replays the real-site soak that
      found them.
- Stop an unwatched live viewer from slowing the browser down. A viewer
  subscribed to every frame for as long as it was open, so on an animated page
  the browser copied each paint and the host read it even with nobody
  watching: on GitHub this took about half a core of Node and made commands
  six times slower. The viewer now listens only while a client asks for
  frames, and stops 5 s after the last request.
- Keep the browser running on ad-heavy sites such as Allrecipes. Requests of
  an iframe whose target went away never finished, so they filled the
  page's 1,024-row response table and the whole browser closed with "Native
  document response limit exceeded". Those requests now end with the target,
  and a full table drops its oldest row instead.
- Leave content hidden by `content-visibility: hidden`, such as
  `hidden="until-found"` and Wikipedia's collapsed navigation boxes, out of AI
  snapshots and `getByRole`, as Playwright does. Agents picked those links
  from the snapshot and every click waited out its timeout.
- Stop closing pages that contain a sandboxed iframe. Chrome runs a sandboxed
  frame in its own process, refuses `Debugger.enable` there because script is
  prohibited, and suitecut treated that refusal as a broken channel. A page
  with `<iframe sandbox srcdoc=…>` closed with "Debugger script-cache policy
  failed". Now that frame just has no debugger.
- Keep AI snapshots working on pages full of ad iframes. A snapshot read every
  iframe and failed whole if any child navigated, detached, errored, never got
  a document (a request that never answers, a lazy frame) or sat busy, so on
  BBC and CNN every `ariaSnapshot({ mode: 'ai' })` timed out for minutes. As in
  Playwright, such a child now leaves its iframe line empty. A child that
  doesn't answer gets a third of the time left (at least 1 s) before the rest
  of the page is captured without it.
- Capture an AI snapshot again, within the same timeout, when a navigation
  replaces the page's document mid-capture, and don't close the page when a
  paused init snapshot for a child or already-replaced document can't be
  delivered.
- Raise AI snapshot bounds from 4,096 nodes and 64 KiB to 65,536 nodes and
  16 MiB (8 MiB of YAML), and the DOM query work budget twentyfold, so long
  pages such as GitHub file views, Hacker News threads and python.org's
  changelog snapshot instead of failing with capacity or work-limit errors.
- Snapshot pages built from nested framesets, such as Craigslist's forums.
  The page's body was found with a selector that matched every nested
  `<frameset>`, so strict resolution failed.
- Stop the browser freezing on sites that use WebAuthn, such as npr.org. On
  macOS 26.2 and later Chromium asked the system whether iCloud Keychain holds
  passkeys with a synchronous call on the browser's UI thread, and when the
  passkey agent was slow every command timed out. suitecut's bundle can't use
  iCloud Keychain passkeys, so the check is off.
- Keep the browser running when an iframe can't get its isolated world or
  frame tree. On CNN an ad frame that went away mid-call ended the whole
  browser with "Native DOM execution world could not be created". Now that
  frame is skipped, as in Playwright, and the next operation that needs its
  world asks again.
- Let a person take control after an agent browsed sites full of ad iframes.
  DevTools commands sent to an iframe whose target then detached are never
  answered, and they counted as work in flight forever: after eBay, 490 of
  them blocked every later handover in that browser. They now end when the
  target detaches, on page and worker channels alike. Best Buy also starts
  shared workers from `blob:` URLs that never run, and Chrome never answers
  their startup commands; a handover no longer waits on a worker's own
  startup. A snapshot held by an earlier document in the back/forward cache no
  longer blocks a handover either.
- Say what is still running when **Take control** can't transfer: the error
  now ends with "(waiting on …)", naming the native work the handover waits
  for, down to the source line that posted a pending native task.
- Stop long sessions on busy sites from saturating the Node host. Once a
  browser had seen 4,096 requests, each new one re-serialised the metadata of
  every retained request to measure the cache, so on CNN the host spent most of
  its CPU on bookkeeping. Every browser reply then waited, snapshots and
  commands timed out, and handovers failed. Retained requests now reuse the
  size measured when they were last observed.
- Keep a page open when the browser is too slow to answer a live-frame
  subscription. A timeout ended the whole browser with "Native frame
  subscription failed"; the subscription is now asked for again.
- Keep **Take control** and **Resume AI** waiting while a heavy page keeps the
  browser busy. On CNN and BBC the browser's UI thread could take more than
  two seconds to answer the handover's idle check, so the takeover failed with
  "Native browser command timed out", and a failed release left the agent
  waiting for a person who had already let go. A slow answer now counts as
  busy, and the handover keeps asking until its own deadline.
- Allow a 64 MiB / 65,536-event output backlog to the host. A video page on
  BBC produced an event burst while the host was busy and the browser exited
  with "Native event stream exceeded its bounded output budget".
- Keep pages open when a frame fails to start, and keep the browser running
  when its first page crashes. A child frame that couldn't bootstrap its init
  scripts, usually an ad frame removed while it loaded, closed the whole page
  with "Native browser: Uncaught". Now only that frame is affected, as in
  Playwright. A crash of the first page no longer ends the browser: the page
  emits `crash` and `page.reload()` recovers it, like any other page.
- Let a person take over and hand back a crashed page in the live viewer. The
  viewer shows that the page crashed, its ↻ button recovers it, and frames
  resume after recovery, whoever reloads the page. Before, takeover and Resume
  AI could wait forever: a crash left the page's earlier init-script snapshots
  and in-flight DevTools commands looking busy, and kept held keys and buttons
  counted as pressed.
- Let a recovered page stream again on macOS. Live streams were marked failed
  on a crash but only re-enabled on recovery on Linux.
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
