# Native continuous publishing

The CEF engine publishes a page live over RTMP, RTMPS or TCP. The engine does
the video and audio ingestion, frame-rate conversion, FFmpeg execution, pipe
backpressure and publisher lifetime; the caller only sends the `streaming`
command and reads its counters. It is available on Linux and macOS
(`native/browser/controllers/media/streaming_controller.inc`); other platforms
reject the command.

## The `streaming` command

From C++, send it through the page's generic command method; the C ABI sends the
same JSON object with `sc_client_call`:

```cpp
#include <suitecut/browser.hpp>

suitecut::LaunchOptions options;
options.executable = "/absolute/path/to/suitecut-browser";
options.frames_per_second = 60;
auto browser = suitecut::Browser::launch(options);
auto page = browser.main_page();
page.navigate("https://example.com/");

auto started = page.command("streaming", suitecut::Json{
    {"action", "start"}, {"clientToken", "stream-1"},
    {"ffmpegPath", "/usr/bin/ffmpeg"}, {"destination", "rtmp://127.0.0.1/live/key"},
    {"framesPerSecond", 30}, {"width", 1280}, {"height", 720}, {"audio", true},
    {"videoCodec", "h264"}, {"audioCodec", "aac"}, {"container", "flv"},
    {"bitrateKbps", 4500}});
const auto id = started.at("id");
// ... the page keeps running ...
auto status = page.command("streaming", suitecut::Json{{"action", "status"}, {"streamId", id}});
auto finished = page.command("streaming", suitecut::Json{{"action", "stop"}, {"streamId", id}});
```

| Action | Fields | Effect |
| --- | --- | --- |
| `start` | see below | Starts a publisher for this page; replies once encoded output is observed |
| `status` | `streamId` | Current counters, phase, selected page and output rate |
| `pause`, `resume` | `streamId` | Holds the image and sends silence while the live timeline continues; resumes ingestion |
| `select` | `streamId`, `sourcePageId` | Switches the source to another page in the same engine |
| `stop` | `streamId` | Waits for the encoder to finish and reports the final counters |
| `cancel` | `streamId` | Cancels the publisher and reports native cancellation |

`start` requires `clientToken`, `ffmpegPath`, `destination`, `framesPerSecond`,
`width`, `height`, `audio`, `videoCodec`, `audioCodec`, `container` and
`bitrateKbps`. Any other field is rejected. Optional fields are
`captureLatencyMs`, `maxWriteLagMs`, `tcpSendBufferBytes` and `reconnect`.

- **Codecs:** `h264`/`aac` in `flv`, or `vp9`/`opus` in `webm`. RTMP and RTMPS
  need H.264/AAC in FLV; `tcp://` destinations take either pair.
- **Rate:** capture takes any integer rate from 1 upward (the launch option
  `frames_per_second`), and CEF applies its own ceiling. The output rate accepts
  any finite value above 0, including 29.97. Held output slots are counted
  separately from incoming frames.
- **Bitrate:** fixed for the life of the publisher. Runtime bitrate changes and
  switching to a page in another engine are not supported.

Encoded and muxed counters show local FFmpeg progress, not receipt by a remote
service. AAC decoding can include codec priming and padding. Graceful
page/context/browser closure finalizes the publisher; crashes and forced
termination report failure.

## Reconnecting

Without `reconnect` the engine makes one publisher attempt. A `reconnect`
object enables C++ reconnect with `maxAttempts` (1 or more replacements per
outage, default 3), `initialDelayMs` (0 or more, default 250), `maxDelayMs` (at
least the initial delay, default 5000) and `timeoutMs` (1 or more, default
30000), all integers. Invalid values reject before admission.

The first encoded-readiness command keeps its original deadline across
replacements. A later outage budget starts at the observed network or child
failure, including physical child cleanup. Backoff, wakeups and retries run in
C++. A successfully encoded replacement resets the outage budget. Cancellation,
page crash, owner closure, malformed configuration and pipe-lag failures remain
terminal. Before first readiness, a child failure is retryable only when its
diagnostics identify a network failure. An encoder is reaped before its
replacement is launched. Linux process identity descriptors prevent signaling a
recycled PID; a kernel supporting `pidfd_open` and `pidfd_send_signal` is
required.

Between attempts the phase is `reconnecting`, with `physicalEncoderPid` zero
only after the old child exits. Output counters, duration and media origin
describe the current attempt; `cumulative*` counters sum the attempts.
`firstCapturedTimestampMs` keeps the real capture time of the retained image
separately from the replacement's output origin. Every entry in `attempts` keeps
its PIDs, timestamps, local counters, child settlement and diagnostics,
unclipped. Pictures arriving during an outage keep only the latest image, and
PCM captured while disconnected is dropped and counted in
`disconnectedVideoFrames` and `disconnectedAudioFrames`. Those counters describe
captured input discarded offline, not all bytes lost in a failed remote
publication. suitecut sets no publisher-count, picture-byte or PCM bound, during
reconnection or otherwise.

## Timing and counters

The audio-enabled publisher buffers 120 ms of captured media by default;
`captureLatencyMs` accepts integers from 0 upward. Video slot `n` selects a real
picture captured at or before that slot's timestamp, and waits for its complete
interval before submission. PCM advances independently in 20 ms chunks on the
same 48 kHz timeline. Encoded timestamps keep the captured origin.
`captureLatencyMs` and `videoIntervalClosureMs` describe this policy;
`maxCaptureToPipeMs` measures local pipe submission delay, not remote latency.
`maxWriteLagMs` (default 2000) fails a publisher whose pipe falls that far behind.

Pause/resume and selection take effect at their native command timestamps.
Media already buffered keeps its original meaning. Audio not available by its
playout time becomes counted silence (`audioGapFrames`); late overlap, pause,
selection discards and stop trimming have their own counters
(`audioOverlapFrames`, `pausedAudioFrames`, `switchedAudioFrames`,
`stoppedAudioFrames`, `stoppedVideoFrames`). A final partial interval is trimmed
when stopping. Exact sample counts do not by themselves establish continuity or
synchronization.

suitecut does not limit picture payloads, including held and in-flight
references, the number of publishers, or the PCM queue; `capturedFrameBytes` and
`peakCapturedFrameBytes` count captured-image bytes per publisher. Pipe-lag
overflow fails explicitly. CEF surfaces, process allocations and FFmpeg memory
are additional.

Child-page renderer failure is isolated from healthy pages in the same engine.
Recording and publishing keep their encoder ownership until the process exits
and the socket closes, and `status`, `stop` and `cancel` stay available after a
renderer failure. A generic engine or protocol error, and failure of the initial
root page, remain fatal.

## Limits

A decoded-window audit still exposes tone dropouts, and low-rate Opus startup
can fail; continuous audio and A/V synchronization remain under repair. Live
streaming throughput is not covered by the [benchmarks](benchmarks.md).
