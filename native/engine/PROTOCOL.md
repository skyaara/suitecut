# Complete native backend API (protocol v1)

Framing, browser launch, CDP forwarding, packet bounds and shutdown are defined
in `BROWSER-PROTOCOL.md`. The complete backend advertises `nativeMedia:true`
in its `ready` handshake. This package also implements the media commands below.
SDKs send control JSON; C++ performs capture, buffering, encoding and muxing.

## Record a playable audio/video file

After creating/navigating a page through CDP, obtain its `targetId` and send:

```json
{"version":1,"id":100,"command":"media.start","targetId":"page-1","destination":"/absolute/path/demo.mp4","width":1920,"height":1080,"fps":30,"audio":true,"streaming":false,"videoCodec":"libx264","audioCodec":"aac"}
```

The native response JSON is:

```json
{"outputId":100,"audioReadinessRequired":true,"audioReadiness":"engine"}
```

That response acknowledges output creation. Poll `media.status` until `ready`
is true; surface `error` immediately. Native audio readiness is mandatory when
audio is enabled (including when the `audio` field is omitted). A silent page
can initialize a valid silent audio track and accept later sound. Initializing
the engine does not establish that the page is already playing audible content.
Use SDK-side request/startup deadlines and stop a failed or timed-out output.

```json
{"version":1,"id":101,"command":"media.status","outputId":100}
```

Stop to flush the encoders and finalize the container:

```json
{"version":1,"id":102,"command":"media.stop","outputId":100}
```

The stop response contains final status. On success the file at `destination`
is ready to read or play with its video and audio tracks. File bytes are written
directly by C++; they are not carried as a stdout packet. A language SDK can
return that path or read/upload the file after stop. MP4 must be finalized before
the SDK treats it as a complete recording.

## Streaming and lifecycle

| Command | Input | Result |
| --- | --- | --- |
| `media.start` | Output configuration below | `outputId`, engine audio-readiness policy |
| `media.status` | `outputId` | Readiness, timing, counters, queue sizes and error |
| `media.pause` | `outputId`, `paused`: boolean | Status after draining/rebasing pause or resume |
| `media.select` | `outputId`, `targetId` | Switches the output's source page |
| `media.stop` | `outputId` | Final status after encoder flush/muxer close |
| `media.encoders` | None | `encoders`: descriptors with name/kind/codec/linked/hardware/lossless/containers |
| `media.encoder` | `name` | `available`: boolean |
| `media.probe` | `input`: absolute file path | Native media metadata and stream descriptors |

Streaming uses `media.start` with `streaming:true`, an `rtmp://` or `rtmps://`
destination, and compatible H.264/AAC encoders. C++ muxes FLV and publishes it
directly. A request failure or later `status.error` must be propagated to the
caller. A muxed network stream is not raw frame/audio subscriptions.

All commands have the normal native request ID. **Reserve an active output's
ID until stop completes:** `outputId` equals its `media.start` request ID.
IDs must remain unique across active outputs and outstanding commands, including
outputs in other page targets. Do not retry `media.start` on timeout using the
same ID; clean up/close the source when its outcome is uncertain.
After successful stop, the output is removed; repeated native stop returns
`Unknown native output`. An SDK can cache the completed stop result to provide
an idempotent public stop method. Graceful stdin EOF stops active outputs too,
but an explicit stop is preferable when the SDK needs final status/file readiness.

### Output configuration

`destination`, `width`, `height`, and `fps` are required. Output dimensions are
even, 2–3840 × 2–2160; FPS is 30 or 60. Recording paths are absolute, with a
supported suffix or explicit container. `targetId` is optional and selects the
page; omission selects the initial root page.

Optional fields:

- `audio`: boolean, default true. `streaming`: boolean, default false.
- `videoCodec`, `audioCodec`: linked encoder names discovered with `media.encoders`.
- `container`: `mp4`, `mov`, `webm`, `mkv` or `matroska`; streaming uses FLV.
- `crf`: encoder-appropriate quality value; omit for default behavior.
- `bitrateKbps`, `audioBitrateKbps`: target bitrates.
- `audioStartupTimeoutMs`: integer 1–60000, default 5000.

`audioStartup` is rejected; callers cannot bypass mandatory native readiness.
Codec/container incompatibility produces a command error. Encoder availability
does not prove usable hardware. Source paint dimensions may differ from the
output dimensions; the C++ engine scales/converts before encoding.

### Status

Status reports `ready`, `stopped`, `paused`, `error`, `startedMs`, `durationMs`,
`videoFrames`, `audioSamples`, `repeatedFrames`, `droppedAudioSamples`,
`missingAudioSamples`, `activeMissingAudioSamples`, `lateAudioSamples`,
`queuedVideoBytes`, `queuedAudioBytes`, `maximumEncodeMs`, `bufferedDelayMs`,
`audioSyncWaits`, `videoSyncWaits`, `maximumTrackLeadMs`, `videoEncoderThreads`
and `videoEncoderPreset`. Audio sample counts are frames per channel at 48 kHz.
Durations/timing values are milliseconds. Missing coverage is distinct from
source-provided silent PCM. Queue overflow and source stalls can still degrade
content; continuous mux timestamps alone do not establish gap-free audio.

## Native offline file operations

- `media.cut`: `input`, `destination`, `width`, `height`, `fps`, `startMs`,
  `durationMs`. The current implementation encodes video using VP9; it is not
  a general purpose copy/remux API.
- `media.render`: `inputs` file paths, `destination`, FFmpeg filter `graph`,
  `fps`, `durationMs`, `audio`, `container`, `videoCodec`, `audioCodec`,
  `pixelFormat`, `fullRange`, `audioBitrate` (bits/sec), `videoOptions` (string
  map), and `jobKey`. The graph must produce `[video0]` and, for audio, `[audioout]`.
  Native status on completion contains `videoFrames` and `audioSamples`.
- `media.cancel`: `jobKey`, requests cancellation of a matching render job.

These are low-level native primitives. SuiteCut's narration/action-manifest
planner is a separate layer and is not included as a language SDK in this package.
Limits and failures are enforced by the engine; SDKs should validate before
dispatch, surface errors and avoid flooding the bounded command channel.
