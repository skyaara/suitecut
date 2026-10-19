# Chromium 156 native WebSocket routing extension

Status: **isolated, proposed vendor source patch; not compiled or run**. A rebuilt
CEF library is still required. The SuiteCut native controller/SDK now has an
explicit root-target adapter guarded by the actual versioned backend probe. The stock SuiteCut binary gains no routing capability from this file.
This patch is separate from the working CDP WebSocket observation feature.

Apply `chromium156-websocket-routing.patch` at Chromium `src`, using the pinned
`refs/tags/156.0.8078.17` checkout selected by CEF
`47dbc1df023e99f006fb9e850a8d83cc19b4fdd9`
(`156.0.7+g47dbc1d+chromium-156.0.8078.17`). The adjacent JSON manifest records
original and patched SHA-256 values for every source file, including the absence
of the newly introduced header. The patch was checked against downloaded exact
pinned source files; this is an applicability check, not a Chromium compilation.

## Actual native interception

The extension changes Blink's `WebSocketChannelImpl` and its existing Inspector
Network agent. It pauses the original native channel before the original
`WebSocketConnector::Connect`, after the existing mixed-content, subresource
filter, scheduler and connection-count checks. A continuation resumes that same
connector and its original handshake/throttle path. A mock decision instead opens
the original `WebSocketChannelClient`, which supplies Blink's own WebSocket state
and events. It does not construct a JavaScript WebSocket replacement, use a local
HTTP/WebSocket proxy, treat Fetch interception as frame control, or manufacture
wire observations.

Outgoing text/ArrayBuffer messages are retained before the native transport send.
Blob messages use the original asynchronous native Blob loader and queue, so
later text/ArrayBuffer sends cannot overtake the Blob. Incoming fragments use the
original native reassembly and UTF-8 validation, then are retained before delivery
to the original channel client. Routing therefore controls complete WebSocket
messages; it does not expose ping/pong control frames or raw fragmentation.

Each held message has an exact channel-local monotonic ID and direction. A
forward, drop or replacement consumes it once and preserves FIFO within its
direction. Invalid replacements or a backpressured destination reject before
consuming that decision. Forwarded and injected transport writes retain an
explicit queue tag across partial data-pipe writes: they cannot reenter routing
or consume the original JS `bufferedAmount` twice. Original sends settle their
native buffered-byte receipt when copied into the bounded interception queue.

Author close waits for original outgoing route decisions and queued Blob/output
work. An explicit controller close initiates the existing real transport closing
handshake, or the native mock client's closing callbacks. Channel disposal
invalidates weak control handles and releases pending messages. Agent disable or
session disposal schedules native channel aborts. Existing public wire
observation is kept separate from interception decisions.

## Versioned protocol contract

These are new experimental methods on the rebuilt renderer's Network domain,
not commands provided by stock CEF:

- `Network.suiteCutWebSocketConfigureV1({enabled})` returns revision `1`,
  `maxSockets:128`, `maxPendingMessages:64`, and `maxPendingBytes:2097152`.
  Enable Network first. Enabling applies only to subsequently created channels
  in that exact target/session; disable aborts its owned routed channels.
- `Network.suiteCutWebSocketCommandV1` takes all of `requestId`, `action`,
  `messageId`, `opcode`, `payloadBase64`, `code`, and `reason`. Unused fields are
  an empty string or zero. Payload encoding must be canonical padded base64.
- `Network.suiteCutWebSocketRoutedV1` emits `created`, `message`, and `closed`
  stages, with exact request identity. Message events carry a message ID,
  direction, opcode `1` or `2`, and the original message bytes as base64.

Commands select the following native operations:

| Action | Native behavior |
| --- | --- |
| `continue` | Consume the initial decision and resume the original server connection without message interception. |
| `connect` | Consume the initial decision and connect the original server transport with message interception. |
| `mock` | Consume the initial decision and open the native client without a server transport. |
| `forward` | Deliver the held message to its original native destination. |
| `replace` | Deliver supplied text/binary bytes instead of that held message. |
| `drop` | Consume the held message without destination delivery. |
| `client` | Inject supplied text/binary bytes into the native client. |
| `server` | Send supplied text/binary bytes through the connected original server transport. |
| `close` | Initiate native close with code 1000 or 3000–4999 and a reason of at most 123 UTF-8 bytes. |
| `abort` | Schedule native error/abnormal close and retire the channel. |

A command ACK establishes native acceptance only. It does not assert remote
server delivery, an observed opening handshake, or completion of a closing
handshake. Initial connect decisions and held message decisions expire after
30 seconds from their original observation; subsequent activity does not renew
them. A single scheduled timer per channel services those original deadlines,
so rapid successfully resolved messages do not accumulate per-message timers.

## Bounded ownership and remaining integration

Each agent owns at most 128 weak channel handles. Each channel retains at most
64 pending decisions and 2 MiB of decision payload; the original Blob/output
queue has its own 64-message/2 MiB admission, and routed receive reassembly is
limited to 2 MiB. Injected output applies bounded admission before allocating
another native output message. These are separate physical/logical queues,
not a claim that total renderer RSS is 2 MiB. Existing Chromium connection-count
limits also remain in force. Overflow, expired decisions, invalid ownership and
closed channels fail explicitly.

The production `NativePage.webSocketRouting` adapter now probes this exact
revision before enabling root-target control. It preserves page-local routing
generations and exact socket/message identities and bounds queued event
ownership and original deadlines. See
[`docs/native-websocket-routing.md`](../../../docs/native-websocket-routing.md).
Stock CEF rejects the probe; no source-file presence enables the feature.

Page/context URL registration and callback dispatch, inherited policy for
new/popup/OOPIF/worker targets, and cross-target generation coordination remain
pending. Independent server-side close interception/handler policy is not
introduced by this first vendor patch; actual server closure keeps Chromium's
existing close propagation. No custom vendor build or runtime acceptance is
claimed by the native adapter's stock-header compilation.

Use the pinned CEF build procedure in `README.md` on a host with room for the
complete Chromium checkout, toolchains and output. The earlier recommendation
is a Linux host with roughly 500 GiB free disk and 64 GiB RAM; it is a planning
budget, not a measured minimum. This workspace cannot hold that build. Do not
replace the normal pinned SDK/library in place or enable these methods in the
production feature map until the generated protocol/probes, Blink, and CEF
library have actually compiled. E2E and fuzz verification remain deferred.
