# Real-world browser control and media verification

Verified on Linux on 2026-10-18 using the standalone SDK and existing CEF, Chrome and Firefox runtimes. These are named end-to-end scenarios with independently checked outcomes, rather than counts of individual API/unit tests. No videos were recorded. No production SDK implementation was changed during this verification.

**23 supported workflow executions passed. Native CEF does not currently support the tested H.264/AAC playback paths, and actual Twitch playback failed there. The SDK controlling Chrome successfully played the same live Twitch channel.**

No finite scenario suite establishes every possible website, codec profile, device, account or failure combination. Local application fixtures provide deterministic business outcomes; the public Wikipedia and Twitch checks add actual third-party website evidence. Authentication against third-party accounts, payments, MFA, protected DRM content and long-duration streaming were not exercised.

## Complete workflows

| Scenario | Native CEF | Chrome CDP | Firefox BiDi |
| --- | --- | --- | --- |
| Sign in, reload persisted session, verify isolated context | Pass | Pass | Pass |
| Search/filter catalog, edit cart, reject invalid checkout, confirm exact server-side order | Pass | Pass | Pass |
| Popup and embedded-frame team review, verify saved notes | Pass | Pass | Adapter APIs unavailable |
| Upload actual inventory bytes, download and hash confirmed-order report | Pass | Pass | Adapter APIs unavailable |
| Recover from HTTP 503 and failed action, sign out and invalidate server session | Pass | Pass | Pass |
| Unicode contenteditable document, save and reload server-persisted text | Pass | Pass | Not run |
| Two-tab collaboration over an actual local WebSocket connection | Pass | Pass | Not run |
| Trusted pointer drag of a kanban card, verify server receipt | Pass | Pass | Not run |
| Service-worker cached handbook while its origin returns HTTP 503 | Pass | Pass | Not run |
| Public Wikipedia search using native text/key input, read matching article and citations | Pass | Pass | Not run |

Totals: **CEF 10, Chrome 10, Firefox 3**. “Not run” is distinct from an unavailable capability. The service-worker case simulates an origin response outage; it does not establish operating-system network disconnection handling. The editor's Unicode insertion does not establish every IME/composition sequence.

The commerce server independently checked quantity three and the 44,700 minor-unit order total. Both advanced adapters uploaded exactly 155,648 bytes with the expected SHA-256 and downloaded the actual 111-byte order export. The collaboration server independently recorded exact Unicode text, bidirectional socket updates and trusted pointer events. Public research verified the article heading, introduction and 34 citation references, rather than merely accepting successful command responses.

Reusable scripts:

- [Account, commerce, file and recovery journeys](../scripts/verify-browser-real-world-scenarios.mjs)
- [Editor, WebSocket, pointer and service-worker journeys](../scripts/verify-browser-collaboration-scenarios.mjs)
- [Public research](../scripts/verify-browser-public-research.mjs)

The local scripts use existing repository development dependencies and final `packages/browser-control/dist/browser.js`. They have executable-path/output overrides where applicable. The public probes preserve workspace proxy settings and TLS certificate verification. Tests use local controlled state rather than making purchases or modifying external accounts.

## Actual playback matrix

Versions: CEF **156.0.7 / Chromium 156.0.8078.17**, Chrome **156.0.8078.12**, Firefox **158.0**. The results apply to these installed Linux binaries and the generated samples, not every browser distribution or codec profile.

| Input sample/path | Native CEF | Chrome CDP | Firefox BiDi |
| --- | --- | --- | --- |
| VP8 + Vorbis or Opus in WebM | Pass | Pass | Pass |
| VP9 + Opus in WebM | Pass | Pass | Pass |
| AV1 + Opus in WebM | Pass | Pass | Pass |
| VP9 video / Opus audio through separate WebM MediaSource buffers | Pass | Pass | Pass |
| H.264 video + AAC audio in MP4 | Unsupported | Pass | Pass |
| H.264/AAC fragmented MP4 through MediaSource | Unsupported | Pass | Pass |
| H.264-only MP4 | Unsupported | Pass | Pass |
| HEVC-only MP4 | Unsupported | Unsupported | Pass |
| AAC-only M4A | Unsupported | Pass | Muted playback passes; unmuted audio sink fails |
| MP3, PCM WAV, Vorbis, Opus and FLAC audio-only | Pass | Pass | Muted playback passes; unmuted audio sink fails |
| Actual live Twitch channel | Failed: error #4000, zero decoded frames | Pass: 720p, advancing time and decoded frames | Not run |

Media files were generated locally with FFmpeg and served over HTTP with range handling. Success required a real trusted SDK click, advancing media time, pause/resume, completion, and decoded video frame/render callback evidence for video. File playback also checked seek. `canPlayType`, `MediaSource.isTypeSupported` and WebCodecs configuration results were recorded separately from actual playback.

The original combined VP9/Opus WebM MediaSource fixture had a timestamp-order compatibility failure in CEF and Chrome. The follow-up used standard separate video/audio SourceBuffers and passed all three browsers, with 15 decoded frames and rendered callbacks. This is preserved as a fixture compatibility result, not a fixed SDK defect or proof every mux is accepted.

Firefox's six unmuted audio-only samples failed with `OnMediaSinkAudioError`, including PCM. All six then passed muted pause/seek/resume/completion. This demonstrates decoder/timeline support separately from the workspace's audio-output limitation; it does not establish working audible playback. Media time progression is not a measurement of audible PCM output or audio capture fidelity.

The initial Twitch channel was offline, so it could not verify playback. The actual live check used AlveusSanctuary, which was visibly live. Chrome progressed from **2.85 to 14.91 seconds**, decoded frames **118 to 841**, at **1280×720**, with no media error. CEF displayed the service's “unavailable or not supported” error #4000, and both time and decoded frame count stayed zero. Local H.264/AAC fixtures independently confirmed the missing codec path; Twitch's error number alone was not used as a decoder diagnosis.

Reusable scripts: [media playback](../scripts/verify-browser-media-playback.mjs), [Twitch smoke check](../scripts/verify-browser-twitch.mjs). The media verifier includes affected-only fixture/backend/mute controls and preserves initial and follow-up evidence.

## Why recording support does not imply playback support

The native recorder/publisher invokes a separate FFmpeg executable to encode captured raw pixels and PCM. Its H.264/AAC encoders do not add H.264/AAC decoders to CEF's `libcef.so`.

The pinned CEF minimal archive is prebuilt. SuiteCut's normal CMake build links it; it does not rebuild Chromium's media stack. The current build recipe records neither `proprietary_codecs` nor `ffmpeg_branding` enablement. Runtime testing, rather than that absence alone, establishes the H.264/AAC gap.

To provide native H.264/AAC playback, the pinned CEF/Chromium library needs an appropriately configured codec-enabled build, followed by the same file, MediaSource and actual-site checks. The existing custom CEF build remains pending separately. No JavaScript flag, replacement output FFmpeg executable or claimed capability bit has been presented as a fix. **Driving installed Chrome through this SDK is the verified working Twitch path now.**

Widevine/DRM is separate from codec decoding. No Widevine provisioning/runtime artifact was found; licensed DRM playback remains unverified. MSE-based streaming support does not imply native HLS manifest support. Headless CEF currently uses software rendering; these samples do not establish hardware decoding or sustained high-resolution/high-frame-rate throughput.

## Cleanup, failures and evidence

CDP/BiDi command/listener counts returned to zero after the app journeys. CEF exited zero. Collaboration and public-research supervisors confirmed every adopted descendant retired, without requested kills or timeout. The app/media supervisors also retired all descendants with no requested kills; their strict cleanup flag was raised by Firefox workers exiting through SIGTERM. That receipt is preserved rather than described as all-zero natural exits or a surviving-process leak. Short scenario memory samples are not a substitute for the separate endurance/memory review.

One CEF screenshot at the end of the original live Twitch check hit the generic 30-second SDK command timeout. A separate bounded 5-second screenshot/recovery check succeeded, including a visually verified Twitch error screen and a later simple document. The original timeout remains unexplained; the later success does not prove it fixed. Source inspection identified font, animation-frame and descendant-document readiness waits but did not identify the failing stage. Earlier intermittent CDP input and native idle allocation findings from the [heavy review](../.suitecut/hard-browser-review/REVIEW.md) also remain open.

Raw evidence under `.suitecut/real-world-review/`:

- `journeys/verified/results.json` and `journeys/verified-supervisor.json`: 13 app journeys, independent server audit and unchanged hashes for all 120 SDK JavaScript files.
- `collaboration/results.json` and `collaboration/supervisor.json`: eight composed journeys.
- `public-research/results.json` and `public-research/supervisor.json`: two actual external research flows.
- `media/result.json`, `media/webm-mse-followup/`, `media/firefox-muted-followup/`: original 45-case matrix, affected-only streaming and audio follow-ups, generated samples and source commands.
- `twitch-live/results.json`, `twitch-live/cdp.png`: live player state progression and Chrome screenshot. The initial offline/harness-option error attempt remains under `twitch/` and is not a native playback pass.
- `twitch-screenshot/REVIEW.md`, `results.json`, `twitch.png`: screenshot reproduction and inspected CEF error presentation.
- `audit.md`: exact native binary/library hashes, build provenance and broader remaining scenario families.

Changed scripts pass focused ESLint. No additional unit-test run or broad native rebuild was needed for these verification-only additions. Public-site results can change with channel availability, site updates, geography, service policy and network conditions; deterministic fixtures remain the repeatable regression basis.
