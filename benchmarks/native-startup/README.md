# CEF version comparison and mandatory audio startup

The current engine pins CEF 154.0.34 and always waits for native source sound for
realtime audio-enabled outputs. `matrix.mjs` exercises this default without a
startup-policy field; immediate and packet-only policies are removed. Silent
flows require `audio: false`. New evidence is saved separately under
[2026-10-09-default](2026-10-09-default/report.md).

The historical [2026-10-10 evidence](2026-10-10/report.md) compares the original CEF 152.0.8,
patched CEF 152.0.13, and stable CEF 154.0.34 using the same SuiteCut implementation.
It measures cold-start audio, packet/signal startup gating, default AAC/MP4,
quiet and CPU-stressed capture, and the real Playwright compatibility checks.

Build a helper against the corresponding official CEF minimal SDK, with the
pinned libyuv and linked FFmpeg dependencies from `native/browser/ENGINE.md`.
`2026-10-10/cef-downloads.json` records exact newer SDK URLs and checksums. On this
host, also pass `-DCMAKE_EXE_LINKER_FLAGS=-L/absolute/obs-deps/root/usr/lib/x86_64-linux-gnu`
to use the private X11 development symlink. Runtime libraries must remain beside
the compiled helper; copying only the executable is insufficient.

```sh
SUITECUT_RUNTIME_EXECUTABLE=/absolute/build/suitecut-browser \
  STARTUP_DIRECTORY=/absolute/new/evidence \
  python3 benchmarks/native-engine/runtime.py benchmarks/native-startup/matrix.mjs

SUITECUT_RUNTIME_EXECUTABLE=/absolute/build/suitecut-browser \
  SUITECUT_ENGINE_E2E_DIRECTORY=/absolute/new/evidence/playwright \
  python3 benchmarks/native-engine/runtime.py playwright
```

Run captures serially without concurrent builds/tests. The runtime owns an
isolated X display and PulseAudio sink, enforces a single-run lock, and preserves
the Chromium sandbox. It can now select a helper with
`SUITECUT_RUNTIME_EXECUTABLE`; omitting it uses the ordinary native build.

The collector runs a new browser process/profile for each case and records the
CEF version reported by that helper. Its source and encoder PCM traces are
native local files, independent of SDK media transport. The existing independent
`benchmarks/native-audio/analyze.py` produces coverage and decoded-audio evidence
for each case directory. The comparison analyzer adds exact leading-signal and
decoded video-marker/resource measurements. See the saved report for scope and
limitations; this is not a complete upstream Playwright conformance suite.

Analyze a new default-policy matrix with full audio/video decodes:

```sh
mkdir -p /absolute/new/evidence/logs
python3 benchmarks/native-startup/analyze.py /absolute/new/evidence
```

Historical CEF 152 comparisons require the source snapshots in their sealed
evidence package; current CMake deliberately rejects SDKs different from the pin.
