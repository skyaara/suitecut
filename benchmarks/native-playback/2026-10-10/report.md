# Linux CEF playback codec fix and native GOFNS capture

Saved October 10, 2026. Human-readable times below use **Asia/Calcutta (IST)**.
Original capture JSON, browser test results and process samples retain their
original UTC or monotonic timestamps.

## Result and remaining issue

Linux x64 SuiteCut can now play H.264/AAC web media and record GOFNS directly
through CEF's paint/audio callbacks and the C++ FFmpeg-library engine. The old
stock CEF 154 build cannot decode H.264/AAC; having those output encoders linked
does not enable the browser decoders. Both actual progressive MP4 and MSE
playback regressions pass with the replacement, including decoded audio in the
final native recording. All seven native tests and 15 real-CEF Playwright
integration checks pass on the final executable.

**This fixes playback compatibility, not all audio timing defects.** The final
two-minute native Twitch run still reports **25,978 uncovered audio samples,
0.541208 seconds at 48 kHz**, and **142 repeated output frames (3.94%)**. Its
overflow-drop and late-audio counters are zero. These counters describe different
conditions; zero overflow does not mean continuous captured audio. The whole
delivered clip decodes successfully and contains real audio. Twitch's natural
silence prevents equating every quiet interval with a capture bug.

## Distribution and build

Linux's default is now **CEF 152.0.6+g708dc14 / Chromium 152.0.7977.83**, replacing
the codec-disabled stock **CEF 154.0.34+g14c5a08 / Chromium 154.0.8037.98**. This
is a deliberate version downgrade to an available codec-enabled Linux build.
macOS ARM64 remains on its existing stock 154 pin; its upgrade, codec playback
and native media packaging are unverified. Windows packaging remains unavailable.

The replacement is a third-party prebuilt distribution from
[tobagin/karere](https://github.com/tobagin/karere/releases/tag/cef-152.0.6-proprietary-codecs).
The public [build recipe at the release source revision](https://github.com/tobagin/karere/blob/eb5bfb5fb3c2463a2a71d7201a73122bf1942962/.github/workflows/build-cef-codecs.yml)
sets `proprietary_codecs=true` and `ffmpeg_branding=Chrome`. The source ref is
`eb5bfb5fb3c2463a2a71d7201a73122bf1942962`. Release metadata, recipe snapshots,
search results and the complete downloaded SDK archive are saved here.

| Item | Verified value |
| --- | --- |
| Archive size | 183,510,343 bytes |
| Archive SHA-256 | `3d64f6fac8911a368ffa415e3ee53d002cbd792866f08ed6578dae1e252cbac8` |
| Extracted libcef.so SHA-256 | `d3e896252f190a21cd04b956da638c90315c8d5067cb03f325913841d5c59fc8` |
| Final SuiteCut executable SHA-256 | `ed00de1fba8a91f3e72f5a42266f8eb4dd715f9da63ea412ddd210de1bc21248` |
| Final build | `.suitecut/native-build`, GCC 14, Release |
| Active codec SDK | `.suitecut/cef-codecs-sdk` |
| Media libraries | Debian FFmpeg 7.1.5 development libraries |
| Automation | Playwright 1.62.1, private CEF IPC, no debugging listener |
| Host CPU quota | Four CPU equivalents (`400000 100000`) |

The artifact was inspected and tested, **not independently rebuilt or fully
audited**. A full Chromium checkout/build is not practical on this host's 32 GiB
filesystem; the upstream recipe specifies a much larger build runner. The old
stock SDK and its manifest remain available for explicit comparisons. CMake's
default profile and the canonical executable now use the codec-enabled SDK.

`scripts/install-cef-codecs.py` verifies archive size/SHA and the extracted
library, rejects traversal/symlink entries, bounds extraction and preserves an
existing different SDK. It accepts a cached archive with the same checks. CMake
verifies both the exact CEF header version and runtime library digest. Saved
guard tests confirm that an invalid archive installs nothing and that a
same-version header with a different library is rejected.

## Linux child-process entry correction

The initial codec-enabled GOFNS run logged **27 stack-protector terminations**.
The previous Linux entry path called `CefExecuteProcess` inside the protected
`RunSuiteCutBrowser` stack frame. Chromium resets the stack canary after zygote
forks, so returning through an older protected frame can fail its guard check.
Upstream CEF's Linux example explicitly annotates the dispatcher
`NO_STACK_PROTECTOR`; its source and Chromium's canary-reset explanation are
saved as `cef-simple-linux.cc` and `chromium-stack-canary.cc`.

The correction moves subprocess dispatch into the small Linux `main`, using
CEF's annotation, before entering the browser implementation. Browser/media
stack protection, Chromium's canary reset and the process sandbox remain enabled.
Saved before/after disassembly verifies that `RunSuiteCutBrowser` retains its
stack guard and only the dispatcher crosses the fork boundary without one.
The final full Twitch run and final integration/playback checks log **zero**
stack-protector terminations.

The 15-second diagnostic Twitch run and 20-context lifecycle run with a fatal
backtrace interposer did not reproduce the intermittent termination. No crash
backtrace was captured for the initial 27 failures; do not infer that every
possible child failure has this cause. The diagnostic interposer still aborts
and is not linked or preloaded in the production/final recording executable.

## Browser playback regression

`playback.mjs` creates five-second 640×360/30 fixtures with actual encoded
H.264/AAC: progressive High-profile MP4 and fragmented Main-profile MP4 appended
to a real MSE SourceBuffer. A local HTTP server supports byte ranges. Tests
require advancing playback, decoded video frames and native audio samples,
then decode both tracks of each finalized FFV1/PCM native recording and check
its nonzero audio tone. Capabilities alone are insufficient to pass.

| Check | Stock CEF 154 | Codec-enabled CEF 152, final build |
| --- | --- | --- |
| H.264 baseline/main/high declaration | Unsupported | `probably` |
| AAC declaration | Unsupported | `probably` |
| H.264/AAC MSE declaration | false | true |
| VP9/Opus and AV1 declaration | `probably` | `probably` |
| Progressive MP4 playback and native recording | Stops at missing-codec assertion | Passed |
| MSE playback and native recording | Unsupported | Passed |

The old build's exit status 1 is an **expected negative regression**, saved in
`stock-negative/`. The original positive candidate is in `codecs-positive/`,
the installed canonical build before the entry correction in
`canonical-playback/`, and the final full-file decoded verification in
`final-playback/`. Final progressive/MSE recordings each contain 56 video frames
and approximately 89,500 mono decoded audio samples with RMS around 2,894.
Both final short tests report zero missing/late/overflow audio samples, empty
queues at stop and no output error. These short fixtures do not establish
continuous live-player or long-running capture behavior. Baseline H.264,
VP9/Opus and AV1 have declaration checks here; their actual decoding is not
tested by these two fixtures. HEVC, DRM and hardware browser decoding are not
established by this fix.

## Two-minute native GOFNS recordings

Both captures use actual `https://www.twitch.tv/gofns`, browser fullscreen,
unmuted volume 0.8, 1280×720/30 capture and native libx264/AAC recording to MP4.
Playwright controls CEF over private IPC. The proxy is inherited for external
browser networking; certificate validation remains enabled. There are no raw
media subscriptions, browser extensions, desktop capture, downloaded playlists
or Chrome fallback. Xvfb hosts CEF's display; video comes from CEF OnPaint.
PulseAudio supplies the browser audio service; samples enter through CEF's
native callback.

| Measurement | Before entry correction | Final executable |
| --- | --- | --- |
| Capture start IST | 16:57:04.489 | 17:07:16.801 |
| Finished IST | 16:59:05.697 | 17:09:17.884 |
| Requested / elapsed recording | 120 s / 120.001868 s | 120 s / 120.000436 s |
| Native video frames | 3,603 | 3,602 |
| Repeated native frames | 238 (6.61%) | 142 (3.94%) |
| Missing audio samples | 123,325 (2.569271 s) | 25,978 (0.541208 s) |
| Overflow / late audio samples | 0 / 0 | 0 / 0 |
| Final audio sample count | 5,764,096 | 5,764,096 |
| Maximum measured encode work | 326.188 ms | 256.389 ms |
| Adaptive buffer delay | 269.731 ms | 200.936 ms |
| Peak sampled video queue | 74,649,600 bytes | 13,824,000 bytes |
| Peak sampled audio queue | 280,320 bytes | 34,560 bytes |
| Queued video/audio at stop | 0 / 0 | 0 / 0 |
| Sampled CPU seconds, process tree | 210.80 | 144.03 |
| Median process-tree CPU | 173.24% | 106.98% |
| Peak process-tree RSS sum | 2,468.93 MiB | 2,135.22 MiB |
| Browser dropped-frame increase | 1,270 / 6,796 total | 1,912 / 5,455 total |
| Stack-protector termination log lines | 27 | 0 |

These are **two different live-content runs, not a controlled performance
comparison**. Twitch adaptive quality changed: the first began at 1920×1080,
the final began at 1280×720, and both ended at 852×480 while the recording canvas
stayed 1280×720. Network scheduling, scene complexity and source quality vary.
The reduced missing-audio count and CPU use do not prove that the entry fix
caused those improvements. Browser decoder drops and native repeated-frame
counts are different measures and must not be added or equated.

Final missing-sample increases were observed in one-second status bins around
21–22 seconds (140 ms combined), 37 seconds (120 ms), and 77 seconds (280 ms),
plus about 1.2 ms at final drain. These are aggregate increments, not measured
individual acoustic gap lengths. Status sampling cannot attribute them to
CEF scheduling, Twitch's source, audio-service restart or engine buffering.
No raw PCM callback tracing ran during these captures. Detailed per-second
counters, host pressure/scheduling samples and resource roles are preserved.

RSS sums can double-count shared library pages. These captures do not have PSS
or private-allocation samples; sampled CPU can miss short-lived children. This
is not a leak audit, a 30-minute soak, a GPU test or a matched OBS comparison.
Prior source-audio and OBS evidence remains separate and unchanged. Full
sanitizer coverage of this prebuilt CEF/Chromium dependency remains unverified;
the previous stock-154 sanitizer abort must not be counted as a 152 result.
The persistent-source-audio CEF patch targets stock 154 and has not been rebased,
built or deployed in this codec-enabled 152 distribution.

## Delivered recording and validation

Final viewing copy: `/workspace/shared/gofns-suitecut-native-fixed-2-minutes.mp4`.
It is remuxed from the native recording with copied tracks and faststart; no
video/audio re-encoding or fallback capture is involved. It is 120.021354 seconds,
H.264 1280×720 at approximately 30 FPS plus 48 kHz stereo AAC. Complete audio
and video decode with FFmpeg `-xerror` passed; measured overall audio RMS is
−31.03 dBFS. Start/middle/end stills and both tracks' metadata are saved.
Final clip SHA-256:
`ba511be2a307f0ad5efde45ffd9e948821b21f23d70c130d127e610824fa5cec`.

Final checks: **7/7 native CTests, 15/15 real CEF integration tests, both actual
MP4/MSE playback recordings, full Twitch audio/video decode, installer/runtime
pin rejection controls, SDK build, TypeScript check, ESLint, formatting and
whitespace checks passed**. Unit tests for unchanged SDK implementation were
not rerun in this codec-only follow-up; earlier SDK test evidence remains in the
memory-hardening report. Integration coverage includes scrolling/input,
recording/streaming readiness, silent pages, blocked-Node pause, page/context
switches, permissions/geolocation, session limits, popup lifecycle and shutdown
with pending output work. GPU encoding remains unverified on this host.

The initial configure attempts used an incorrect private FFmpeg sysroot and
then lacked the private X11 link path. These setup failures and corrected build
logs are retained; the final configure/build succeed. The stock missing-codec
test and distribution mismatch controls fail intentionally, as documented.

## Reproduction and saved evidence

From the repository checkout, install the pinned SDK and build following
`native/browser/ENGINE.md`. This host additionally uses its existing private
FFmpeg and display dependency sysroots, recorded in the saved CMake cache.

```sh
pnpm native:install:cef
pnpm build
SUITECUT_NATIVE_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser" pnpm test:native:playback
ctest --test-dir .suitecut/native-build --output-on-failure

MEMORY_DIRECTORY="$PWD/.suitecut/playback-check" \
SUITECUT_RUNTIME_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser" \
python3 benchmarks/native-memory/runtime.py benchmarks/native-playback/playback.mjs

MEMORY_DIRECTORY="$PWD/.suitecut/twitch-check" \
SUITECUT_RUNTIME_EXECUTABLE="$PWD/.suitecut/native-build/suitecut-browser" \
python3 benchmarks/native-memory/runtime.py benchmarks/native-playback/twitch.mjs
python3 benchmarks/native-playback/summarize.py .suitecut/twitch-check
python3 benchmarks/twitch-capture/verify.py .suitecut/twitch-check /absolute/path/to/gofns.mp4
```

The evidence includes source/SDK adapter snapshots, exact tested executables,
CEF SDK archive/provenance/digests, configurations, failed and passing logs,
fixtures, decoded PCM, full native recordings, screenshots, final viewing copy,
raw resource/queue samples and summaries. `checksums.sha256` seals all payloads.
The self-contained archive is
`/workspace/shared/suitecut-native-playback-codecs-2026-10-10.tar.gz`; its SHA and
verification metadata are saved beside it. Prior evidence integrity is recorded
in `previous-seals.json` and `previous-archives.json`. No packages or releases
were published.
