# Changelog

SuiteCut follows [Semantic Versioning](https://semver.org/). This file records user-visible changes.

## Unreleased

- Publish selected pages over RTMP or RTMPS with bounded frame retention, automatic reconnect,
  live visual effects, and no source recording files. Live audio is silent AAC.
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
