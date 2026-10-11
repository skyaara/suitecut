# Benchmarks

Run benchmarks from a source checkout with Node.js 22 or newer and
`pnpm install --frozen-lockfile`. Commands build the code before measuring it.
Reports include runtime and host information. Compare the same workload on the
same idle machine, use repeated trials, and keep cold startup separate from
steady-state work. These are diagnostic measurements, not CI speed thresholds.

## FLV framing

```sh
pnpm benchmark:flv
pnpm benchmark:flv --trials 7 --output .suitecut/benchmarks/flv.json
```

No browser or FFmpeg installation is required. The benchmark covers 1 KiB,
16 KiB, 256 KiB, 1 MiB, and 4 MiB payloads with 1 KiB, 16 KiB, and 64 KiB input
fragments. Each case has one warmup and repeated measured trials. It reports
median and p95 trial duration, per-tag time, and payload throughput. Reconstructed
payloads and tag counts are checked outside the timed section.

This measures framing, allocation, and copying. It does not measure encoder or
network performance. The large cases expose fragmented-keyframe costs; they
should not be presented as typical stream traffic or an end-to-end speedup.
Small unfragmented frames may see little benefit from parser changes.

## Media-probe finalization

Install FFmpeg and FFprobe or run `pnpm build && node dist/cli.js install` first.
`SUITECUT_FFPROBE_PATH` can select a specific executable.

```sh
pnpm benchmark:media
pnpm benchmark:media --trials 5 --artifacts 48 --output .suitecut/benchmarks/media.json
```

This creates local PCM WAV fixtures, starts real FFprobe processes, and compares
unbounded dispatch with the production batches of four. It checks metadata,
artifact ordering, empty input, and rejection for a missing file. All temporary
fixtures are removed in `finally`; a report is retained only with `--output`.

Process startup is included; fixture creation is excluded. The baseline deliberately
starts up to `--artifacts` concurrent processes (24 by default, maximum 128).
The production limit bounds resource demand and can trade throughput for a smaller
process burst. Results for short local WAV files do not predict long videos or
network storage. Concurrent recording attempts each have their own four-process limit.

## Native and Playwright capture

The existing backend benchmarks measure distinct recorded FPS, repeated frames,
longest held frame, startup/finalization time, process-tree CPU, RSS, and output
size. Install Playwright Chromium and build the pinned CEF executable following
[the native build instructions](../native/browser/README.md). Set
`SUITECUT_NATIVE_EXECUTABLE` to that executable.

```sh
pnpm benchmark:suite --trials 3
pnpm benchmark:beta --trials 3 --width 3840 --height 2160 --fps 60 --duration-ms 30000
```

The suite covers static pages, canvas motion, DOM motion, page switching, and
4K30. The second command explicitly exercises 4K60. These benchmarks currently
sample processes on macOS/Linux; macOS ARM64 is the verified native target.
Generated recordings and reports go under ignored `.suitecut/` directories.

Avoid inferring a sustained 4K60 improvement from a removed memory copy alone.
Compare complete capture runs, including popup composition, before publishing a
native throughput claim.

## CEF and OBS capture

The [saved CEF / OBS comparison](obs-cef/README.md) includes Linux setup scripts,
repeated recording trials, scrolling and browser controls, audio checks, local
RTMP tests, and bounded streaming soaks. Its report retains failures, decoded
motion measurements, resource samples, and partial media evidence. OBS captures
external Chromium with XSHM because the installed package has no browser-source
plugin. Recording codecs differ; compare the documented pipelines and settings
before interpreting performance.

## CEF optimization evidence

The follow-up profiling, recorder/lifetime fixes, repeated OBS comparison, and
continuous recording/streaming memory evidence are saved separately in
[2026-10-09 optimization](obs-cef/2026-10-09-optimization/report.md).
The original CEF/OBS measurements remain the before-change evidence.

The C++ media migration and its new evidence are documented in
[native-engine/README.md](native-engine/README.md). Older OBS and dropout evidence
remains unchanged.

The [native audio gap investigation](native-audio/2026-10-10/report.md) saves
source/encoder PCM traces, startup timing, missing-input counters, and scheduling
controls. It distinguishes input continuity from queue-overflow drop counts.

The [CEF version and startup comparison](native-startup/README.md) adds native
audio-readiness gating and compares the original build with a newer patch and
stable release under matched capture and Playwright checks.

The [CEF 154 promotion and mandatory readiness evidence](native-startup/2026-10-09-default/report.md)
removes selectable audio startup policies, verifies the default with actual
recording/RTMP and Playwright flows, and saves both clean continuous capture
and the unresolved intermittent-audio simultaneous failure.

The [native synchronization fixes and detailed evidence](native-sync/2026-10-10/report.md)
cover silent-page readiness, capture-clock mapping, independent encoding workers,
adaptive buffering, pause/stop draining, encoder CPU limits, and fully decoded
recording/RTMP controls. Source gaps and the unbuilt persistent-capture CEF patch
remain explicit limitations; container selection alone cannot restore missing input.
