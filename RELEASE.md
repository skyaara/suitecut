# SuiteCut 2.1.0 release preparation

This release is prepared locally and **unpublished**. The npm package contains
TypeScript SDKs, native C++ source, and checksum pins. It does not include a compiled
CEF browser or its runtime libraries. Build and keep the complete browser bundle
as described in [the engine guide](native/browser/ENGINE.md#build), then set
`SUITECUT_NATIVE_EXECUTABLE`. Linux x86-64 with software graphics is the verified
native target; the new macOS media integration and Windows packaging are not verified.

## Changes

- Native C++ CEF capture, FFmpeg-library recording, streaming, checkpoint processing,
  and presentation rendering; TypeScript supplies lifecycle, automation and timeline plans.
- Real Playwright browser/page/locator objects through private CEF CDP transport,
  plus `suitecut/engine/test`. External browser automation through `connectBrowser()`;
  external pages cannot be passed to the embedded engine's recording outputs.
- Linux codec-enabled CEF 152.0.6 / Chromium 152.0.7977.83, with pinned archive and
  runtime-library digests, for H.264/AAC browser playback.
- Selectable H.264, HEVC, VP8, VP9, AV1 and FFV1 recording encoders with AAC, Opus,
  FLAC or PCM audio and validated container pairings. Conventional RTMP uses H.264/AAC/FLV.
  Linked GPU encoders require suitable hardware; GPU performance is unverified here.
- Common audio/video epoch, mandatory native output readiness, bounded queues,
  worker progress coordination and adaptive buffering. Encoder stalls wait for the
  delayed track while retaining queued PCM within the documented limits.
- Lifecycle ownership, malformed-buffer checks, scaler-failure cleanup and concurrent
  stop/pause protections, backed by sanitizer regressions and coverage-guided fuzzing.

Existing root recording defaults remain native, 1920×1080/60 FPS, and page audio
opt-in. The low-level engine defaults to audio-enabled output. Stock CEF can start
its audio capture only after the page becomes audible. Silent pages get timestamped
zero PCM; readiness cannot restore source sound emitted before CEF starts capturing it.
The root presentation facade retains CSS locators; use `suitecut/engine` for the
real Playwright API. The explicit legacy backend remains `suitecut/playwright`.

## Local artifacts and verification

`pnpm pack:release` builds the SDK and all eight audio packages and creates nine
2.1.0 tarballs, file inventories, SHA-256 checksums and `.suitecut/release/latest.json`.
It does not publish, push, tag or create a GitHub release. Install the main artifact:

```sh
npm install ./suitecut-2.1.0.tgz
export SUITECUT_NATIVE_EXECUTABLE="/absolute/path/to/suitecut-browser"
```

Split audio plugins depend on the matching 2.1 core package. To install VITS
from the prepared release assets, supply both plugin tarballs with the main tarball:

```sh
npm install ./suitecut-2.1.0.tgz ./suitecut-audio-sherpa-core-2.1.0.tgz ./suitecut-audio-vits-2.1.0.tgz
```

The final release evidence is saved in the repository under
`benchmarks/release/2026-10-11-suitecut-2.1/`. It records native source hashes,
sanitizer settings, tests, fuzz logs/corpora and package verification. The release
checks are `pnpm prepublishOnly`; installed native recording is separately checked
by `pnpm test:release:package` with a working display/audio service and the native executable.
The default native CEF SDK installer ships at `scripts/install-cef-codecs.py` inside
the package. Source-checkout build/test helpers are not included in the npm artifact.

## Known quality and coverage limits

The earlier sealed resolution matrix covers separate recording and streaming at
720p60, 1080p30 and 4K30, plus a 30-minute 1080p30 stream. On this software-rendered
host, the long output contained **15.03 seconds of quiet audio**, with a longest
interval of **275 ms**. A matched OBS embedded-CEF control measured **15.02 seconds**
and **280 ms**. Both had continuous AAC packet timestamps. These observations do
not locate every quiet interval's cause or demonstrate a fix. Video repetition,
source startup loss and bounded queue overflow under overload remain possible.

The detailed evidence is in `benchmarks/native-sync/2026-10-11-shared-clock/`
and `benchmarks/obs-cef/2026-10-11-quietness-control/`, with standalone visual reports
and preserved raw recordings. Resource numbers apply to this fixture and host;
4K quality and throughput differ substantially between the implementations.

Sanitizers cover exercised SuiteCut native code and allocation leak checks. Prebuilt
Chromium and codec libraries are not fully instrumented. A passing fuzz campaign,
or stable memory in a bounded run, cannot establish universal memory safety or
prove there are no leaks in indefinitely long sessions. The earlier 30-minute
SuiteCut profile had a +1.04 MiB/min post-warmup summed-RSS trend; its source is
unresolved. A per-process analysis attributes about +0.61 MiB/min to the CEF/native
host and +0.22 MiB/min to the benchmark controller; it does not identify allocation
causes. Chromium-wide sanitizer coverage, GPU devices, remote RTC transport,
and the new engine's macOS release packaging require further validation.
