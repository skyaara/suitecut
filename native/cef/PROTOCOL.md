# SuiteCut native protocol v1

This protocol works over inherited process stdin/stdout on a local machine.
It is not an HTTP server or an in-process ABI. Stderr contains diagnostics and
must be drained separately. JSON uses UTF-8. Unknown additive fields can be
ignored; reject unknown protocol versions and unknown packet types.

## Launch

Start an absolute executable path with separate argv entries, without a shell:

```text
--suitecut-width=1920
--suitecut-height=1080
--suitecut-scale=1
--suitecut-fps=30
--suitecut-pixel-format=bgra
--suitecut-graphics=auto
--suitecut-webrtc-policy=default
--suitecut-profile=/absolute/private/temporary/directory
--suitecut-frame-subscriptions
```

Width/height are CSS pixels, minimum 2; physical dimensions after scale must
not exceed 3840×2160. Scale is 1 or 2; FPS is 30 or 60. Pixel format is `bgra`
or `i420`; I420 requires even CSS dimensions. Graphics is `auto` or explicit
`swiftshader` (the latter opts into Chromium's reduced-security software WebGL).
WebRTC policy is `default` or `disable-non-proxied-udp`; network permission and
working TURN/peer connectivity still depend on deployment. Use a fresh private
profile per process. Do not share profiles between processes.

The frame-subscriptions switch advertises frame subscription support in the
handshake. The root page initially emits raw frames/audio; immediately send
`frames` and `audio` with `enabled:false` for automation-only clients. Subscribe
explicitly when needed. Child pages start with both subscriptions disabled.

## Commands: stdin

Send one JSON object per line, terminated by LF:

```json
{"version":1,"id":1,"command":"devtools","method":"Runtime.evaluate","params":{"expression":"1 + 1","returnByValue":true}}
```

IDs are positive signed 32-bit integers, unique among outstanding requests.
Keep each encoded line below **4,194,304 bytes**, excluding LF. The host bounds
queued UI commands to 64 and their total bytes to 16 MiB; exceeding input limits
closes the browser. Apply bounded concurrency and write backpressure in the SDK.
Raw newline characters inside strings must be JSON escaped.

| Command | Fields | Result |
| --- | --- | --- |
| `cdp` | `message`: CDP request object with its own `id`, `method`, optional `params`, `sessionId` | Immediate native dispatch acknowledgement; eventual CDP result/error in `event` |
| `devtools` | `method`, `params` object | JSON CDP result for a page command; `targetId` selects a page |
| `navigate` | `url`: HTTP(S), optional `targetId` | Main-frame load completion; one pending navigation per page |
| `frames` | `enabled`: boolean, optional `targetId` | Enables/disables raw video |
| `audio` | `enabled`: boolean, optional `targetId` | Enables/disables raw audio |
| `audio.status` | Optional `targetId` | `requested`, `active`, `packets`, `lastPacketMs` |

`targetId` values come from CDP's Target domain, never from CEF process IDs.
New child pages have raw subscriptions disabled until explicitly enabled.
For browser automation use `cdp`: the host handles browser/context/target
commands and forwards page-session commands to CEF's DevTools implementation.
The simpler `devtools` command addresses the selected page, not a browser domain.
All `media.*` commands are rejected in the standalone build. Inspect the `ready`
capabilities before assuming optional functionality exists.

## Packets: stdout

Each packet consists of:

```text
uint32 little endian: JSON header byte length
UTF-8 JSON header: exactly that many bytes
binary payload: exactly header.bytes bytes
```

Pipe reads can fragment or combine packets at any position. Validate a header
length of **2–16,384 bytes** before allocating it. Require `version:1` and
integer `bytes` from 0 to **33,177,600** (3840×2160×4). Control JSON payloads
must additionally be at most **16 MiB**. Verify payload dimensions before
allocation; reject truncation on EOF. Header schemas are in
`protocol.schema.json`; dimension-dependent byte equality needs a decoder check.

| Type | Header fields | Payload |
| --- | --- | --- |
| `ready` | `timestampMs`, `cefVersion`, `nativeMedia:false`, `frameSubscriptions:true`, `audioSubscriptions:true` | Empty |
| `response` | `id`, `ok`, optional `error` | Optional JSON result, or empty |
| `event` | `topic:"cdp"` | JSON CDP response or event |
| `frame` | `timestampMs`, `width`, `height`, `pixelFormat`, `dropped` | BGRA8 or I420 pixels |
| `audio` | `timestampMs`, `frames` | 48 kHz stereo signed 16-bit little-endian PCM |
| `error` | `error` | Empty; SDK treats this as terminal |

`ready` must occur exactly once, before any other packet. `timestampMs` is
relative to the native host's steady clock origin, not wall-clock Unix time.
Retain it for A/V synchronization; an SDK may map it to its monotonic clock on
handshake but that mapping includes transport latency. Audio timestamps estimate
the first sample's native-clock time. Continuous timestamps do not prove that
the source audio was continuous.

BGRA frames are packed top-left-origin bytes, exactly `width*height*4`.
I420 is contiguous Y, U, V, BT.709 limited range, even dimensions, exactly
`width*height*3/2`. Width is 1–3840, height 1–2160 after browser resizing.
`dropped` is the cumulative number of queued frames replaced by fresher frames.
Audio packets contain 1–4096 frames and exactly `frames*4` bytes.
No raw audio drop count is carried in v1's audio header. Native diagnostic
counters can report it, but an SDK cannot promise lossless audio from this tap.

For `cdp`, native request ID 7 might contain CDP request ID 42. The native
`response` for 7 indicates acceptance. The eventual `event` payload with
`id:42` completes the CDP request. Events without `id` are notifications. Preserve
`sessionId` for flattened page and iframe sessions. A successful dispatch
acknowledgement must never be mistaken for a successful CDP operation.

## Backpressure and lifetime

The host keeps one pending raw video frame and at most 100 raw audio packets;
it replaces/drops old raw data under consumer backpressure. A currently written
packet is separate from those queues. Control traffic has a separate cap of
4096 queued packets and 16 MiB of queued header/payload bytes. Overflow produces
a terminal error packet; shutdown cleanup is the caller's responsibility.

Use request deadlines, clear pending maps on disconnect, and bound subscriber
queues. Never wait for application callbacks on the protocol-reader thread.
Do not expose unauthenticated command or CDP forwarding to a network.

Close stdin (after flushing any commands) to initiate graceful shutdown. The
host closes its targets, stops capture, shuts down CEF, and exits. An EOF mid-packet
is truncation unless a previously declared shutdown policy permits discarding
it. Wait for process/pipe completion, kill after a bounded deadline if necessary,
then remove the private profile. `Browser.close` through CDP also closes targets,
but stdin EOF is the process-lifetime primitive. There is no `close` command.

## Versioning

The package version and wire version are independent. Additive optional fields
may appear in v1. Incompatible framing, required-field, semantic, or unit changes
require a new protocol version and SDK negotiation. `nativeMedia:false` is a
capability boundary; SDKs should reject recording/encoding requests early.
