# Changelog

## Unreleased

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
