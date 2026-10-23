# Native WebSocket transport control

`NativePage.webSocketRouting` controls the original Blink WebSocket channels in
that exact root page target. It requires a custom CEF build containing the
isolated Chromium 156 WebSocket routing extension. The stock pinned binary
rejects `enable()` with an unsupported-extension error. Merely shipping the
vendor patch does not enable anything.

```ts
const routing = page.webSocketRouting
await routing.enable()
// Enable before the page creates the channels to be controlled.
await page.evaluate("globalThis.socket = new WebSocket('wss://example.test/ws')")
const created = await routing.nextEvent()
if (created.event === 'created') await created.socket.mock()

const event = await routing.nextEvent()
if (event.event === 'message') {
  console.log(event.message.outgoing, event.message.payload())
  await event.message.replace('replacement') // Or forward() / drop().
}
await routing.disable()
```

`enable()` requires the actual backend to acknowledge
`Network.suiteCutWebSocketConfigureV1` with revision 1 and the pinned capacity
contract. It does not infer support from a version string, source patch, existing
observation events or a JavaScript socket replacement. The launch capability
metadata remains conservatively false for the distributed stock backend; this
explicit target probe is the authority for an attempted custom-backend session.
A successful probe is not evidence that the uncompiled vendor candidate works.

`nextEvent({timeout, signal})` yields `created`, `message`, or `closed`, with a
stable `NativeRoutedWebSocket` object. A channel's initial decision can be:

- `connectToServer()`: connect the original server transport and intercept messages.
- `mock()`: open the original native client without a server connection.
- `continue()`: connect the original transport without message interception.

A `NativeWebSocketRouteMessage` provides its exact channel-local `id`, direction,
opcode, copied binary payload or decoded text, and one-shot `forward()`, `drop()`
or `replace(string | Buffer)` decisions. Concurrent decisions on the same SDK
message reject; native ownership and FIFO validation remain authoritative. A
failed or timed-out command may have reached the backend: the SDK does not claim
remote delivery or make an automatic retry. Failed initial and message decisions
remain consumed or uncertain in the SDK; they are not reset for a blind retry.

`socket.send()` injects to the native client; `socket.sendToServer()` writes to
its original connected transport. `close({code, reason})` initiates the native
closing path; `abort()` schedules native abnormal close. Only an actual `closed`
event marks `isClosed()`. Native acceptance is distinct from a server opening
handshake, remote message delivery and completed closure.

The native adapter scopes every command to a page-local routing generation and
an actually observed socket identity. It accepts root-target events only and
allows one pending event consumer. It retains at most 128 queued events and
8 MiB of encoded event metadata/payload. Queue overflow or a queued event older
than its original 30-second observation deadline disables the routing policy.
It never extends the vendor channel's own pending-decision deadlines. Each
payload is limited to 2 MiB; the vendor's 128-channel and per-channel 64-message
limits also apply. Queued event deadlines use the existing single source timer.
Disabling aborts routed channels through the vendor agent. The native adapter
retains actual configure replies independently of logical command cancellation,
keeps a configuration barrier until both the original enable and required
cleanup ACK have settled, and pins all callbacks to that exact routing epoch.
Failed cleanup or a missing physical ACK retires the page through the native
fatal teardown path. Cancelled activation cannot leave an enabled backend
without its caller or let a later generation overtake its cleanup.

A successful disable ACK invalidates control handles; it does not establish
transport closure. Such handles report `isDetached()` while `isClosed()` still
requires an actual native closed observation. A failed or uncertain SDK
enable/disable leaves that controller in an uncertain state, requiring page
recreation before further routing. It never pretends that a transport failure
reset the backend policy.

Page and context registration use the same original native channel controls:

```ts
await context.routeWebSocket('**/socket', socket => {
  // Registered routes mock by default. send() first opens that real native client.
  socket.onMessage(async message => {
    if (message.outgoing) await socket.send(`reply: ${message.payload()}`)
    // A handler's undecided message is dropped. Use forward()/replace() explicitly.
  })
})
await page.routeWebSocket(/socket$/u, async socket => {
  await socket.connectToServer()
  socket.onMessage(async message => {
    if (message.outgoing) await message.forward()
    else await message.replace('intercepted server message')
  })
})
await page.unrouteAllWebSockets({ behavior: 'wait' })
await context.unrouteAllWebSockets({ behavior: 'ignoreErrors' })
```

`routeWebSocket` accepts URL globs, regular expressions, and synchronous URL
predicates. Native policy chooses candidates in newest-page-first order, then
newest-context-first order, before authored callbacks run. A host matcher runs
only for its genuine native candidate, under an acknowledged callback lease.
Regular expressions are snapshotted without modifying the author's `lastIndex`.
An asynchronous predicate is rejected and its actual continuation remains
physically retained until settlement. Relative string patterns carry the actual
owning context's `baseURL`; native policy resolves HTTP/HTTPS bases to WS/WSS.
The original matcher identity remains available for `unrouteWebSocket(url,
handler?)` removal.

`socket.onMessage` receives both outgoing client messages and incoming server
messages, distinguished by `message.outgoing`, with exact held-message identities
and copied text/binary payload access. Without a message handler, connected
transports forward automatically and mock transports drop outgoing messages.
With a handler installed, forward/drop/replace decisions are explicit; an
undecided message is dropped after the handler returns. Native message callbacks
serialize per exact target/attachment/channel, preserving FIFO through awaited
author work and native decision ACKs. `send` injects into the native client;
`sendToServer` writes the original server transport. `onClose` receives only the
actual closed socket handle; no missing close code/reason is fabricated.

`unrouteWebSocket` and `unrouteAllWebSockets` leave HTTP route registrations
unchanged. Native removal aborts the selected controlled channels and detaches
SDK handles after its actual removal ACK. Channels previously released with
`continue()` remain ordinary browser transports. Detachment never claims
transport closure. `behavior: 'wait'` joins actual authored callback and native
finish receipts; `ignoreErrors` suppresses selected authored exceptions only
after actual policy removal succeeds. Genuine native command failures remain
observable. `webSocketRouteErrors()` drains asynchronous callback/control
errors. Active callbacks retain their actual physical ownership after scope
closure; closing an SDK scope submits native registration cleanup using the
process lifetime when the native owner still exists.

Context policy is registered with native targets directly. Global native push
messages are decoded before Page lookup, so context callbacks can control a
popup or worker before a Page facade exists. The packet header contains bounded
metadata; incoming message bodies carry up to 2 MiB of raw binary data, avoiding
a base64 encode/decode round trip in SDK event delivery. Outgoing send/replace
payloads above 24 KiB use native begin/append/commit staging with 24 KiB chunks.
Each IPC command remains under the existing 64 KiB bound, and only commit invokes
the original native send once. Native staging pins original ownership/deadline;
command failure attempts actual staging abort rather than retrying a send.

Frame facades reject page-owned registration. Independent server-close
interception still requires additional backend support. The custom CEF vendor
library has not been compiled or exercised; stock pinned CEF rejects native
registration through its actual versioned probe. Source compilation and SDK
transport checks do not establish custom-runtime support. E2E and fuzz acceptance
remain deferred.

Backend boundary: V1 cannot authenticate an agent replacement. Actual root-owner
loss retires routing and requires recreating the page. The existing native pause
scope remains the extent of the pre-author guarantee. A service-worker startup
phase that requires resuming before routing acknowledgment is unsupported under
an active policy; failure closes the actual page/context rather than resuming
author code without the inherited policy.
