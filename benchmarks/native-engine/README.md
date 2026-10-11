# Native engine validation

This directory contains the C++ engine's real Playwright integration tests and
simultaneous recording/RTMP benchmarks. Saved results belong to their exact tested
binary and host; they are not guarantees about all Playwright features or machines.

- `engine.e2e.spec.ts`: ordinary Playwright Test fixtures against embedded CEF.
- `soak.mjs`: 1920×1080, 30 fps H264/AAC recording and RTMP, scroll automation,
  a deliberate four-second Node block, independent process sampling and cleanup GC.
- `recovery.mjs`: kills its own native helper mid-recording and independently decodes
  completed MP4 fragments; checks SDK failure notification.
- `runtime.py`: owns a private Xvfb display and PulseAudio null sink on this host,
  serializes benchmark sessions, and cleans up its own child processes.
- `2026-10-09/report.md`: implementation, validation, failed attempts, long-run
  media analysis and qualified comparison with the saved OBS control.

Build the helper and TypeScript package using `native/browser/ENGINE.md` first.
The host coordinator expects the private Xvfb/PulseAudio dependencies prepared by
the existing OBS benchmark setup under `.suitecut/obs-deps/root`.

```sh
python3 benchmarks/native-engine/runtime.py playwright
SUITECUT_VERIFICATION_DIRECTORY=benchmarks/native-engine/local-presentation \
  python3 benchmarks/native-engine/runtime.py scripts/verify-native-recording.mjs
SUITECUT_VERIFICATION_DIRECTORY=benchmarks/native-engine/local-stream \
  python3 benchmarks/native-engine/runtime.py scripts/verify-native-stream.mjs
SOAK_LABEL=my-run SOAK_DURATION_MS=1800000 \
  python3 benchmarks/native-engine/runtime.py benchmarks/native-engine/soak.mjs
python3 benchmarks/obs-cef/analyze-pipeline-investigation.py \
  benchmarks/native-engine/2026-10-09/my-run
```

The analyzer uses FFmpeg only to receive or decode validation media. SuiteCut's
native capture, publishing, checkpoint and render paths link libav directly.
Do not compile, decode other media or run competing tests during a comparison run.

The controller's `result.json.status` checks native counters and lifecycle.
`media-validation.json.status` additionally checks the actual decoded files. Both
must be read: an encoder reporting zero queue drops does not prove A/V sync or
absence of content stalls. Full logs and decoded marker/audio artifacts are saved.
