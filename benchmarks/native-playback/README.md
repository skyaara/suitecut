# Native browser playback verification

These checks distinguish CEF's browser decoders from SuiteCut's output encoders.
The Linux codec-enabled distribution is pinned in
[`cef-build-linux-codecs.json`](../../native/browser/cef-build-linux-codecs.json).
Install it with `pnpm native:install:cef` and follow the
[native build guide](../../native/browser/ENGINE.md#build).

```sh
SUITECUT_NATIVE_EXECUTABLE=/absolute/path/to/suitecut-browser pnpm test:native:playback
```

`playback.mjs` creates actual progressive High H.264/AAC MP4 and fragmented Main
H.264/AAC MSE fixtures. It verifies declared codec support, advancing decoded
video, nonzero native AAC capture, and decoded audio/video in finalized C++
FFV1/PCM recordings. A stock codec-disabled SDK must fail. VP9/Opus and AV1 are
checked for declared support; this fixture does not decode those formats.

`twitch.mjs` records the live GOFNS browser player through CEF and native libav,
with Playwright controlling the player over private IPC. Its default duration is
120 seconds. `SUITECUT_TWITCH_DURATION_MS` permits 5–120-second diagnostics. It
saves page state, screenshots, native output counters and process-tree samples.
It requires the channel to be live, a display/audio service and network access.
The hosted tests use the existing owned Xvfb/PulseAudio runtime wrapper; that
display hosts CEF and is not the video capture source.

`entry-repro.mjs` exercises repeated CEF context creation/closure. The diagnostic
`trace-stack.c` interposer records a fatal stack-protector backtrace and still
aborts; it is never linked into the production executable. The short diagnostic
runs did not reproduce the intermittent failure and are not proof of its cause.
The Linux dispatch correction instead follows upstream CEF's explicit
`NO_STACK_PROTECTOR` entry pattern and preserves protection on browser/media code.

`summarize.py OUTPUT_DIRECTORY` computes resource and timing summaries from saved
samples. No native diagnostic PCM tracing runs during the two-minute captures.
See the [October 10 report](2026-10-10/report.md) for exact versions, recordings,
remaining gaps, setup failures and reproduction commands. Historical OBS and
Chrome-fallback evidence remains separate.
