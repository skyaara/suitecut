# Benchmarks

Latest valid capture measurements for the CEF 156 engine
(`156.0.7+g47dbc1d+chromium-156.0.8078.17`). Each result names its commit,
host, workload and the command that reproduces it. Older numbers measured on
CEF 152, on the removed Playwright backend, or on short low-resolution runs are
not published here.

## Sustained 1080p120 recording on macOS

Measured 2026-10-23 at commit `4694aa1` on an Apple M4 (16 GB, 10 logical CPUs,
macOS 27.0.1, ARM64). The runtime was packaged and relocated with the documented
[macOS full-runtime flow](native-full-runtime-macos.md), ad-hoc signed.
Capture uses IOSurface, Metal NV12 conversion and `h264_videotoolbox` into MP4.
The page is the `reallocate` cadence fixture (a new canvas every animation frame)
with 48 kHz page audio. Five `nice 10` SHA-256 workers load the CPU for the
whole run.

| Run            | Encoded frames | Distinct FPS | Held (undelivered) paints | Coalesced | Audio                                      | Load average |
| -------------- | -------------- | ------------ | ------------------------- | --------- | ------------------------------------------ | ------------ |
| 120 s          | 14,403         | 119.07       | 112 (0.78%)               | 0         | 0 ms longest silence                       | 3.9–8.4      |
| 60 s           | 7,203          | 119.25       | 45 (0.62%)                | 0         | 0 ms longest silence                       | 8.0–11.4     |
| 10 s, A/V sync | 1,203          | 120.00       | 0                         | 0         | audio delay 8.33 ms, event agreement 0.995 | 9.6–10.2     |

Every encoded frame carried a valid paint ID and capture timestamps were exact.
The recorder dropped and coalesced nothing: the held frames are paints CEF never
delivered, which the recorder fills by holding the previous image. Recorder
preparation before the first active frame took 189–204 ms.

The strict sustained gate (`--required-distinct-fps 120`) did **not** pass on the
120 s and 60 s runs. The host was also running interactive desktop applications
beside the five load workers, so these results reflect a busy machine rather than
a quiet one.

For comparison, the same command on 2026-10-20 at commit `c75d579` (before the
2026-10-21 runtime modularization), on the same host model with load average
2.3–6.8, measured 120.00 distinct FPS over 120 s with 0 held paints and 115 ms
preparation. Whether the gap is host contention or a regression is not yet
established; re-run on an otherwise idle host before claiming either.

Reproduce:

```sh
python3 scripts/verify-native-macos-recording.py \
  --installation '/path/to/relocated installation' \
  --output /new/output/dir --format mp4 \
  --duration-ms 120000 --case 1920x1080@120:audio --load-workers 5 \
  --required-distinct-fps 120 --require-no-recorder-drops --require-native-capture \
  --require-source-continuity
```

Add `--audio-sync-proof` (with `--duration-ms 10000`) for the A/V sync row, and
`--fixture-mode steady` to measure drawing into a stable canvas instead. State the
fixture mode with any published number; the two are different workloads.

### Limits

- The recorder cannot restore images CEF never delivered. A 120 FPS container
  can still repeat images under CPU, GPU or I/O contention.
- macOS ARM64 only. Intel Macs, older macOS releases and notarized builds are
  not covered.
- Arbitrary page workloads, popup and live-pixel paths, heavier contention and
  longer captures are not qualified.
- Live streaming throughput is a separate path and is not covered by these
  results.

## Component measurements

[Native performance review](internals/native-performance-review.md) records paired
before/after component results (pixel conversion, frame fitting, HAR lookup,
worker event delivery, viewer encoding) measured on Linux on 2026-10-17 with
CEF 156. They measure specific helpers after optimisation work, not
whole-browser throughput, and are kept there with their own caveats.
