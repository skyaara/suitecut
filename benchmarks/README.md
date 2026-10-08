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
