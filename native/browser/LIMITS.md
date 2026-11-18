# Native limits

suitecut no longer imposes its own size, count, byte, concurrency or retention
limits. The November 2026 audit listed 441 of them in `native/browser`; they
are all removed, together with the ones the audit missed in `native/shared`,
the TypeScript host (`src/`), and suitecut's own Chromium WebSocket-routing
patch (`cef-patches/chromium156-websocket-routing.patch`). This includes limits
that only copied a Chromium number (192 MiB DevTools bodies, 1,000 frames per
page, the 123-byte WebSocket close reason): Chromium, Blink, V8, CEF, FFmpeg
and VideoToolbox now enforce their own limits and return their own errors.

What that means in practice:

- Nothing is clipped, dropped, refused or evicted for size, and no page or
  browser is closed because a suitecut table filled up. Bodies, postData,
  WebSocket frames, HAR entries, trace resources, console messages, cookies,
  routes, workers, frames, handles and snapshots pass through whole.
- Journals and histories (network events, WebSocket frames, console, context
  network records, request metadata, response extras) are no longer evicted.
  They are released when their page, context or owner closes. A very long-lived
  page with heavy traffic therefore holds more memory than before.
- Media queues (raw video and audio in `media_pipeline.cc`, the writer's audio
  and event queues) are unbounded. Recordings and streams have no file-size,
  canvas-size, fps, bitrate or duration cap of suitecut's own.
- Trace archives switch to ZIP64 when a ZIP32 count or offset would overflow.

## What stays, and why

| Kind | Examples |
| ---- | -------- |
| Wire and file formats | 4-byte length fields in the init-snapshot transport; the 16-bit ZIP entry-name length; the 32-bit `wrappedMask` in pristine dispatch; multipart boundaries suitecut generates (16..70). |
| Overflow and precision guards | `width*height*4` must fit `size_t`; `ceil(dim*scale)` must fit an `int` stride (CEF, libyuv and encoders take int strides); 2^53 serials and `UINT64_MAX` exhaustion; 20-digit decimal uint64 parsing. |
| Validity | Empty, NUL, scheme, base64-canonical, finite, non-negative, HTTP status and port ranges, cookie expiry ≤ year 9999, latitude/longitude. |
| Backpressure that only waits | Host stdin `writableLength` 4 MiB; writer `TryEvent` backlog (65,536 events / 64 MiB, an empty backlog accepts any size, routes retry); `reply_credits_`; `queued_commands_<64`; VideoToolbox `pending+packets>=4`. None of these refuses or drops work. |
| Splitting only | 8 MiB body chunks, 24 KiB / 32 KiB staging pieces, 64 commands per UI turn, coverage inventory pages, 1 MiB trace write chunks. |
| Parameters that raise Chromium's own buffers | `Network.enable` `maxTotalBufferSize`, `maxResourceBufferSize`, `maxPostDataSize`. Omitting them would shrink Chromium's buffers to its smaller defaults. |
| Timeouts and retry policies | Every deadline; the 3-attempt HTTP auth retry (as Playwright). |
| Display only | Strict-mode and handle previews (100 characters, a few nodes), debug-log line clips. |

`protocol/values.h` `kMaxResponseBytes` and `DevToolsCall::response_limit` are
still passed around but nothing reads them; they can be deleted in a later
cleanup that touches `page/source.h`.

`native/runner` (the test runner, not the browser) still has its own report
metadata caps; they were out of scope.

Regression cases live next to each header in its `*_test.cc`: each former
limit is now exercised with a value past it.
