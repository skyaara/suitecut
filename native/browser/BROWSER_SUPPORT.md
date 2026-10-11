# Browser compatibility and remaining work

SuiteCut targets broad Chromium web-app automation and recording. “Full Chrome”
also includes a desktop shell, Google product services, extensions and protected
media integrations; embedding CEF does not provide all of those automatically.
Support means exercising real browser behavior through our adapter, not merely
finding an API name or receiving a successful protocol response.

## What the browser build contains

The Linux default is the checksum-pinned codec-enabled CEF 152 distribution.
SuiteCut has not source-modified or removed Blink, V8, DOM/CSS, workers, network
APIs, IndexedDB, WebRTC or GPU APIs from that binary. The public build recipe
enables H.264/AAC and changes build/link optimization and debug-symbol settings.
The “minimal SDK” packaging and absence of a Debug build do not mean a reduced
HTML/JavaScript engine. This is a prebuilt third-party artifact, not a complete
independent audit of every Chromium build flag.

Linux `SetAsWindowless(0)` explicitly chooses CEF's **Alloy runtime style**. That
provides the embedded web surface and paint callbacks; it has no Chrome toolbar
or ordinary desktop window. Some native dialogs and Chrome-style integrations
need a native parent window or different runtime architecture. Changing this
style is an architectural change, not a codec switch.

## Verified capabilities

| Capability                                            | Evidence and scope                                                                                                                              |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------- |
| DOM, shadow DOM, CSS grid and Canvas 2D               | Actual layout/pixel checks passed                                                                                                               |
| WebAssembly                                           | Instantiated module executes and returns the expected result                                                                                    |
| Dedicated and shared workers                          | Actual worker message round trips passed                                                                                                        |
| IndexedDB and CacheStorage                            | Actual write/read checks passed, including IndexedDB after document reload (profiles remain temporary)                                          |
| Service workers                                       | Registration/control/fetch; Playwright enumeration/evaluation/console/lifecycle, first-page closure, main-script and internal-request routing, HAR and network attribution passed |
| WebSockets                                            | Local text echo round trip passed                                                                                                               |
| WebGL 1 and 2                                         | Shader compilation/drawing, pixel readback, composited screenshots and native recorded pixels passed with explicit SwiftShader on this CPU host |
| WebRTC                                                | Local data-channel and actual canvas-video encode/decode peer loopbacks passed; camera/microphone calls and remote networks are separate checks |
| HTML-to-PDF export                                    | Actual Playwright `page.pdf()` returns and saves a PDF document                                                                                 |
| Clipboard                                             | Actual write/read passed after explicit permission grants in the owned display                                                                  |
| H.264/AAC video playback                              | Progressive MP4 and MSE decode, native captured AAC, recorded file decode passed                                                                |
| Input, scrolling, selectors and frames                | Real Playwright locators, auto-waiting, typing, mouse wheel, select, iframe input and screenshot passed                                         |
| Popup pages, JavaScript dialogs, file upload/download | Actual `launchEngine` Playwright integration passed                                                                                             |
| Network routing, init scripts and isolated storage    | Actual Playwright integration passed                                                                                                            |
| Permissions/geolocation                               | Grant/reset and geolocation setup passed; other permission/device combinations have narrower coverage                                           |
| Partitioned cookies (CHIPS)                           | Chromium Network cookie commands preserve partition keys; context isolation, filtered clearing, state round trips and cross-site delivery verified, including contexts with no pages |
| Windowless page resize                                | Per-target window IDs, applied bounds, physical screenshot width and sibling isolation passed                                                   |
| Native recording/streaming lifecycle                  | Readiness, silent pages, pause, selection and shutdown regression checks passed                                                                 |

The ordinary web-platform checks can be rerun with `pnpm test:native:features`
after building the native executable and SDK. They require a working display and
audio service. WebGL 1/2 rendering is required by default. On a CPU-only host,
run with `SUITECUT_GRAPHICS=swiftshader` to explicitly opt into software WebGL.
`SUITECUT_ALLOW_MISSING_GL=1` is for availability diagnosis only and must not be
used to establish WebGL support. WebGPU remains an optional availability probe.

```ts
const engine = await launchEngine({
  executablePath: '/absolute/path/to/suitecut-browser',
  graphics: 'auto', // Chromium's normal graphics selection (default).
})
// CPU-only trusted workloads may instead explicitly select graphics: 'swiftshader'.
```

The thin SDK forwards `--suitecut-graphics=auto|swiftshader`; the C++ browser
backend selects ANGLE/SwiftShader. Unknown modes fail before launch. Auto adds no
special graphics flags. SwiftShader sets `--use-gl=angle`,
`--use-angle=swiftshader` and `--enable-unsafe-swiftshader`. Modern Chromium
requires this last explicit opt-in because software WebGL fallback has reduced
security protections. It is not silently enabled for arbitrary websites, and
SuiteCut does not disable renderer sandboxing. Software rendering consumes CPU;
these functional checks do not establish GPU performance or a leak-free browser.

## Intentional policies and capture constraints

| Policy or limit                       | Current behavior                                                                                                                                                        |
| ------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Browser context menu                  | Native built-in right-click menu is cleared. A website's JavaScript `contextmenu` handling remains available                                                            |
| Browser profiles                      | Temporary isolated profiles; no normal user Chrome profile or persistent browser launcher is exposed                                                                    |
| Permissions                           | SDK grants supported permissions explicitly; ungranted media/permission prompts are denied rather than creating native prompt windows                                   |
| Dialogs, downloads and popup pages    | Supported through `launchEngine`; the older low-level control path suppresses/denies these by default                                                                   |
| Timer/background throttling           | Disabled to keep capture active; this changes scheduling, not web API availability                                                                                      |
| Native desktop window placement/state | The captured surface remains at (0,0), with normal window state. Unsupported maximize/minimize/native-window placement now reports an error; DOM fullscreen is separate |
| Capture dimensions                    | Physical size up to 3840×2160, supported output rates 30/60 FPS. Ordinary browser views/screenshots allow odd sizes and 1×1; encoder/raw I420 dimension constraints are separate |
| Raw frame subscriptions               | Existing low-level subscribers negotiate fixed dimensions; resizing a source while those subscriptions are active is rejected                                           |
| Output image/audio format             | Native pipeline uses 8-bit BT.709 I420 and 48 kHz stereo PCM; HDR, 10-bit and RGB-lossless output are not implemented                                                   |
| Resource limits                       | Bounded contexts/targets/sessions/outputs/queues protect lifecycle and memory stability; these are capacity limits, not removed web engines                             |

## Explicitly incomplete or unverified

- **Audio continuity:** the latest saved live run still has uncovered audio
  samples. Fixing playback and resize does not fix missing source audio.
- **Full Playwright compatibility:** our private transport implements a subset
  of browser-level CDP management and forwards page commands. Focused and pinned
  upstream tests now establish specific workflows, with confirmed failures;
  the full upstream compatibility suite has not been completed. See the
  [October 11 report](../../benchmarks/playwright-compatibility/2026-10-11/report.md).
- **Authentication-state breadth:** cookie/localStorage/IndexedDB file export
  and restoration pass, including binary values, dates, indexes, third-party
  localStorage, lone UTF-16 values and fresh-context isolation. Automation uses
  Playwright's origin-based storage semantics with third-party storage
  partitioning disabled. Persistent profiles and state
  exceeding native protocol limits remain unsupported or incomplete.
- **Worker/frame automation and networking:** basic service-worker automation
  and cross-origin process frame interaction/routing pass. Advanced worker
  main-script routing/HAR attribution and offset iframe input now pass.
  HTTP/HTTPS proxy and origin-auth cases pass individually; distinct proxy and
  origin credentials pass with the explicitly configured Playwright 1.62.1
  [driver patch](../../patches/README.md); unpatched drivers retain the overwrite.
  Explicit page CDP sessions now own independent debugger sessions. Raw worker
  aliases still need domain-isolation verification.
- **Playwright video/HAR breadth:** decoded WebM recordings, HAR JSON/ZIP
  capture/replay and redirected HAR history pass. Windowless repaint/visibility
  and dialog-event fixes help individual cases; yielding between CDP messages
  corrects the reproduced initialization race. Deletion and blank-popup checks
  passed ten repetitions each; independent launcher cases remain outside the harness. SuiteCut's synchronized native
  media output remains a separate API. Full upstream compatibility is not
  established by this subset.
- **Locale, headers and command bursts:** context language is configured before
  popup scripts execute; default API request headers use Chromium's real user
  agent. The explicit Playwright [driver patch](../../patches/README.md) preserves
  windowless dimensions, including 3840×2160. The SDK bounds concurrent CDP IPC
  submissions so parallel page creation does not exhaust native command slots.
- **Blank-origin srcdoc frames:** creating a srcdoc iframe in an initial
  about:blank document crashes the renderer in a minimal CEF client, without
  SuiteCut media or DevTools code. HTTP-origin srcdoc frames work.
- **Permission breadth:** storage-access and screen-wake-lock overrides remain
  unsupported; CEF contexts are not registered with Chromium’s browser CDP
  context manager. Existing permission grants retain their verified scope.
- **Early popup events:** javascript-URL console events and prompts during popup
  initialization still fail selected upstream cases; see the
  [latest report](../../benchmarks/playwright-compatibility/2026-10-11-remaining-fixes/report.md).
- **Graphics coverage:** WebGL 1/2 are verified with explicit software rendering.
  Chromium's default graphics selection and a Mesa/ANGLE OpenGL trial could not
  create a WebGL context on this CPU-only Xvfb host. Hardware GPU rendering,
  high-resolution graphics performance and WebGPU remain unverified; WebGPU did
  not provide an adapter even in the successful SwiftShader WebGL run.
- **Media breadth:** additional playback formats/profiles, real webcam and
  microphone use, successful remote WebRTC media, screen sharing, PDF viewer flows, physical printing
  and device-specific APIs need their own functional checks. Output encoder
  availability is a separate matrix.
- **Chrome extensions and native UI:** no extension loader or Chrome desktop
  shell integration is exposed by this windowless SDK. Native file-picker,
  print and browser-menu UX require a separate design if they must be captured.
- **DRM:** Widevine is not provisioned or verified. It requires a suitable
  content-decryption module, distribution/provider integration and compatible
  playback conditions; providers may restrict capture. It is an optional
  integration, not required for ordinary websites or unprotected video.
- **Platforms and security maintenance:** codec playback is validated on Linux
  x64 only. The currently available codec-enabled 152 build is older than the
  previous stock 154 build. A maintained, independently built newer distribution,
  macOS/Windows packaging and full dependency sanitizer coverage remain work.

## Implementation priorities

1. Resolve and attribute missing audio samples under continuous, intermittent
   and live-player workloads; retain matched OBS/source tracing evidence.
2. Maintain a current codec-enabled CEF distribution with reproducible builds
   and browser playback regressions on supported platforms.
3. Close browser-adapter gaps: partitioned/persistent state, worker/process
   targets, proxy/auth combinations and full upstream Playwright coverage.
4. Validate graphics, devices and the additional media/document workflows on
   hosts that provide their dependencies; add functional regressions for each.
5. Add optional Chrome extensions/native UI and DRM integrations where the
   required runtime, modules and capture behavior are available.

The October 10 support audit, failures, passing tests, native resize fix and exact
tested snapshots are saved in
[`benchmarks/native-browser-support/2026-10-10/report.md`](../../benchmarks/native-browser-support/2026-10-10/report.md).

## Remote RTC network diagnosis (October 10 follow-up)

SuiteCut joined a user-provided Jitsi room and received another participant's
screen-share signaling. Actual remote media did not connect: ICE produced no
working candidate pair and received zero transport bytes. The explicit
`webrtcPolicy: 'disable-non-proxied-udp'` setting is now available and verified
in all request-context types. Jitsi provided TLS TURN on TCP 443, but it returned
ICE error 701 (failed to establish connection) on this host. The configured TCP
CONNECT proxy endpoint refused a connection and no TCP destination grants/VPN
were configured. This establishes a blocked/unavailable usable transport here;
it does not establish complete remote media compatibility on another network.
See [RTC proxy setup](ENGINE.md#webrtc-with-a-network-proxy) and the saved
[diagnosis](../../benchmarks/native-rtc/2026-10-10-network-fix/report.md).
