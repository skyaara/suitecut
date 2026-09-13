# Changelog

SuiteCut follows [Semantic Versioning](https://semver.org/). This file records user-visible changes.

## Unreleased

## 1.1.0 - 2026-09-13

- Add `capture.deviceScaleFactor` for device-pixel supersampling without changing CSS viewport geometry, with Lanczos downscaling for recordings and streams.
- Add optional action zoom to highlights, hovering, and clicks, with camera operations queued through action completion.
- Match recorded cursor shapes to links, buttons, text fields, and disabled controls using bundled macOS-style artwork.
- Export word-timing artifact validation and timing types from the root package.
- Update installation, streaming, and API documentation for managed media tools, tab audio, adaptive bitrate, diagnostics, and action zoom.

- Keep the RTMP publisher and AAC encoder alive during bitrate changes. Prepare replacement H.264 encoders and switch at keyframes on a shared timeline, with bounded framing and continuous audio. Verify one-connection handoffs, timestamps, decoder output, and failed replacement recovery.

- Adapt live bitrate to measured network socket backpressure with bounded transport buffers, video encoder handoffs, and gradual recovery. Report pending writes and require output progress before declaring recovery.

- Recover from temporary live congestion without restarting the encoder, bound audio/video retention, and report stage timings through `stream.onDiagnostic`. Escalate repeated congestion and reconnect after 20 seconds without output progress.

- Add `suitecut install` for per-platform, checksum-pinned FFmpeg and FFprobe, plus an offline `suitecut doctor` check. Preserve custom and system tool paths and remove the Playwright FFmpeg fallback.

- Publish selected pages over RTMP or RTMPS with bounded frame retention, automatic reconnect,
  live visual effects, and no source recording files. Audio defaults to silent AAC; `stream.audio: 'tab'` captures real Chromium tab output.
- Capture tab audio through an isolated extension in headed or headless Chromium, with timestamped
  PCM over a persistent local WebSocket, bounded producer and receiver queues, pause silence,
  page selection, and fresh audio on reconnect.
- Verify flash/beep synchronization and audio lifecycle through local RTMP in platform CI.
- Wait for popup capture startup before pausing for narration to avoid first-frame timeouts.
- Preserve terminal reconnect errors at shutdown so Playwright Test teardown reports failed streams.
- Document streaming options and verify receiver restart recovery in CI.
- Use the pnpm lockfile in CI and build GitHub Pages with the repository base path.

- Make the standalone Playwright recorder the primary `suitecut` entry point and move the optional
  Playwright Test fixture to `suitecut/test`. Keep `suitecut/playwright` as a compatibility alias.
- Persist page-selection events so renders return to the opener after a popup closes.
- Write `schemaVersion: 1` in manifests while continuing to read unversioned pre-v1 manifests.
- Redact local paths from render reports and write manifests, reports, and Kokoro WAV files atomically.
- Default renders to 1920x1080, 30 fps, and the standard quality profile.
- Pin optional Kokoro voices to the documented upstream revision, time out stalled downloads, report
  slow local startup, and add a real inference release check.
- Add a minimal code-first homepage, social preview metadata, higher muted-text contrast, and
  reliable static site browser tests.
- Build packages once, pack without lifecycle scripts, validate audio tarballs in parallel, and add
  macOS and Windows smoke jobs.

## 1.0.0 - 2026-08-24

- Record Chromium, Firefox, and WebKit pages through Playwright screencasts.
- Run through either Playwright Test or a plain Playwright `record()` callback.
- Capture narration, captions, checkpoints, highlights, pointer actions, scrolling, holds, and zooms.
- Render MP4, WebM, MOV, and Matroska output through local FFmpeg.
- Run Kokoro narration locally with the bundled `af_heart` voice.
- Load optional audio model packages through the worker-isolated audio plugin API.
- Provide separate Sherpa-ONNX packages for VITS and Piper, Matcha, Kokoro, KittenTTS, ZipVoice,
  Pocket TTS, and Supertonic models.
- Validate saved manifests, media metadata, public options, and reporter input at runtime.
