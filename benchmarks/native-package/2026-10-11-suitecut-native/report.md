# Complete SuiteCut native package 2.1.0

The complete native backend is prepared locally. Unlike the optional CEF-only
component, it advertises `nativeMedia:true` and includes CEF, capture, the native
shared A/V clock, audio/video workers, FFmpeg encoding, muxing, finalized files
and RTMP/RTMPS publishing. SDKs send commands; they do not perform media encoding
or synchronization. No Python SDK was added.

Direct black-box tests sent plain stdin commands without Node or Playwright
and verified finalized outputs with independent ffprobe and ffmpeg decoding:

| Output | Video + audio | First packet timestamp difference | Track end difference |
| --- | --- | --- | --- |
| MP4 | H.264 + AAC | 0 ms | 7.98 ms |
| Matroska | FFV1 + PCM | 0 ms | 14 ms |
| WebM | VP9 + Opus | 7 ms | 8 ms |
| RTMP receiver FLV | H.264 + AAC | 21 ms | 24 ms |

All four outputs contained 640×360 video and audible 48 kHz stereo audio, had
monotonic per-track packet timestamps, decoded successfully, and stopped with
empty native input queues. The API tests also checked engine readiness, native
metadata probing, pause/resume, rejected startup-policy bypass and incompatible
codec/container errors. Recorded sample durations are approximately 2.5 seconds;
these measurements are container timestamp alignment, not a lip-sync or
long-duration source-continuity measurement.

All eight native CTest groups passed after enabling relocatable runtime lookup.
All 16 Playwright/media integration tests passed against the staged complete
runtime, including actual recording/streaming and shutdown/pause/target lifecycle.
The shared-library staging check verified that each bundled media dependency
resolves from the runtime's own `lib/` directory. No FFmpeg command-line process
is used by the production host; ffmpeg/ffprobe are independent test tools only.

The Linux runtime includes 125 additional media libraries (190,388,536 bytes),
their SHA-256/package/source-version inventory, Debian copyright notices and
common license texts. CEF and its licenses/resources are also included. System
Chromium/display/audio libraries still need a compatible Debian 13 environment.
The native executable uses `$ORIGIN:$ORIGIN/lib`, with no current-directory or
build-SDK rpath. The old SuiteCut 2.1.0 SDK and CEF-only archives remain unchanged.

Detailed stdout/stderr logs, native statuses, ffprobe metadata, original output
files, source hashes and build information are retained. All live checks ran
serially with four-core affinity, private Xvfb and PulseAudio. These functional
checks claim no 8 GB kernel cap or verified GPU encoder support.

This change packages the already implemented media engine; it does not claim
new whole-Chromium leak coverage, full Playwright conformance, or a resolution
of previously observed long-running audio quietness. macOS runtime packaging
and Windows delivery remain unverified. No external publication occurred.
