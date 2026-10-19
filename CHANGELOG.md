# Changelog

## Unreleased

- Keep the native CEF recording and streaming APIs and their shared media pipeline.
- Accept integer capture/output FPS from 1 through 120 with timestamp-based hold/drop
  resampling and exact audio sample boundaries.
- Remove the legacy external browser backend, its dependencies, reporter/test
  integration, examples, generated demos and performance comparison tooling/data.
- Preserve native capture, media and lifecycle verification.

The pre-cleanup source is retained in the local checkpoint
`checkpoint/native-cef-120fps-before-playwright-benchmark-removal` at `582a17e`.
