// Published numbers from docs/benchmarks.md and native/client/VALIDATION.md.

export const captureHost =
  'Apple M4, 16 GB, macOS 27.0.1, commit 4694aa1, 2026-10-23. IOSurface capture, Metal NV12 conversion and h264_videotoolbox MP4 with 48 kHz page audio, a new canvas every frame and five CPU load workers.'

export const captureRuns = [
  {
    run: '120 s',
    frames: '14,403',
    fps: 119.07,
    held: '112 (0.78%)',
    audio: '0 ms longest silence',
    load: '3.9–8.4',
  },
  {
    run: '60 s',
    frames: '7,203',
    fps: 119.25,
    held: '45 (0.62%)',
    audio: '0 ms longest silence',
    load: '8.0–11.4',
  },
  {
    run: '10 s, A/V sync',
    frames: '1,203',
    fps: 120,
    held: '0',
    audio: '8.33 ms delay, 0.995 agreement',
    load: '9.6–10.2',
  },
]

export const clientSetup =
  'Same pinned CEF binary, 1280×720 at 60 FPS, deterministic fixture. 30 samples per small operation, six 256 KiB evaluations and 50 context/page cycles.'

export const clientRuns = [
  { measure: 'Launch to ready', native: 187, unit: 'ms' },
  { measure: 'Small evaluation', native: 0.5, unit: 'ms' },
  { measure: '256 KiB evaluation', native: 7.43, unit: 'ms' },
  { measure: 'Locator count', native: 11.92, unit: 'ms' },
  { measure: 'Locator click', native: 49.99, unit: 'ms' },
  { measure: 'Locator fill', native: 11.33, unit: 'ms' },
  { measure: 'Peak client memory', native: 3.63, unit: 'MiB' },
]

export const reproduceCapture = `python3 scripts/verify-native-macos-recording.py \\
  --installation '/path/to/relocated installation' \\
  --output /new/output/dir --format mp4 \\
  --duration-ms 120000 --case 1920x1080@120:audio --load-workers 5 \\
  --required-distinct-fps 120 --require-no-recorder-drops --require-native-capture \\
  --require-source-continuity`
