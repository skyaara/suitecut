# GOFNS Twitch two-minute capture

Saved 2026-10-10T16:43:50.625053+05:30 (Asia/Calcutta).

The live channel https://www.twitch.tv/gofns was playing before capture began.
Final MP4: `/workspace/shared/gofns-twitch-2-minutes.mp4`.
Duration: 120.0 seconds. Video: H.264, 1280×720, 30 FPS.
Audio: AAC, 48 kHz stereo; average RMS -29.95 dBFS.
Full audio and video decoding passed. SHA-256: `ae2de579d5f3ba4f5db5dec1af4b74e1bfd632a787e29f5e085a257ad57e7e53`.

The delivered recording uses direct X11 display and PulseAudio monitor capture
of sandboxed Playwright Chrome 151.0.7922.34, encoded with FFmpeg.
It is **not a successful SuiteCut native CEF or legacy capture-adapter test**.
The actual display includes the Chrome toolbar and clips the bottom of its
virtual viewport; page screenshots alone do not establish display composition.
The saved display-root image and extracted start/middle/end frames show the
actual recording. No stream playlist/segment download was used.

## Capture and playback observations

The encoder ran for 120.384 seconds. Playback time advanced
from 5.761 to 126.422 seconds. Twitch's selected source
was 1280×720; the player indicated 720p60.
The delivered display recording is 30 FPS. Player decoded-frame count increased
from 230 to 7469; reported dropped frames
increased from 6 to 327.
Those are browser playback counters, not dropped-frame measurements for SuiteCut.
The natural live-stream audio is not a continuous reference tone; audio energy
and successful decoding do not establish absence of short audio gaps or sync error.

## SuiteCut issues exposed by this real stream

The first native CEF load reached the live channel but returned Twitch error
#4000 and `No playable format`, with video size 0 and no decoded playback.
The separately recorded codec query returned empty support for H.264 and AAC,
while VP9/Opus and AV1 returned `probably`. This is **browser playback support**,
which is separate from the C++ output encoder's H.264/AAC support.
The standard CEF build cannot play this Twitch source as configured.

A later repeated native codec probe stalled and its saved stderr contained
`stack smashing detected`. That owned attempt was terminated. No cause has been
established; this is additional evidence against declaring full CEF memory safety.

Chrome successfully played the stream. Attempts to capture it through SuiteCut's
older adapter timed out initializing a screencast or produced a 0.106-second
RTMP output and stalled before recording readiness. Direct display/audio capture
was used to fulfill the requested clip. Proxy bypass was configured for loopback
services in the later attempt, and failed logs were preserved. The adapter failure
has not been diagnosed or fixed; the successful fallback does not close that issue.

All capture scripts, raw output, player state, screenshots, encoder logs,
failed-attempt logs, codec results and media verification are saved here.
Historical sealed benchmarks were not changed.
