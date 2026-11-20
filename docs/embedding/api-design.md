# Embedding API and session protocol design

Status: **draft for review**, 2026-11-20. It specifies the
[direction document](direction.md), whose open questions were decided by
delegation on 2026-11-20 (the user can revisit them). Nothing in "Proposed"
sections is implemented unless it says so. `client.h` is unchanged; the header below is a sketch.

Part 1 records what exists today, checked against the code at `48ab856`
(`claude/napdock-156`). Part 2 specifies the versioned contract. Napdock
references are to its repository (`helpers/browserhost`, its app's
`crates/platform-macos`, and `docs/decisions/2026-11-18-agent-browser.md`),
which this repository does not contain. They describe the first embedder, not
part of SuiteCut.

Line numbers refer to these files unless a path is given:

- `shared_session.cpp`, `control_session.cpp`:
  [native/client/examples/](../../native/client/examples/)
- `surface_output.mm`, `surface_output.h`:
  [native/browser/](../../native/browser/)
- `capture.cc`, `commands.cc`, `lifecycle.cc`, `navigation.cc`, `input.cc`,
  `frames.cc`: [native/browser/page/](../../native/browser/page/)

## Part 1: What exists today

### Session protocol 0 (as implemented)

There is no version field; this document calls it protocol 0.

**Location.**

- The socket is `<dir>/<name>.sock`. `<dir>` is `--session-dir`, else
  `$XDG_RUNTIME_DIR/suitecut-control`, else `/tmp/suitecut-control-<uid>`
  (`control_session.cpp:86-102`).
- The directory must be a real directory owned by the user with no group or
  other permission bits. The socket is made 0600 (`:199`).
- A client checks the socket's type, owner and mode before connecting
  (`:108-111`).
- Startup is serialised by `flock` on `<dir>/<name>.lock` (`:165-180`).
- Napdock uses
  `~/Library/Application Support/Napdock/Browser/run/napdock.sock`
  (`SessionWire.swift:100-110`).

**Starting and stopping.**

- `suitecut-shared-session --session NAME --browser ABS --profile ABS [--scale 1|2] [--session-dir DIR] [--idle-ms N] start|status|close`
  (`shared_session.cpp:584-633`).
- `start` forks a worker (`setsid`, `umask 077`) and waits up to 35 s for its
  readiness message on a pipe. It then sends `_session_status` and prints the
  reply; the exit status is 0 when it is ok (`control_session.cpp:181-233`).
- The defaults are scale 2 and an idle timeout of 30 minutes
  (`shared_session.cpp:595-597`).
- The worker exits when it is idle with no connection open, on
  `_session_close`, or within about a second after the engine dies
  (`control_session.cpp:336`, `shared_session.cpp:121`, `:518-528`).

**Framing.** Each message is a 4-byte little-endian length followed by that
many bytes of JSON. Length 0 is refused, and there is no other size limit
(`control_session.cpp:71-85`).

**Connections.**

- Every accepted connection runs on its own detached thread and carries any
  number of request and reply pairs, one at a time (`:289-323`).
- The peer's user id must equal the server's (`getpeereid`); otherwise the
  connection is dropped without a reply (`:344-345`).
- Once bytes arrive, the whole message must arrive within 2 s (`:304`). A
  reply has 10 s to drain (`:317`).
- A malformed message or an expired deadline ends the connection with no
  reply (`:320`).
- A connection limit exists in the code (`busy`, `:348-351`), but the shared
  session passes 0, meaning no limit (`shared_session.cpp:621`).

**Service requests.** These need no owner and are handled before the tab
logic:

| Request                               | Reply `result`                                         | Source                     |
| ------------------------------------- | ------------------------------------------------------ | -------------------------- |
| `{"request":{"tool":"_session_status"}}` | `{running:true, pid, session, idleTimeoutMs}`        | `control_session.cpp:308`  |
| `{"request":{"tool":"_session_close"}}`  | `{closed:true, session}`, then the whole service closes | `control_session.cpp:309-311`, `:324-363` |

**Owner requests.**

- Every other message must carry `owner`, a non-empty string of
  `[A-Za-z0-9._:-]` with no length limit (`shared_session.cpp:66-73`,
  `:127-129`).
- The first of these keys present decides the request: `request`, `tabs`,
  `view`, `watch`, `catalog` (`:132-137`). The header comment lists only the
  first four (`:8-13`).
- A reply is `{"ok":true,"result":…}` or
  `{"ok":false,"error":{"code","message"}}` (`:138-143`).
- An exception that escapes the handler produces `{"ok":false,"error":{"message"}}`
  with no `code` (`control_session.cpp:314`).

| Key and op            | Fields                                                       | Result                                    | Effect on control                                    | Source |
| --------------------- | ------------------------------------------------------------ | ----------------------------------------- | ---------------------------------------------------- | ------ |
| `request`             | `page`, `request:{tool, arguments?}`                         | the tool's result                         | refused on a `human` tab; `unclaimed` becomes `agent` after the call | `:160-189` |
| `tabs` `list`         | –                                                            | `{tabs:[{page,url,title,control,generation}], cursor}` | –                                         | `:398-409` |
| `tabs` `open`         | `by: agent\|human` (default `agent`), `url?`                 | the tab                                   | `agent` if by an agent, else `unclaimed`             | `:220-240` |
| `tabs` `close`        | `page`                                                       | `{closed:true}`                           | –                                                    | `:242-251` |
| `tabs` `navigate`     | `page`, `url` or `history: back\|forward\|reload`            | the tab, after the commit                 | becomes `human`                                      | `:252-267` |
| `tabs` `take`         | `page`                                                       | the tab                                   | becomes `human`                                      | `:268-271` |
| `tabs` `release`      | `page`                                                       | the tab                                   | `human` becomes `unclaimed`; otherwise unchanged     | `:272-281` |
| `view` `attach`       | `page`, `port` (bootstrap name, at most 128 bytes)           | `{enginePid}`                             | –                                                    | `:291-299` |
| `view` `detach`       | `page`                                                       | `{detached:true}`                         | –                                                    | `:300-303` |
| `view` `screenshot`   | `page`                                                       | `{base64, mimeType:"image/png"}`, CSS scale, viewport only | –                                   | `:304-315` |
| `view` `resize`       | `page`, `width`, `height` in CSS px                          | `{width,height}`                          | –                                                    | `:316-322` |
| `view` `input`        | `page`, `event`                                              | `{delivered:true}`                        | a press, key or text makes it `human`                | `:323-330`, `:337-378` |
| `watch`               | `cursor`, `timeoutMs` (0 to 25000, default 20000)            | the list and the new `cursor`             | –                                                    | `:380-396` |
| `catalog`             | –                                                            | the tool definitions                      | –                                                    | `:136` |

Input events (`:337-378`):

- `{kind:"mouse", action:"move", x, y}`
- `{kind:"mouse", action:"down"|"up", button:"left"|"right"|"middle", clickCount}`
- `{kind:"mouse", action:"wheel", deltaX, deltaY}`
- `{kind:"keyboard", action:"down"|"up", key}`
- `{kind:"keyboard", action:"insertText", text}`

There are no modifier fields; modifiers are keys sent down and up in turn.

**Tools.**

- The catalog is `agent_tool_catalog("core,debug,media")` without `pages`,
  `events` and `profiles` (`shared_session.cpp:107-113`). Because of how
  `core` is narrowed
  ([agent_tools.cpp:54](../../native/client/src/agent/agent_tools.cpp)),
  it includes `observe`, `act`, `wait`, `navigate`, `read`, `frames`,
  `viewport`, `get`, `upload`, `history`, the debug group (`evaluate`,
  `devtools`, `console`, …) and the media group (`recording`, `pdf`,
  `image_compare`). It does not include the driver's `dialog` or
  `screenshot`.
- A ref such as `e12` is rewritten to a driver target (`:198-212`).

**Error codes.** All of them come from `shared_session.cpp` unless a path is
given:

| Code | When |
| ---- | ---- |
| `invalid_owner` | `owner` is missing or malformed |
| `invalid_request` | an unknown key or op, or a missing field |
| `unknown_tool` | a tool outside the catalog |
| `unknown_page` | the page is missing or belongs to another owner (`:483-487`) |
| `human_in_control` | an agent call on a tab a person holds |
| `invalid_url` | a URL that is not http or https (`:75-81`) |
| `invalid_input` | a malformed input event |
| `unsupported` | no engine process id, so no frames |
| `navigation_failed` | the navigation did not commit |
| `engine_exited` | the engine connection died |
| `failed` | any other exception |
| `busy` | the connection limit (`control_session.cpp:349`) |
| no code | an exception in the connection loop (`control_session.cpp:314`) |

**Control.**

- Each tab has `owner`, `control` (`unclaimed`, `agent`, `human`),
  `generation` and `url` (`:57-63`). `generation` moves only when `control`
  changes.
- The URL follows the engine's `navigation` packets. Titles are read with
  `Runtime.evaluate` on every list, with an 800 ms timeout per tab
  (`:430-445`).
- A popup gets its opener's owner and control (`:547-558`).
- Page 1 belongs to nobody (`:105-106`).

**Napdock depends on behaviour nobody wrote down, or disagrees:**

1. **Reply size.** The helper refuses replies over 8 MB and requests over
   4 MB (`SessionWire.swift:8-9`, `:23-28`), and the service has no limit. A
   full-viewport PNG at scale 2, sent as base64 in one JSON reply, can
   exceed 8 MB. The helper then reports an oversized reply and drops the
   connection.
2. **Owner length.** The helper allows 1 to 128 bytes
   (`SessionWire.swift:50-59`); the service has no limit.
3. **Codeless errors.** The helper maps an error with no code, or an error
   given as a plain string, to `failed` (`SessionWire.swift:36-43`).
4. **Probing with any owner.** The helper checks whether the service is
   running by sending `tabs.list` as the owner `browserhost`
   (`Runtime.swift:122-128`). That works only because an owner needs no
   registration.
5. **The watch cursor.** It is one counter for the whole session
   (`shared_session.cpp:489-492`), so a watch wakes for any owner's change.
   The helper compares each result with the last and drops duplicates
   (`Session.swift:161`).
6. **The frame sender.** The helper accepts frames only from the process id
   that `attach` returns as `enginePid` (`Helper.swift:356-357`). That this
   is the process that sends frames is true but not documented.
7. **Frame size.** The engine has no limit on frame size, the helper accepts
   at most 16384 px a side (`BrowserhostMach.c:29`), and the app at most 8192
   (`crates/platform-macos/src/lib.rs:90`). The helper also limits the
   viewport to 1920×1080 CSS px (`Helper.swift:70`, `:374`). That limit came
   from an engine limit removed in `9324f9f`.
8. **The tab limit.** The decision record says an owner has at most 16 tabs
   (line 50). Nothing enforces it.
9. **Bootstrap names.** The service accepts `:` in a bootstrap name
   (`shared_session.cpp:293`), but the engine refuses one
   (`surface_output.mm:187-194`). A name with `:` therefore fails at the
   engine with "Invalid surface port name".
10. **URL schemes.** A person's navigation must be http or https, but an
    agent's `navigate` goes to the driver unchecked
    ([agent_driver.cpp:373](../../native/client/src/agent/agent_driver.cpp)).
    Whether the engine itself refuses other schemes, such as `file:`, was
    not checked.
11. **When the agent takes a tab.** An agent call takes an `unclaimed` tab
    only after the call returns (`shared_session.cpp:180-187`), so during a
    long `wait` the tab still reads `unclaimed`. A person taking the tab
    meanwhile does not stop the call.
12. **One agent call at a time.** Agent calls of every owner share one lock
    (`:169`, `:225`, `:243`), and tab `open` and `close` take it too.

### Frame handoff on macOS (as implemented)

**Receiver setup.**

- The receiver checks a receive right into its bootstrap namespace under a
  name. Napdock's helper uses `com.napdock.browserhost.<uuid>`
  (`SessionWire.swift:92-95`, `BrowserhostMach.c:31-34`).
- The name reaches the engine as `surfaceOutput {port}`
  (`shared_session.cpp:297`), and the engine gets a send right with
  `bootstrap_look_up` (`surface_output.mm:206`).
- Both processes must share a bootstrap namespace. Napdock lists checking
  this as an open item (decision record, lines 224-226).

**Surfaces.**

- The engine allocates its own NV12 surfaces: `CVPixelBufferCreate` with
  `kCVPixelFormatType_420YpCbCr8BiPlanarFullRange` (`420f`), IOSurface
  backed and Metal compatible. It then releases the pixel buffer, keeping
  only the IOSurface (`surface_output.mm:83-100`).
- Up to 4 surfaces exist per output, and all are replaced when the frame
  size changes (`:17`, `:139-159`).
- CEF's BGRA surface is converted by a Metal compute kernel with BT.601
  full-range coefficients on sRGB-encoded values. An open popup (a
  `<select>` list) is composited in (`:38-68`, `capture.cc:137-158`).
- Sides are rounded down to even numbers (`:258`).
- The paint callback waits for the GPU to finish, because CEF's surface is
  borrowed only for the callback (`:289-292`).

**Message.** 64 bytes (`surface_output.h:8-22`, `surface_output.mm:19-26`,
`:295-311`):

| Part | Content |
| ---- | ------- |
| header | `COPY_SEND` to the receiver, complex, `msgh_id` `0x47425331` (`'GBS1'`), no reply port |
| descriptor | one port descriptor: a `MOVE_SEND` right from `IOSurfaceCreateMachPort` |
| inline data | `u32` magic `'GBS1'`, `u32` width, `u32` height, `u32` reserved 0, `u64` timestamp (µs of `mach_absolute_time`) |

- The send uses `MACH_SEND_TIMEOUT` with a zero timeout, so it never waits
  (`:312-313`).
- A full queue drops the frame. A dead port ends the output, and the engine
  forgets it (`capture.cc:154-156`).

**Reuse.**

- The surface sent last is never drawn into next. Another surface is reused
  only when `IOSurfaceIsInUse` is false. With none free and 4 in existence,
  the frame is dropped (`surface_output.mm:145-158`).
- "In use" is the system-wide use count. Napdock's app wraps each received
  surface in a `CVPixelBuffer` (`surface_port.rs`, `pixel_buffer`), which
  counts as a use (the engine's own note, `surface_output.mm:83-85`).
- A receiver that holds only the Mach right, or looks the surface up without
  raising its use count, can have it drawn into while it reads. Nothing
  documents this rule.

**When frames are sent.**

- Only while the page paints. Attaching asks for one paint so the viewer
  starts with a picture (`commands.cc:84-108`).
- `port: null` detaches.
- With only this viewer and no other consumer, the CPU readback is skipped
  (`capture.cc:97-107`).

**Scale.** The frame size is the CSS viewport times the session's launch
scale (`--scale`, default 2). It cannot be changed per view.

**Who checks whom.**

- The engine trusts whatever name it is given; the name travels over the
  private socket.
- The receiver checks the audit trailer: the sender's process id must be
  `enginePid` (`BrowserhostMach.c:41-77`). Napdock's app checks the helper's
  process id the same way (`surface_port.rs:214`).
- Only the process id is compared, not its version number.

### Host events today

The engine's handlers live in `native/browser/page/`. The session passes on
only `navigation` packets and the `page-created` and `page-closed` context
events, and drops everything else (`shared_session.cpp:515-567`).

| Need | State | Evidence |
| ---- | ----- | -------- |
| Popups, `<select>` | Partly: drawn into the macOS view; input into an open list not checked | `capture.cc:137-168` |
| New windows | Supported: `OnBeforePopup` makes a page; the session gives it the opener's owner | `lifecycle.cc:854-872`, `shared_session.cpp:547-558` |
| Pointer, wheel, keys, text | Supported, through CDP `Input.*` | `input.cc`, `shared_session.cpp:337-378` |
| IME composition | Missing: no `ImeSetComposition` or `Input.imeSetComposition` path, and no composition range events | none of `ImeSetComposition`, `OnImeCompositionRangeChanged` in `native/browser` |
| Edit commands | Partly: on macOS only Cmd+A maps to `selectAll`; Cmd+C/V/X/Z send bare keys | `input.cc:25-31` |
| Cursor shape | Missing: no `OnCursorChange` | – |
| JavaScript dialogs | Partly: suppressed with the default answer unless a client subscribed; when subscribed, held for up to 30 s for an answer; reaches the agent's `observe`/`act`, never the app | `lifecycle.cc:232-247`, `dialog_controls.h:25-36` |
| `beforeunload` | Partly: same controller | `lifecycle.cc:249` |
| File chooser | Partly: the agent's `upload` and the engine's `filechooser` events; a page's own dialog with nothing pending falls through to CEF (behaviour off screen not checked) | `lifecycle.cc:114-124`, `controllers/input/form_controller.inc:455-463`, `controllers/input/file_chooser_controller.inc:26` |
| Downloads | Partly: engine policy and the agent's `download` tool; not reported to the app | `lifecycle.cc:182-199` |
| Permission prompts | Policy only: answered from the context's granted list, never asked | `lifecycle.cc:126-147` |
| HTTP auth, certificates | Policy only: context credentials; no client-certificate handler | `controllers/context/context_settings_source.inc` |
| Loading state | Partly: load events exist for waits; no `OnLoadingStateChange`; the session reports navigations only | `navigation.cc:4-71`, decision record "Open" |
| Title, favicon | Partly: `OnTitleChange` only feeds HAR; no favicon | `navigation.cc:63-69` |
| Audio | Captured, and so muted (CEF mutes captured pages): `GetAudioParameters` always accepts, and the session drops the `audio` packets, so a person hears nothing (inferred from code, not measured) | `capture.cc:181-187`, `:245`, [shared-browser.md](../shared-browser.md) "CEF's audio capture mutes the page" |
| Clipboard | Partly: `clipboard-write` permission only; no host clipboard bridge | `native/browser/context_controls.cc:449` |
| Drag and drop | Missing for the person (`StartDragging` not implemented); the agent's `drag` tool exists | – |
| Accessibility | Missing for the platform (no `OnAccessibilityTreeChange`); the agent reads CDP snapshots | – |
| Crash and recovery | Supported in the engine; the session reports only `engine_exited` | `navigation.cc` (`OnRenderProcessTerminated`), `shared_session.cpp:118-126` |
| Recording | Needs FFmpeg: every recording validates an absolute FFmpeg path, and Napdock's bundle has none | [media_pipeline.cc:457-458](../../native/browser/media_pipeline.cc) |

### MCP: `suitecut-native-control` and `browserhost mcp`

| | `suitecut-native-control --mcp` | Napdock `browserhost mcp` |
| - | ------------------------------- | ------------------------- |
| Browser | Launches its own (`control.cpp:206`) | Joins the shared session; starts it if needed (`MCPServer.swift:125`) |
| Owner | None | `NAPDOCK_AGENT_SCOPE_KEY` or the session id (`SessionWire.swift:83-90`) |
| Tools | `--simple`: `observe`, `act`, `wait`. Default `core`: those plus `profiles`, `navigate`, `read`, `pages`, `frames`, `viewport`, `get`, `upload`, `history`. `--tools` adds groups. | `tabs`, `observe`, `act`, `wait`, `navigate`, `history`, `read`, `evaluate`, `console` (`AgentTools.swift:18`) |
| Tabs | The driver's `pages` | Session tab ops, so the panel sees them |
| A person's tab | Not a concept | `human_in_control` returned as the tool's error |
| First call | – | `navigate` without `page` opens the owner's first tab (`MCPServer.swift:127-150`) |
| Instructions | One generic sentence (`control.cpp:83`) | The call order and the untrusted-content rule (`AgentTools.swift:73-80`) |

**Why `suitecut-native-control` cannot join a session:**

- `--mcp` and `--session` are mutually exclusive (`control.cpp:179-180`).
- Its worker always calls `Browser::launch` (`:206`).
- Its named-session envelope (`{"request","profiles"}`, `:211-212`, `:244`) has no
  owner.
- The CEF backend cannot attach to a running engine (`:184-185`).

Both tools also default to the same session directory and
`<name>.sock` namespace, with incompatible envelopes, so two sessions with
the same name collide.

### Platform-specific and portable parts

| Area | macOS only | Linux | Windows | Portable |
| ---- | ---------- | ----- | ------- | -------- |
| Frames | `surface_output.mm`; `OnAcceleratedPaint` compiled for Apple only (`capture.cc:66-135`); `shared_texture_enabled` set only on Apple (`process/app.cc:121-124`, `page/window_mode.h:39-42`); capability `surfaceOutput` true only on Apple (`native_backend_capabilities.h:19-23`) | CPU paint | no engine | – |
| Socket | – | – | stubs that throw (`control_session.cpp:365-369`) | AF_UNIX, framing, startup lock |
| Peer check | `getpeereid` in the shared service (`:344-345`) | `SO_PEERCRED` only in the single-client service (`:249-252`); glibc has no `getpeereid` | – | – |
| Process | – | `PR_SET_CHILD_SUBREAPER` and child retirement (`:126-156`, `:189-191`) | – | `fork`, `setsid` |
| Signing | `native_macos_signing.py`; Napdock checks team `WTD7AF5L23` | none | none | – |
| Session logic, agent | – | – | – | `Host`, `AgentDriver`, `SimpleAgent`, the transport |

`examples/CMakeLists.txt` builds `suitecut-shared-session` on every platform
(line 3). Because glibc does not provide `getpeereid`, `control_session.cpp`
probably no longer compiles on glibc Linux. That breaks
`suitecut-native-control` there too, which the Linux runtime uses as its
launcher. This was not built here; it needs a Linux build to confirm.

**CEF 156 accelerated paint** (CEF branch 8078, `include/internal/cef_types_linux.h`,
`cef_types_win.h`, `cef_render_handler.h`):

- **Linux.** `cef_accelerated_paint_info_t` has
  `planes[kAcceleratedPaintMaxPlanes]` (4). Each plane has `stride`,
  `offset`, `size` and `fd` (usually a dmabuf). The struct also has
  `plane_count`, a `uint64_t modifier`, `format` (`cef_color_type_t`: RGBA or
  BGRA 8888) and `extra`. The `shared_texture_enabled` doc in the same
  header still says shared textures are "currently only supported on
  Windows". A run on Linux (see "How frames reach the app" in the
  [direction](direction.md)) shows 156 does try: it allocates GBM buffers,
  and without a render node it delivers no frames at all.
- **Windows.** `shared_texture_handle` is a `HANDLE`, opened with D3D11
  `OpenSharedResource1` or D3D12 `OpenSharedHandle`, and has no keyed mutex.
- **All platforms.** The handle or fds "cannot be cached and cannot be
  accessed outside of this callback"; the client must copy into a texture it
  owns. Resources return to a pool when the callback returns.

The engine's ready packet already reports `cefVersion` and a capability
dictionary including `surfaceOutput` (`frames.cc:203`, `:213`). The session
protocol reports neither.

### Corrections to the earlier audit

- The C ABI has 23 functions, not about 20. It also attaches to Chrome and
  Firefox over CDP and BiDi, not only to SuiteCut's engine.
- `suitecut-shared-session` is not macOS-only as built. It compiles from
  `examples/` on every platform. Only frame sharing is macOS-only, and its
  peer check probably breaks glibc builds.
- The protocol also has a `catalog` request and the service-level
  `_session_status` and `_session_close`. Tab changes arrive by long-polling
  `watch`, not as pushed events; Napdock's `tabs.changed` is the helper's
  own event to its app.
- `record-start` belongs to the prototype tabs window
  (`process/prototype_tabs.cc`), which the shared session does not use. The
  conclusion still holds by another path: the session offers the `recording`
  tool, and engine recordings need an absolute FFmpeg path.
- The packaging findings hold, checked on the installed bundle:
  - `otool -L` shows the engine loading `/opt/homebrew/opt/{curl,openssl@4,libpsl,brotli}`.
  - The engine's `minos` is 13.0 and the session tool's is 27.0.
  - Both have code-directory flags `0x0` (no hardened runtime), with team
    `WTD7AF5L23` and a secure timestamp.
  - `spctl` rejects the bundle as "Unnotarized Developer ID", and no ticket
    is stapled.
  - The Napdock packager does check that the session tool links only system
    libraries (`package-napdock-browser-macos.py:43-46`). It skips the
    engine.
- The session has no tests: nothing under `native/client/tests` refers to
  `shared_session` or `control_session`.
- Found while writing those tests: `view` `screenshot` closed the tab it
  read. `Browser::page` returns a `Page` that owns the page, and the
  temporary's destructor sent `closePage` (`:308`). With the real engine the
  page then closes and `page-closed` removes the tab. Fixed in `1ffa23f`; a
  bundle built before that has the bug.

## Part 2: Proposed contract

### Built in phase 1 (2026-11-20)

Phase 1 built the service and protocol 1 below, on branch
`claude/embeddable-runtime-2343a7`. Where it differs from this draft, the
built behaviour is what the rest of Part 2 now says:

- **Where.** `native/client/session/` inside the client CMake project, not
  a separate `native/session/` project. `host.cpp` (tabs, control, events,
  grants), `protocol.cpp` (both protocols), `main.cpp` (the CLI) and
  `control_session.cpp` (the socket, also used by `suitecut-native-control`).
- **Protocol 0 is unchanged.** It keeps claiming a tab after the agent
  call, and `watch` keeps waking on the session-wide counter. Only
  protocol 1 claims before the call.
- **Rule 9 holds.** Each owner has its own `AgentDriver` and
  `SimpleAgent` over the shared `Browser`, so different owners' calls run
  in parallel, protocol 0 included.
- **Not built yet:** `view.focus`, `input.ime.*`, `input.edit`,
  `prompt.answer`, per-view `scale`, `tab.loading`/`tab.title`/cursor
  events, and anything that needs engine work. Those are phase 3.
- **`hello`.** The reply's `runtime` has only `cef`, and `engine` only
  `pid`. `pidVersion` waits until the engine reports one.
- **Grants.** `--grants required` applies to agents: a host is the same
  user's app and needs no grant. `session.grant` defaults to `["core"]`.
- **Results.** `agent.call` returns `{result, generation, controlLost?}`.
  `agent.catalog` returns `{tools}`. `view.attach` takes
  `sink:{kind:"mach-bootstrap", name}` and returns `{view, engine:{pid}}`.
  `view.resize` returns `{width, height}`.
- **Input.** Modifiers are the keys the host holds down with `input.key`,
  as the engine already tracks them. No `modifiers` field. A pointer press
  or wheel with `x` and `y` moves there first.
- **Events.** A `tab.changed` carries the tab as it is when read, with its
  title read then. Several changes to one tab in one reply arrive as one.
  Cursors start at 1, and 0 always means a first read.
- **`--allow-schemes`** applies to tab opens and a person's navigations.
  The agents' `navigate` tool is not checked yet.
- **`_session_close`.** The socket layer answers protocol 0's
  `_session_status` and `_session_close` only on a connection that has not
  spoken protocol 1, so an agent's protocol 1 connection cannot get round
  the host-only `session.close` that way. Any other same-user client can
  still send it, whatever the protocol flags, because the CLI's `status`
  and `close` use it. Closing it further needs a decision; see "Open items".
- **Pages the engine closed** leave the owner's agent driver on its next
  call, and the driver no longer caps how many pages it holds.

### Checked against the real engine (2026-11-20)

The service from this branch ran against the CEF 156 engine Napdock ships,
with a throwaway session, directory and profile, and pages served locally.
It answered every protocol 0 envelope Napdock's helper sends, and the
protocol 1 `hello`, `agent.call`, `view.screenshot` to a file and
`events.next`. A screenshot came back as a 1280×800 PNG, and the tab stayed
open and could still navigate. The bundle built from it is the one Napdock
now runs.

Three things the fake engine hides:

- `view` `attach` to a bootstrap name nobody has checked in fails ("No
  receiver is checked in under the surface port name"). The fake engine
  accepts any name. Napdock's helper checks its port in first, so this is
  correct, but frames to a real receiver are not tested here.
- Right after a reload the title reads `""` until the page loads.
- A popup is listed with `about:blank` until its first navigation event.

The same checks passed on Linux (Ubuntu 24.04, x86_64, no GPU) against the
engine built from the official CEF 156.0.30 archive, headless, with
renderers in Chromium's sandbox. There, protocol 0's `view` `attach` and
`detach` answer `failed` with "Unknown native command", because the
surface output exists only on macOS. A Linux host has no view to attach
until phase 6. Detach could answer as a no-op there instead; nothing
depends on it yet.

### Built in phase 2 (2026-11-20)

Phase 2 built `suitecut/embed.h` and a second sample host on the same
branch. The header speaks protocol 1 to `suitecut-session`. It is installed
beside `client.h` in the same `SuiteCut::Client` library, and `client.h` and
`SC_ABI_VERSION` are unchanged. The code is in `native/client/src/embed/`:
`embed.cpp` is portable, and `embed_apple.cpp` holds the frame receiver and
the signature check. Where it differs from the sketch under "C ABI additions",
the header is what was built:

- **Runtime.**
  - `sc_runtime_inspect` takes a `.app` (programs in `Contents/MacOS`) or a
    directory holding `suitecut-browser` and `suitecut-session`.
  - It checks the code signature first: the bundle with its nested code, or
    each program. Then it runs `suitecut-session --version --json`.
  - A service that does not answer is protocol 0. A range that does not
    overlap the caller's `protocol_min`..`protocol_max` is `SC_UNSUPPORTED`.
  - The manifest (`suitecut-runtime.json`) waits for phase 4.
  - `sc_runtime_info` carries the service version, its protocol range, the
    protocol chosen, whether the signature was checked, and both paths.
- **Session.**
  - There is no `SC_SESSION_ALLOW_PROTOCOL_0` and no `max_message_bytes`:
    `embed.h` speaks protocol 1 only.
  - `SC_SESSION_START` runs the service's `start` with the session's name,
    directory, profile, and optional scale and idle time.
  - `sc_session_describe` returns the `hello` reply as JSON.
  - `sc_session_grant` takes a comma-separated tool list (NULL is `core`)
    and a lifetime. `sc_session_revoke` was added.
  - `sc_session_close` is idempotent. With `shutdown` it sends
    `session.close`, which only a host may do. Calls after it return
    `SC_CLOSED`.
  - The socket path, `directory/name.sock`, must fit in `sun_path`: at most
    103 bytes on macOS and 107 on Linux. `sc_session_open` refuses a longer
    one with `SC_INVALID_ARGUMENT`, naming its length and the limit, before
    it starts anything. A host keeps `directory` short; the sample uses
    `/tmp/suitecut-sample-<uid>`.
- **Tabs.**
  - `sc_tab` carries `page`, `control` and `generation`. URLs and titles
    come from `sc_tab_list`'s JSON.
  - `sc_tab_screenshot` writes a PNG to an absolute path (`view.screenshot`
    with `to:{file}`).
- **Views.**
  - The sinks are `SC_SINK_LIBRARY` and `SC_SINK_MACH_BOOTSTRAP`.
  - A NULL sink gives a view for input and resizing only, with no frames.
  - `sc_frame.surface` is an `IOSurfaceRef`. The frame owns it, with its
    use count raised, until `sc_frame_release`.
  - The library keeps only the newest waiting frame, and releases the ones
    it replaces.
  - Its receiver drops frames from any process but the engine's, by the pid
    in `hello`. The pid version waits until the engine reports one.
  - Off macOS, frame sinks return `SC_UNSUPPORTED`. A NULL-sink view works
    everywhere.
- **Input.**
  - There is no `modifiers` field and no `code`. Modifiers are keys the host
    holds down with `sc_view_key`, as in protocol 1.
  - There is no per-view scale. Coordinates are CSS pixels.
- **Events.** The kinds are `OTHER` 0, `TAB_CHANGED` 1, `TAB_CLOSED` 2,
  `RESYNC` 3 and `ENGINE_EXITED` 4. Types the library does not know come as
  `OTHER`, with their JSON.
- **Not in `embed.h` yet:** `sc_view_set_focus`, IME, edit commands,
  prompts, the cursor, loading and title events, and the C++ classes. Those
  come with phase 3. The header has no stubs for them. (Phase 3 added all
  but `sc_view_set_focus` and the C++ classes; see "Built in phase 3".)
- **Connections.**
  - Requests use a pool: a call takes an idle connection or opens one, so
    calls from several threads do not wait for each other.
  - Events have their own connection, and each view has its own for input.
  - A pointer move coalesces with the next one while another input is still
    on its way.

The sample host ran against a clone of the signed CEF 156 runtime Napdock
ships. It used session `sample-probe` with a throwaway profile, and a page
served locally:

- the signature requirement passed;
- frames arrived at the view's size (1024×684 CSS at scale 2);
- a click sent at CSS (200, 75) reached the page at (200, 75);
- the typed text reached the focused input;
- the session shut down when the app quit.

Two checks now repeat this; both are opt-in because they need a runtime:

- **`suitecut-native-cef_embed_frames`** (`native/client/tests/integration`,
  macOS only). Configure with `-DSUITECUT_EMBED_RUNTIME=/path/to/SuiteCut.app`
  and run `ctest -L embed-frames`; `SUITECUT_EMBED_SIGNING_REQUIREMENT` adds a
  signature check. It starts session `embed-frames` with a short `mkdtemp`
  folder and a fresh profile, removes both, and writes `results.json` in the
  `tests/cef` evidence shape. Against a copy of the signed CEF 156 runtime
  (2026-11-21, 3 runs, all passed), it checked:
  - frames at the view's size times 2, `420f`, two planes, and in use until
    released;
  - page content by luma: 200 and 32 for the two grey pages, both exact;
  - a held frame kept its content and was not sent again while about 64
    frames a second arrived;
  - frames at the new size after a resize, and none in the 3 s after
    detach;
  - frames again after a new attach;
  - the engine exiting when the session closed.

  It is not in `tests/cef/manifests/cpp.json`, because those cases have no
  platform field and the full suite also runs on Linux.
- **`scripts/verify-embed-sample-macos.py`**. It drives the sample against a
  fixed local page with its own session name, short folder and profile,
  clicking and typing. It checks a clean exit: exit 0, no process left with
  the profile, and the socket removed. It then checks four probe points:
  the click, the text, the page's script and the background. Last, it
  compares the snapshot with
  `native/client/examples/embed-macos/reference/snapshot.png`:
  - **Tolerance:** both images are scaled to CSS size, and at most 0.5% of
    pixels may differ by more than 40 of 255 in any channel.
  - **Regenerating:** run with `--update-reference` on a Mac whose run you
    have checked. The probes must pass before the reference is replaced.
    Commit it with the macOS and runtime versions.
  - **Results:** two runs against the reference had 0 differing pixels. A
    negative control against a different page's snapshot failed, with 18.5%
    differing.

### Built in phase 3 (2026-11-21)

Phase 3 built host events and prompts on the same branch, in the order the
decision record's prompt rule settles. It keeps protocol 0 and the ABI:
`SC_EMBED_ABI_VERSION` is still 1, and `embed.h` only gained names.

- **Engine switch.** The engine sends host events only when launched with
  `--suitecut-host-events=1` (`page/host_events.cc`). `suitecut-session`
  passes it through a new `LaunchOptions::host_events`. Other clients never
  see the new `host` packet type, so their decoders, which refuse unknown
  types, keep working. The engine's `ready` packet says `hostEvents:true`
  only then, because the TypeScript SDK's capability schema is strict.
- **1. Loading.** `OnLoadingStateChange` and `OnLoadingProgressChange`
  become `tab.loading {page, loading, progress, canGoBack, canGoForward}`.
  `tabs.stop` (host, `sc_tab_stop`) calls `StopLoad`. Like a navigation, it
  takes the tab for the person. `OnTitleChange` becomes
  `tab.title {page, title}`: the engine's title, which is the URL for a page
  without one. `tabs.list` still reads `document.title`, as protocol 0 always
  has. With host events, a protocol 1 `Tab` adds `loading`, `canGoBack` and
  `canGoForward`.
- **2. Dialogs.** A connection whose `hello` lists `capabilities:["prompts"]`
  (`SC_SESSION_PROMPTS`) makes the session hold its owner's dialogs. It
  subscribes each of the owner's pages, and popups as they open, to the
  engine's existing dialog controls.
  - A held dialog is `prompt.opened {prompt:{id, page, kind:"dialog",
    dialog, message, defaultValue, url, deadlineMs:30000}}`. Its id is
    `dialog:PAGE:N`.
  - `prompt.answer {prompt, accept, text?}` answers it
    (`sc_prompt_answer_dialog`). A host may answer any of its owner's
    prompts, and an agent only on a tab it holds. Answering does not change
    control. A prompt dialog answered without text keeps its default.
  - `prompt.closed {prompt, page, outcome}` follows. The outcome is
    `accepted`, `dismissed`, `timeout` (the engine's 30 s deadline) or
    `closed` (a navigation ended it, or its page closed). Under the switch,
    `OnResetDialogState` reports each dialog it drops closed, with
    `reason:"reset"`.
  - A page showing a dialog is blocked in it, so a navigation would wait
    for the dialog's deadline. A host's `tabs.navigate` therefore first
    dismisses the page's held alerts, confirms and prompts, reported
    `closed`. A `beforeunload` is left for the person, since it asks about
    that navigation.
  - When the owner's last such connection closes, what it held is
    `dismissed`, and its pages go back to the engine's default answers.
    While none is connected, nothing changes from before.
  - An agent connection may also list `prompts`, to answer dialogs on the
    tabs it drives.
- **3. Popups.** A popup's `Tab` now carries `opener`, on protocol 1 only,
  and its `tab.changed` is the new-window event. The window's disposition
  and features are not reported (see "Open items").
- **4. Cursor.** `OnCursorChange` becomes `view.cursor {view, cursor}` with
  a CSS cursor name. `custom` adds the hotspot and size but not the image
  yet. Under the switch the engine returns true, so CEF does not set a
  native cursor of its own.
- **5. File choosers, downloads, permissions, HTTP auth and client
  certificates.** Built after the decisions; see "Prompts that go to the
  person" below.
- **6. IME and edit commands.**
  - `input.ime.set {view, text, selection?, replacement?}`,
    `input.ime.commit {view, text, replacement?}` and
    `input.ime.cancel {view}` call CEF's `Ime*` methods. Ranges are UTF-16
    `[start, end]`.
  - `OnImeCompositionRangeChanged` becomes
    `view.ime {view, range, bounds:[[x, y, w, h]...]}`, for placing the
    candidate window.
  - `input.edit {view, command}` runs `copy`, `cut`, `paste`, `undo`,
    `redo`, `selectAll` or `delete` on the focused frame.
  - IME set and commit, and every edit but `copy`, take the tab.
  - In `embed.h` they are `sc_view_ime_set`, `sc_view_ime_commit`,
    `sc_view_ime_cancel` (with `sc_ime_composition`) and `sc_view_edit`
    (`sc_edit_command`). They go in order with the view's other input.
- **`hello`.** It now lists `prompts`, and with host events also
  `tabs.stop`, `tab.loading`, `tab.title`, `view.cursor`, `input.ime` and
  `input.edit`.
- **Events.** `sc_event_kind` adds `TAB_LOADING` 5, `TAB_TITLE` 6,
  `VIEW_CURSOR` 7, `VIEW_IME` 8, `PROMPT_OPENED` 9 and `PROMPT_CLOSED` 10.
  `sc_event_page` reads `view` and `prompt.page` too. When a reply merges
  several `tab.changed` for one tab, the merged one now stays where the first
  was, so a new tab comes before its loading events.
- **Protocol 0.** Its replies drop the new tab fields, and it gets no new
  events. `shared_session_test` passes unchanged.
- **The sample host** shows the tab's title and loading state, stops loads,
  follows the history buttons, sets the page's cursor, shows dialogs as
  sheets, composes through `NSTextInputClient`, and sends the Edit menu's
  commands to the page. It was compiled on the Mac but has not been run
  against an engine with host events, since no macOS engine with them has
  been built.

**Checked against the real engine** on the devbox (2026-11-21: Linux x64,
headless, the official CEF 156.0.30 build, engine and client at `e71a2c9`).
`suitecut-native-cef_host_events` passed all 40 checks:

- `hello` lists the host events, so the switch reached the engine.
- The first load: start, progress to 1, finish, with `canGoBack` true
  because a new tab starts at `about:blank`. Its title and a script's later
  title change both arrive, and `tabs.list` agrees.
- Stopping a page whose image hangs ends the load at once.
- Dialogs: a confirm accepted (the page got `true`), a prompt answered with
  text (the page got it), a confirm dismissed (`false`), and a second answer
  refused. An alert open during a host navigation is closed, and the
  navigation completes.
- The cursor over an input, a link, `cursor:wait` and the page:
  `text`, `pointer`, `wait` and `default`.
- IME: the composition shows in the input while composing, `view.ime`
  gives its range and two character boxes, commit puts the text in, and
  cancel leaves nothing. Select all with delete, undo and redo do what they
  say. The clipboard works headless too: cut, then paste twice, gave the
  text twice.
- A click on a `target=_blank` link opens a popup with its `opener`, and the
  popup reports its own loading.
- The engine exits when the session closes.

The first runs found the test's own mistakes (history after `about:blank`,
an argument-order bug, a dangling loop over a temporary). They also found
two engine behaviours, one fixed in the session (dismissing dialogs before a
host navigation) and one left open (a navigation's commit; see "Open
items").

#### Prompts that go to the person

**Decided 2026-11-21 by the user on my recommendation:**

1. **Deadline.** A person's prompt is held until it is answered, its page or
   tab goes away, or the owner's last connection that listed `prompts`
   leaves. Then today's policy answers it. There is no deadline of
   SuiteCut's own. Dialogs keep the engine's 30 s.
2. **Downloads.** On a tab the agent holds, the agent's `download` policy and
   directory apply, and nobody is asked. On a person's tab or an unclaimed
   one, a prompt host is asked where to save. With no prompt host, today's
   policy applies.
3. **Permissions.** Each request is answered on its own. Nothing is
   remembered in the profile's content settings. Remembering per origin is a
   later option.
4. **HTTP auth.** A prompt host's answer wins. The agent's
   `network.credentials` apply only on tabs the agent holds. The password
   may cross the session socket in an answer. The service never logs, echoes
   or writes it to disk.

   *Refined 2026-11-21 with the parent session:* on a tab the agent holds,
   its matching `network.credentials` answer first, and a prompt host is
   asked only when they are missing or the server rejects them. They are
   tried at most once per challenge; then the person is asked, and asked
   again while their answers are rejected. Tabs the agent does not hold
   always ask a prompt host. With no prompt host, today's policy is
   unchanged.
5. **Client certificates.** `prompt.opened` lists the certificates, and the
   answer is `{index}` or `{cancel:true}`.
6. **File choosers.** The answer is `{paths:[ABS]}`. The service checks each
   path before it reaches the engine.

**Built** (2026-11-21), under `--suitecut-host-events=1`, on protocol 1 and
`embed.h`. The ABI is still 1, protocol 0 is unchanged, and without the
switch nothing changes.

- **The engine holds the callbacks.** `SuiteCutPersonPrompts`
  (`native/browser/person_prompts.h`, `page/person_prompts.cc`) keeps each
  held CEF callback with its answer, today's policy and a cancel. The
  session turns it on per page with `{"command":"host","action":"prompts",
  "enabled":true}`, while its owner has a host connection that listed
  `prompts`. It reports `{"event":"prompt","id","kind","state":"opened",...}`
  and later `"state":"closed"` with `reason` `answered`, `policy` (today's
  policy answered it) or `reset`. An answer's contents never go into an
  event.
- **Which requests are held**, and today's policy for each, used when no
  host is connected, when the last one leaves, and for `{"policy":true}`:

  | Kind | Held when | `prompt.opened` adds | Answer | Policy |
  |---|---|---|---|---|
  | `file` | `OnFileDialog`, unless the agent's file chooser tool is waiting for it | `mode` (`open`, `openMultiple`, `openFolder`, `save`), `title`, `defaultPath`, `accept` | `{paths:[ABS]}` or `{cancel:true}` | cancel, as today with nothing pending |
  | `permission` | `OnShowPermissionPrompt` and `OnRequestMediaAccessPermission`, only for what the context would not allow | `origin`, `permissions` (Chromium's names, or `microphone`, `camera`, `display-audio`, `display`) | `{accept:true\|false}` (all or none) or `{cancel:true}` | the context's allowed set |
  | `auth` | `GetAuthCredentials`, on a tab the agent does not hold, or without matching agent credentials, or after they were rejected | `url`, `host`, `port`, `realm`, `scheme`, `proxy` | `{username, password}` or `{cancel:true}` | the agent's `network.credentials` if they match, else cancel |
  | `certificate` | `OnSelectClientCertificate` with at least one certificate | `host`, `port`, `proxy`, `certificates:[{index, subject, issuer, validFrom, expires}]` (Unix ms) | `{index}` or `{cancel:true}` | `Select(nullptr)`: send none |
  | `download` | a download starts on a tab the agent does not hold, for an owner with a host | `url`, `suggestedName`, `mimeType`, `totalBytes?` | `{path:ABS}` or `{cancel:true}` | it stays where today's policy saved it |

- **Ids** are `KIND:PAGE:N`. `prompt.closed {prompt, page, outcome}` says
  `accepted`, `dismissed` (today's policy answered it, or the host left),
  `closed` (a navigation or the page's end dropped it), or for downloads
  `failed` with `error`. A download's `accepted` adds the saved `path`.
- **Only a host answers.** An agent connection answering a person's prompt
  gets `forbidden`. A second answer gets `prompt_gone`. An answer that does
  not fit the kind gets `invalid_request`, and the prompt stays.
- **Paths are checked first.** Each chooser path must be absolute and exist.
  It must be a readable regular file, or a directory for `openFolder`. A
  `save` or `download` path must not be a directory, and its directory must
  be writable. Otherwise the answer is refused with `invalid_path`, and the
  message names the bad path. The prompt stays open.
- **Downloads.** Today's policy has already started the file into the
  profile's download directory when the prompt opens. An answer with `path`
  has the engine copy it there once it completes (`download` `saveAs`, on a
  thread of its own with the transport's longest wait). `{cancel:true}`
  cancels the download.
- **HTTP auth on an agent's tab.** When the agent holds the tab and its
  `network.credentials` match, the session answers `{policy:true}` itself,
  so no prompt reaches the host. The engine
  (`SuiteCutAgentAuthTries`) remembers that try by proxy, host, port,
  scheme and realm. The same challenge again before a main-frame document
  commits means the server rejected them, so the engine reports it without
  `agentCredentials` and the person is asked. The record clears at the main
  frame's load start. A subresource that asks again for a realm that already
  succeeded on the page goes to the person too. The engine's Fetch auth handler defers to
  `GetAuthCredentials` while prompts are on, so every challenge comes
  there. A proxy with credentials configured gets them at once.
- **A navigation** drops the document's file choosers and media requests,
  reported `closed`. The engine does this at the main frame's load start,
  since Chromium resets dialog state only on a page that has shown a dialog. Permission prompts close when Chromium dismisses them.
  A host's `tabs.navigate` dismisses only held dialogs first, not these.
- **Secrets.** The fake engine writes `<redacted>` for a password in its
  log. The real-engine test checks that the answered password appears in no
  event, no `tabs.describe` output, and no file under the session
  directory.
- **`hello`** lists `prompts.person` when host events are on.
- **`embed.h`**: `sc_prompt_answer_files`, `sc_prompt_answer_permission`,
  `sc_prompt_answer_login`, `sc_prompt_answer_certificate`,
  `sc_prompt_answer_download` and `sc_prompt_cancel` (a dialog gets
  `accept:false`, anything else `cancel:true`).
- **The sample host** shows open and save panels for file choosers and
  downloads, and sheets for permissions, logins and certificates. It was
  compiled on the Mac, not run against an engine.

**Not built, or not checked:**

- Client certificates were not exercised on a real engine: that needs a TLS
  server that asks for one and an NSS database with a certificate.
- The Fetch handler's `Default` answer was checked only on the box's
  engine with no agent credentials set.
- A file chooser left when its host goes is cancelled. CEF's own dialog
  cannot be shown later in a headless engine.
- No macOS engine with host events has been built, so none of this has run
  in the sample.

**Navigation's commit: fixed under the switch.** With host events,
`tabs.open` and `tabs.navigate` send the engine `navigateFrame` with
`waitUntil:"commit"` (`page.goto`'s path). The old `navigate` command waited
for the load to end. A page whose image hangs now returns from
`sc_tab_navigate` at once while it still loads. The first navigation of a
new tab then replaces its `about:blank`, so nothing may be behind it.
Without the switch the old command stays, so protocol 0's timing and
history do not change.

### Rules for all of it

- **No limits of SuiteCut's own.** CEF and Chromium enforce theirs
  ([client ARCHITECTURE.md](../../native/client/ARCHITECTURE.md)). Where a
  peer has a limit, it declares it, and the service respects it.
- **Dispatched mutations are never replayed.** A timeout reports an unknown
  outcome, as today (`control_session.cpp:53`, `:58`).
- **No callbacks.** The library never calls host code from its threads.
  Everything is polled with a timeout.

### Session protocol 1

**Framing.** Unchanged: a `u32` little-endian length and a UTF-8 JSON object.

**Messages.**

```json
{"v":1,"id":7,"op":"tabs.open","params":{"url":"https://example.com/"}}
{"v":1,"id":7,"ok":true,"result":{"page":4,"url":"about:blank","control":"agent","generation":2}}
{"v":1,"id":7,"ok":false,"error":{"code":"human_in_control","message":"…","retryable":false}}
```

- The presence of `"v"` marks protocol 1. A connection keeps the protocol of
  its first message, and a protocol 1 connection must begin with `hello`.
- `id` is the client's integer and is echoed back. A request connection has
  one request in flight, as today. Events have their own long-poll (below),
  so no message arrives unasked.

**Handshake.**

```json
{"v":1,"id":1,"op":"hello","params":{
  "protocols":[1],
  "client":{"name":"example-host","version":"1.4.0"},
  "owner":"chat-42", "role":"host", "grant":null,
  "accept":{"maxMessageBytes":8388608},
  "capabilities":["events","prompts","input.ime"]}}
```

```json
{"v":1,"id":1,"ok":true,"result":{
  "protocol":1,
  "service":{"name":"suitecut-session","version":"0.2.0"},
  "runtime":{"version":"0.2.0","cef":"156.0.30+g6028375+chromium-156.0.8078.96","chromium":"156.0.8078.96"},
  "session":"example", "owner":"chat-42", "role":"host", "tools":["core","debug"],
  "engine":{"pid":5123,"pidVersion":88812},
  "capabilities":["tabs","views","view.sink.mach-bootstrap","view.sink.library","input.pointer","input.key","input.text","events","agent.call","grants"]}}
```

- The owner and role are bound to the connection; requests do not repeat
  them. Acting as another owner needs another connection.
- A capability is used only if both sides list it. Unknown capabilities,
  fields and event types are ignored, and an unknown error code is treated
  as `failed`.
- Adding ops, optional fields, events, error codes or capabilities does not
  change the protocol number. Removing something, or changing what it means,
  does. A service may speak several protocols.

**Detecting an old service.** A protocol 0 service answers `hello` with
`{"ok":false,"error":{"code":"invalid_owner",…}}`, because the message has
no top-level `owner` (`shared_session.cpp:127-129`). Rule: a reply to
`hello` without `"v"` means protocol 0. The C library then fails with
`SC_UNSUPPORTED`, unless the host set `SC_SESSION_ALLOW_PROTOCOL_0`. In that
case the library speaks protocol 0 and reports only protocol 0 capabilities.

**Keeping the installed Napdock helper working.** The new service accepts
protocol 0 on the same socket, unchanged:

- The same keys, ops, fields, replies and error codes as Part 1.
- The same CLI (`--session`, `--session-dir`, `--browser`, `--profile`,
  `--scale`, `--idle-ms`, `start|status|close`).
- The same socket path.
- The same binary name inside the bundle: `Contents/MacOS/suitecut-shared-session`
  stays as a second, identically built Mach-O beside `suitecut-session`,
  because the helper checks that exact path and its signature
  (`Runtime.swift:33-47`).

A protocol 0 connection acts with role `host` for whatever owner each
message names, as today. Grants do not apply to it unless the service runs
with `--protocol-0 off`. Then every protocol 0 message gets
`{"ok":false,"error":{"code":"protocol_unsupported","message":"…"}}`, which
the helper shows as `failed: protocol_unsupported: …`.

Protocol 0 keeps today's behaviour: an agent call takes an `unclaimed` tab
after it has run, and `watch` wakes on any owner's change. Protocol 1
connections get the stricter rules below.

**Roles and grants.**

| Role | May |
| ---- | --- |
| `host` | everything for its owner: tabs, views, input, take and release, answering prompts, agent calls. `session.close` and `session.grant`. |
| `agent` | agent calls, `tabs.list`, `tabs.open` (always as `agent`), `tabs.close` and `tabs.release` of tabs it controls, events. No views, no input, no `session.close`. Only the tool groups in its grant. |

- `session.grant {owner, role:"agent", tools:["core"], ttlMs?}` returns an
  opaque random token (32 bytes, hex). A host may grant only its own owner.
  `session.revoke {grant}` withdraws one.
- With `--grants required`, an agent's `hello` without a valid grant gets
  `not_granted`; a host needs none. With `--grants optional` (the default
  while protocol 0 is served), a same-user client without a grant is
  whichever role it asks for.
- Grants are about scope: one owner, chosen tools. They are not a defence
  against malware running as the same user, which can read the profile
  anyway.
- Napdock's panel helper and its agent process cannot talk to each other
  directly (decision record, lines 118-120). Its executor would have to mint
  the grant through the library, or run with `--grants optional`.

**Ops.**

| Op | Role | Params | Result |
| -- | ---- | ------ | ------ |
| `hello` | any | above | above |
| `session.status` | any | – | `{pid, session, idleTimeoutMs, protocols:[0,1]}` |
| `session.close` | host | – | `{closed:true}`; all owners' tabs close, as `_session_close` does today |
| `session.grant`, `session.revoke` | host | above | `{grant}` / `{revoked:true}` |
| `tabs.list` | any | – | `{tabs:[Tab], cursor}` |
| `tabs.open` | any | `url?`, `by?` (host only: `human`\|`agent`) | `Tab` |
| `tabs.close` | host; agent on its tabs | `page` | `{closed:true}` |
| `tabs.navigate` | host | `page`, `url` or `history` | `Tab`; takes the tab |
| `tabs.take` | host | `page` | `Tab` |
| `tabs.release` | host: `human`→`unclaimed`; agent: `agent`→`unclaimed` | `page` | `Tab` |
| `view.attach` | host | `page`, `sink`, `scale?`, `replace?` | `{view, engine:{pid,pidVersion}, pixelWidth, pixelHeight}` |
| `view.resize` | host | `view`, `width`, `height` (CSS px), `scale?` | `{pixelWidth, pixelHeight}` |
| `view.focus` | host | `view`, `focused` | `{}` |
| `view.detach` | host | `view` | `{detached:true}` |
| `view.screenshot` | any | `page`, `format:"png"`, `to:"inline"`\|`{"file":ABS}` | `{base64,…}` or `{path,width,height}` |
| `input.pointer` | host | `view`, `action:move\|down\|up`, `x`, `y` (CSS px), `button`, `clickCount`, `modifiers` | `{}` |
| `input.wheel` | host | `view`, `x`, `y`, `deltaX`, `deltaY`, `modifiers` | `{}` |
| `input.key` | host | `view`, `action:down\|up`, `key`, `code?`, `modifiers`, `repeat?` | `{}` |
| `input.text` | host | `view`, `text` | `{}` |
| `input.ime.set` | host | `view`, `text`, `selection:[start,end]`, `replacement?:[start,end]` | `{}` |
| `input.ime.commit` | host | `view`, `text`, `replacement?` | `{}` |
| `input.ime.cancel` | host | `view` | `{}` |
| `input.edit` | host | `view`, `command:copy\|cut\|paste\|undo\|redo\|selectAll\|delete` | `{}` |
| `agent.call` | any | `page`, `tool`, `arguments`, `expectGeneration?` | the tool's result, plus `controlLost` if the tab was taken meanwhile |
| `agent.catalog` | any | – | the tools the role and grant allow |
| `events.next` | any | `cursor`, `timeoutMs` (0 to 25000) | `{events:[Event], cursor}` |
| `prompt.answer` | host; agent per policy | `prompt`, the answer fields | `{answered:true}` |

`Tab` is `{page, url, title, control, generation}` as today. With the
capabilities `tab.loading` and `tab.history` it adds
`loading, canGoBack, canGoForward`, and with `tab.favicon`, `favicon`.

**Views.**

- The engine keeps one output per page (`page/source.h`, `surface_output_`).
  So a second `view.attach` on the same page gets `view_busy` unless it
  passes `replace:true`.
- `scale` is per view and defaults to the session's `--scale`. It needs
  engine work: today the scale is fixed at launch.
- Input coordinates are CSS pixels of the view. They are not the 0 to 1
  fractions Napdock's app uses with its helper (`Helper.swift:445-446`).

**Which input takes a tab.** As today, plus IME and edits:

- It takes the tab: a pointer press, a key down, text, IME set or commit,
  any edit except `copy`, a navigation, a `take`.
- It does not: a pointer move, a wheel, `view.focus`, `copy`.

**Events.**

```json
{"events":[
  {"type":"tab.changed","tab":{"page":4,"url":"https://example.com/","title":"Example","control":"human","generation":3}},
  {"type":"prompt.opened","prompt":{"id":"p17","page":4,"kind":"dialog","dialog":"confirm","message":"Leave?","deadlineMs":30000}}
 ],"cursor":42}
```

| Type | Fields | Needs engine work |
| ---- | ------ | ----------------- |
| `tab.changed` | `tab` | no |
| `tab.closed` | `page` | no |
| `tab.loading` | `page`, `loading`, `canGoBack`, `canGoForward` | yes: `OnLoadingStateChange` |
| `tab.title`, `tab.favicon` | `page`, `title` / `url` | yes: `OnTitleChange` is not reported; no favicon handler |
| `view.cursor` | `view`, `cursor` (CSS cursor name or `custom` with a PNG) | yes: `OnCursorChange` |
| `view.ime` | `view`, `range`, `caretBounds` (to place the candidate window) | yes |
| `prompt.opened`, `prompt.closed` | `prompt` / `prompt`, `outcome` | yes, except dialogs |
| `download.updated` | `page`, `id`, `url`, `state`, `receivedBytes`, `totalBytes`, `path?` | yes: reported from the download controller |
| `resync` | – | no |
| `engine.exited` | – | no |

- The cursor counts events of the connection's owner only.
- Events are kept while at least one `events.next` reader of that owner is
  connected. When none is, only state is kept, and the next reader gets
  `resync` first and should call `tabs.list`. That bounds memory without a
  limit of SuiteCut's own.

**Prompts.**

- `kind` is one of:
  - `dialog` (`alert`, `confirm`, `prompt`, `beforeunload`)
  - `permission` (`permissions: [...]`, `origin`)
  - `file` (`mode: open|openMultiple|openFolder|save`, `accept`)
  - `download` (`suggestedName`, `url`, `mimeType`)
  - `auth` (`url`, `host`, `port`, `realm`, `scheme`, `proxy`)
  - `certificate` (`host`, `port`, `certificates: [...]`)
- The matching answers:
  - `{accept, text?}`
  - `{accept}` for a permission, all of it or none
  - `{paths:[ABS]}` or `{cancel:true}`
  - `{path:ABS}` or `{cancel:true}`
  - `{username, password}` or `{cancel:true}`
  - `{index}` or `{cancel:true}`
  - any of them `{policy:true}`, for today's answer
- The first answer wins; later ones get `prompt_gone`. A dialog left
  unanswered takes the engine's default after 30 s
  (`dialog_controls.h:34`), and `prompt.closed` says `timeout`. A person's
  prompt has no deadline (built as "Prompts that go to the person" says).
- When an agent holds the tab, the agent may answer `dialog` (through its
  tools, as today). The person, through the host, answers `permission`
  (camera, microphone, location, notifications), `file` choosers that read
  the person's files, `auth` (HTTP authentication and client certificates)
  and `download` to disk. This was decided by delegation on 2026-11-20
  (decision 6 in the direction document).
- The engine work: permissions are answered by policy today
  (`lifecycle.cc:126-147`); file dialogs with nothing pending fall through to
  CEF (`form_controller.inc:455-463`); downloads and auth are policy; dialogs
  already wait for an answer once subscribed (`dialog_controls.h:25-36`).
- A session in which no host supports `prompts` keeps today's policies.

**Errors.** Every error has `code`, `message` and `retryable` (true only for
`busy`):

| Code | Meaning |
| ---- | ------- |
| `protocol_unsupported` | no common protocol, or protocol 0 is off |
| `hello_required` | a protocol 1 connection began with another op |
| `invalid_request`, `invalid_owner`, `invalid_input`, `invalid_url` | as protocol 0 |
| `not_granted` | the grant is missing, expired or for another owner |
| `forbidden` | the role may not use this op or tool |
| `unknown_tool`, `unknown_page`, `unknown_view`, `unknown_prompt` | missing, or another owner's |
| `view_busy` | the page already has a view |
| `human_in_control` | a person holds the tab |
| `stale_generation` | `expectGeneration` does not match |
| `prompt_gone` | already answered or expired |
| `reply_too_large` | over the client's `maxMessageBytes`; for screenshots use `to:{file}` |
| `navigation_failed`, `engine_exited`, `unsupported`, `busy`, `failed` | as protocol 0 |

**Ownership and control rules (normative).**

1. A tab belongs to one owner for its whole life. A popup gets its opener's
   owner and control.
2. `control` is `unclaimed`, `agent` or `human`. `generation` increases on
   every control change and at no other time.
3. Input that takes a tab, a host navigation, and `tabs.take` make it
   `human`.
4. An agent call on a `human` tab fails with `human_in_control` and changes
   nothing.
5. An agent call on an `unclaimed` tab makes it `agent` before the call
   runs.
6. An agent call whose `expectGeneration` differs from the tab's fails with
   `stale_generation`.
7. A person taking a tab during an agent call does not cancel it (the engine
   cannot preempt). Its result carries `controlLost:true`, and later calls
   are refused.
8. An owner never sees another owner's tabs, views or prompts. They read as
   `unknown_*`.
9. Agent calls of one owner run one at a time; those of different owners
   do not wait for each other. Built in phase 1 with one `AgentDriver` per
   owner over the shared `Browser`.

**Limits and timeouts.**

- No size limits beyond the 4-byte length.
- A message must arrive within 2 s of its first byte, and a reply drains for
  up to 10 s, as today.
- `events.next` waits up to 25 s, as `watch` does today.
- A client's own `maxMessageBytes` is respected (`reply_too_large`), so
  Napdock's 8 MB reply limit stops being a silent failure.

**Version and protocol range of a runtime, before running it.** Each runtime
bundle carries a manifest inside its signed contents
(`Contents/Resources/suitecut-runtime.json` on macOS, `share/suitecut/runtime.json`
on Linux). It extends the existing `runtime-manifest.json`
(`package-native-runtime-macos.py:211-222`):

```json
{"formatVersion":2,"name":"SuiteCutRuntime","version":"0.2.0",
 "protocols":{"session":[0,1]},"embedAbi":1,
 "cef":{"version":"156.0.30+g6028375+chromium-156.0.8078.96","codecs":"none"},
 "platform":"macos","architecture":"arm64","minimumOS":"14.0",
 "programs":{"engine":"Contents/MacOS/suitecut-browser","service":"Contents/MacOS/suitecut-session"},
 "notarized":true}
```

`suitecut-session --version --json` prints the same fields for hosts that
prefer to ask a program.

`codecs` is `"none"` for a distributed runtime: it defaults to the official CEF
build without H.264 and AAC (decision 3). A local build from the codec SDK
reports `"proprietary"`. FFmpeg is not in the bundle; a host that wants
recording passes its own FFmpeg path (decision 4).

### C ABI additions (sketch)

This is the design as drafted before phase 2. The header that was built
differs in the places listed under "Built in phase 2"; read
[embed.h](../../native/client/include/suitecut/embed.h) for the real one.

A new header, `suitecut/embed.h`, in the same `SuiteCut::Client` library. It
follows `client.h`: `struct_size` plus `abi_version` in every options
struct, opaque handles, `sc_status` with `sc_error`, idempotent close,
destroy only with no call in flight, and no callbacks.

**ABI v1 callers stay unbroken.**

- `client.h` and `SC_ABI_VERSION` (1) do not change. The library checks
  `abi_version == SC_ABI_VERSION` and `struct_size == sizeof` exactly
  ([c_api/client.cpp:62-64](../../native/client/src/c_api/client.cpp)), so
  raising `SC_ABI_VERSION` would reject every compiled v1 caller.
- The embedding structs carry `SC_EMBED_ABI_VERSION` instead.
- New status values are macros of type `sc_status`, returned only by
  `embed.h` functions, so code written against v1 never sees them.

```c
/* SPDX-License-Identifier: MIT */
#ifndef SUITECUT_EMBED_H
#define SUITECUT_EMBED_H
#include "suitecut/client.h"
#ifdef __cplusplus
extern "C" {
#endif

#define SC_EMBED_ABI_VERSION 1u
/* Returned only by embed.h functions. A refusal's message starts with the
 * protocol's error code and a colon: "human_in_control: The person ...". */
#define SC_REFUSED ((sc_status)5)
#define SC_UNSUPPORTED ((sc_status)6)
#define SC_CLOSED ((sc_status)7)

typedef struct sc_session sc_session;
typedef struct sc_view sc_view;
typedef struct sc_event sc_event;
typedef struct sc_json sc_json;          /* an owned UTF-8 JSON document */

SC_API uint32_t sc_embed_abi_version(void);
/* Copies the protocol code of an SC_REFUSED error into |code|; 0 if none. */
SC_API size_t sc_error_refusal(const sc_error *error, char *code, size_t code_size);
SC_API const char *sc_json_data(const sc_json *json, size_t *size);
SC_API void sc_json_destroy(sc_json *json);

/* ---- Runtime: find, verify and read a runtime without running it. ---- */
typedef struct sc_runtime_options {
  size_t struct_size;
  uint32_t abi_version;
  const char *path;                 /* bundle (macOS) or install root; required */
  const char *signing_requirement;  /* macOS: code requirement string; NULL skips (development only) */
  uint32_t protocol_min, protocol_max; /* what the host speaks */
} sc_runtime_options;
typedef struct sc_runtime_info {
  size_t struct_size;
  uint32_t abi_version;
  char version[32], cef_version[96], chromium_version[32], minimum_os[16];
  uint32_t protocol_min, protocol_max, embed_abi;
  uint32_t signature_verified;      /* 1 only when signing_requirement was checked and met */
  char engine_path[1024], service_path[1024];
} sc_runtime_info;
SC_API void sc_runtime_options_init(sc_runtime_options *options);
SC_API void sc_runtime_info_init(sc_runtime_info *info);
/* SC_UNSUPPORTED when the protocol ranges do not overlap. */
SC_API sc_status sc_runtime_inspect(const sc_runtime_options *options, sc_runtime_info *out, sc_error *error);

/* ---- Session: start or join as an owner. ---- */
typedef enum sc_session_role { SC_ROLE_HOST = 1, SC_ROLE_AGENT = 2 } sc_session_role;
#define SC_SESSION_START 1u              /* start the service if it is not running */
#define SC_SESSION_ALLOW_PROTOCOL_0 2u   /* fall back to the unversioned protocol */
typedef struct sc_session_options {
  size_t struct_size;
  uint32_t abi_version;
  const sc_runtime_info *runtime;  /* required with SC_SESSION_START */
  const char *name;                /* session name, [A-Za-z0-9._-] */
  const char *directory;           /* absolute private directory */
  const char *profile;             /* absolute; used only when starting */
  const char *owner;
  sc_session_role role;
  const char *grant;               /* NULL when none */
  const char *client_name, *client_version;
  double default_scale;            /* 0 keeps the service default */
  uint32_t idle_ms;                /* 0 keeps the service default */
  uint32_t timeout_ms;             /* per request; 0 means 30000 */
  size_t max_message_bytes;        /* this host's own limit; 0 means none */
  uint32_t flags;
} sc_session_options;
SC_API void sc_session_options_init(sc_session_options *options);
SC_API sc_status sc_session_open(const sc_session_options *options, sc_session **out, sc_error *error);
SC_API uint32_t sc_session_protocol(const sc_session *session);
/* The hello result as JSON: capabilities, engine pid, versions. */
SC_API sc_status sc_session_describe(sc_session *session, sc_json **out, sc_error *error);
SC_API sc_status sc_session_grant(sc_session *session, const char *tools_csv, uint32_t ttl_ms,
                                  char *token, size_t token_size, sc_error *error);
/* Closes this handle's connections. shutdown != 0 also asks the service to
 * close every owner's tabs and stop (host role only). Idempotent. */
SC_API sc_status sc_session_close(sc_session *session, int shutdown, sc_error *error);
SC_API void sc_session_destroy(sc_session *session);

/* ---- Tabs ---- */
typedef enum sc_history { SC_HISTORY_BACK = 1, SC_HISTORY_FORWARD = 2, SC_HISTORY_RELOAD = 3 } sc_history;
typedef struct sc_tab {
  size_t struct_size;
  uint32_t abi_version;
  int32_t page;
  uint32_t control;          /* 0 unclaimed, 1 agent, 2 human */
  uint64_t generation;
} sc_tab;
SC_API void sc_tab_init(sc_tab *tab);
SC_API sc_status sc_tab_list(sc_session *session, sc_json **out, sc_error *error);
SC_API sc_status sc_tab_open(sc_session *session, const char *url /* NULL: blank */, int for_agent,
                             sc_tab *out, sc_error *error);
SC_API sc_status sc_tab_close(sc_session *session, int32_t page, sc_error *error);
SC_API sc_status sc_tab_navigate(sc_session *session, int32_t page, const char *url, sc_tab *out, sc_error *error);
SC_API sc_status sc_tab_history(sc_session *session, int32_t page, sc_history step, sc_tab *out, sc_error *error);
SC_API sc_status sc_tab_take(sc_session *session, int32_t page, sc_tab *out, sc_error *error);
SC_API sc_status sc_tab_release(sc_session *session, int32_t page, sc_tab *out, sc_error *error);

/* ---- Views and frames ---- */
typedef enum sc_sink_kind {
  SC_SINK_LIBRARY = 1,         /* the library receives and checks frames; read them with sc_view_next_frame */
  SC_SINK_MACH_BOOTSTRAP = 2,  /* macOS: a bootstrap name the host checked in itself */
  SC_SINK_UNIX_FD = 3,         /* Linux, untested proposal: an AF_UNIX path the host listens on */
  SC_SINK_D3D11 = 4            /* Windows, untested proposal: shared NT handles */
} sc_sink_kind;
typedef struct sc_surface_sink {
  size_t struct_size;
  uint32_t abi_version;
  sc_sink_kind kind;
  const char *name;            /* SC_SINK_MACH_BOOTSTRAP */
  const char *path;            /* SC_SINK_UNIX_FD */
  uint64_t adapter_luid;       /* SC_SINK_D3D11 */
} sc_surface_sink;
typedef struct sc_view_options {
  size_t struct_size;
  uint32_t abi_version;
  int32_t page;
  const sc_surface_sink *sink;
  int css_width, css_height;
  double scale;                /* 0 keeps the session default */
  int replace;
} sc_view_options;
#define SC_PIXEL_NV12_FULL_601 0x34323066u /* '420f' */
typedef struct sc_frame {
  size_t struct_size;
  uint32_t abi_version;
  uint32_t width, height, pixel_format;
  uint64_t timestamp_us;
  /* macOS: handles[0] is a send right to the IOSurface, now owned by the
   * caller, who must raise the surface's use count (IOSurfaceIncrementUseCount
   * or a CVPixelBuffer over it) before reading and drop it when done.
   * Linux (proposal): handles[i] are dmabuf fds owned by the caller, with
   * strides, offsets and modifier. Windows (proposal): handles[0] is an NT handle. */
  uint64_t handles[4];
  uint32_t handle_count, strides[4];
  uint64_t offsets[4], modifier;
} sc_frame;
SC_API void sc_surface_sink_init(sc_surface_sink *sink);
SC_API void sc_view_options_init(sc_view_options *options);
SC_API void sc_frame_init(sc_frame *frame);
SC_API sc_status sc_view_attach(sc_session *session, const sc_view_options *options, sc_view **out, sc_error *error);
SC_API sc_status sc_view_resize(sc_view *view, int css_width, int css_height, double scale, sc_error *error);
SC_API sc_status sc_view_set_focus(sc_view *view, int focused, sc_error *error);
/* SC_SINK_LIBRARY only. Returns the newest frame; older waiting frames are
 * released. SC_TIMEOUT when none arrived. Frames from any process other than
 * the engine (pid and pid version) are discarded. */
SC_API sc_status sc_view_next_frame(sc_view *view, uint32_t wait_timeout_ms, sc_frame *out, sc_error *error);
SC_API void sc_frame_release(sc_frame *frame);
SC_API sc_status sc_view_detach(sc_view *view, sc_error *error);
SC_API void sc_view_destroy(sc_view *view);

/* ---- Input, in CSS pixels of the view ---- */
#define SC_MOD_SHIFT 8u
#define SC_MOD_CONTROL 2u
#define SC_MOD_ALT 1u
#define SC_MOD_META 4u        /* the CDP modifier bits the engine already uses */
typedef enum sc_pointer_action { SC_POINTER_MOVE = 1, SC_POINTER_DOWN = 2, SC_POINTER_UP = 3 } sc_pointer_action;
typedef enum sc_button { SC_BUTTON_NONE = 0, SC_BUTTON_LEFT = 1, SC_BUTTON_RIGHT = 2, SC_BUTTON_MIDDLE = 3 } sc_button;
typedef struct sc_pointer_event {
  size_t struct_size; uint32_t abi_version;
  sc_pointer_action action; double x, y; sc_button button; uint32_t click_count, modifiers;
} sc_pointer_event;
typedef struct sc_wheel_event {
  size_t struct_size; uint32_t abi_version;
  double x, y, delta_x, delta_y; uint32_t modifiers;
} sc_wheel_event;
typedef struct sc_key_event {
  size_t struct_size; uint32_t abi_version;
  int down; const char *key; const char *code; uint32_t modifiers; int repeat;
} sc_key_event;
typedef enum sc_edit { SC_EDIT_COPY = 1, SC_EDIT_CUT, SC_EDIT_PASTE, SC_EDIT_UNDO, SC_EDIT_REDO,
                       SC_EDIT_SELECT_ALL, SC_EDIT_DELETE } sc_edit;
/* Moves coalesce inside the library: only the newest waiting move is sent. */
SC_API sc_status sc_view_pointer(sc_view *view, const sc_pointer_event *event, sc_error *error);
SC_API sc_status sc_view_wheel(sc_view *view, const sc_wheel_event *event, sc_error *error);
SC_API sc_status sc_view_key(sc_view *view, const sc_key_event *event, sc_error *error);
SC_API sc_status sc_view_text(sc_view *view, const char *utf8, size_t size, sc_error *error);
SC_API sc_status sc_view_ime_set(sc_view *view, const char *utf8, size_t size, int32_t selection_start,
                                 int32_t selection_end, int32_t replace_start, int32_t replace_end, sc_error *error);
SC_API sc_status sc_view_ime_commit(sc_view *view, const char *utf8, size_t size, int32_t replace_start,
                                    int32_t replace_end, sc_error *error);
SC_API sc_status sc_view_ime_cancel(sc_view *view, sc_error *error);
SC_API sc_status sc_view_edit(sc_view *view, sc_edit command, sc_error *error);

/* ---- Agent calls ---- */
SC_API sc_status sc_agent_catalog(sc_session *session, sc_json **out, sc_error *error);
/* expect_generation 0 skips the check. */
SC_API sc_status sc_agent_call(sc_session *session, int32_t page, const char *tool, const char *arguments_json,
                               size_t arguments_size, uint64_t expect_generation, uint32_t timeout_ms,
                               sc_json **out, sc_error *error);

/* ---- Events and prompts ---- */
typedef enum sc_event_kind {
  SC_EVENT_OTHER = 0, SC_EVENT_TAB_CHANGED = 1, SC_EVENT_TAB_CLOSED = 2, SC_EVENT_TAB_LOADING = 3,
  SC_EVENT_VIEW_CURSOR = 4, SC_EVENT_VIEW_IME = 5, SC_EVENT_PROMPT_OPENED = 6, SC_EVENT_PROMPT_CLOSED = 7,
  SC_EVENT_DOWNLOAD = 8, SC_EVENT_RESYNC = 9, SC_EVENT_ENGINE_EXITED = 10
} sc_event_kind;
/* Long-polls the session's event cursor. SC_TIMEOUT when nothing happened. */
SC_API sc_status sc_session_next_event(sc_session *session, uint32_t wait_timeout_ms, sc_event **out, sc_error *error);
SC_API sc_event_kind sc_event_kind_of(const sc_event *event);
SC_API int32_t sc_event_page(const sc_event *event);
SC_API const char *sc_event_json(const sc_event *event, size_t *size);
SC_API void sc_event_destroy(sc_event *event);
SC_API sc_status sc_prompt_answer(sc_session *session, const char *prompt_id, const char *answer_json,
                                  size_t answer_size, sc_error *error);

#ifdef __cplusplus
}
#endif
#endif
```

**Threading.**

- Every function may be called from any thread. Each blocks for at most its
  timeout.
- A session keeps one request connection under a lock, one connection for
  events, and one per view for input. Pointer moves coalesce on the view's
  connection, as Napdock's `StreamSender` does today (`Session.swift:49-109`).
- A library sink owns one receiving thread per view. It queues frames and
  never calls the host.
- The C++ API gets matching `suitecut::Session` and `suitecut::View` classes
  over the same implementation.

**The plain-control path.** `sc_client_*` is unchanged. A program that wants
plain control of a session's tab uses `sc_agent_call` with the driver's
tools. One that wants raw engine commands keeps `sc_client_launch`. Exposing
raw engine commands through a session is deliberately left out, because it
would bypass ownership.

### Surface sinks per platform

**macOS (built; `SC_SINK_MACH_BOOTSTRAP` is today's path).** The host checks
in a name; the frame message is as in Part 1. Additions:

- The engine reports its `pidVersion` (`audit_token_to_pidversion` on its
  own task's audit token), and receivers compare it with the trailer as well
  as the process id.
- The library's sink validates the same fields Napdock's helper does
  (`BrowserhostMach.c:55-67`), without a side limit, and requires the
  surface's pixel format to be `420f`.
- The reuse rule is part of the contract: a receiver raises the surface's
  use count before reading it.

`SC_SINK_LIBRARY` on macOS is the same message received by the library.

**Linux (proposal, untested).**

- The host listens on an AF_UNIX path in a private directory.
- The engine connects, and for each frame sends a small header
  (`'GBS1'`, width, height, fourcc, plane count, strides, offsets, modifier,
  timestamp) with the dmabuf fds as `SCM_RIGHTS`.
- The host checks `SO_PEERCRED` against the engine's pid.
- The engine must copy CEF's planes into buffers it allocates (GBM or
  Vulkan), because CEF's fds are valid only during the callback.
- CEF 156 allocates Linux off-screen frames from GBM when shared textures
  are on. Without a render node it delivers nothing, so the engine turns
  them on only after checking for one, and uses CPU frames otherwise. No
  sink in this contract carries CPU frames yet; a shared-memory sink for
  Linux machines without a GPU is phase 6 work.
- Still unknown, for a machine with a GPU: which formats and modifiers;
  whether the descriptors import in another process; and how to fence
  (implicit sync or sync files).

**Windows (proposal, untested; needs an engine port first).**

- The host creates a named pipe and passes its adapter LUID.
- The engine copies CEF's texture into a texture it owns on that adapter,
  creates an NT handle, and duplicates it into the host process
  (`DuplicateHandle` with the host's pid from the pipe,
  `GetNamedPipeClientProcessId` for the check in both directions).
- It sends the handle value with the frame header. Without a keyed mutex
  (CEF's own texture has none), reuse follows a rule like macOS's: the host
  acknowledges a frame before the engine draws into it again.

### MCP joining a session

```bash
suitecut-native-control --mcp --join example --session-dir /abs/run --owner chat-42 --grant-env SUITECUT_GRANT --tools core,debug
```

- **Joining.** `--join NAME` joins an existing session and never launches a
  browser. With `--start --runtime PATH` it may start the session the way
  the library does.
- **Identity.** It sends `hello` with role `agent` and the grant read from
  the named environment variable, which it then removes from its
  environment, as it already does for the credential vault
  (`control.cpp:196`).
- **Tools.** The tool list is what the grant allows, narrowed by `--simple`
  or `--tools`. A flag can narrow the grant but never widen it.
- **Napdock's behaviour, ported.** A `tabs` tool (`list`, `open`, `close`,
  `release`); an optional `page` argument when the owner has one tab;
  `navigate` without `page` opens the first tab; `human_in_control` and
  `stale_generation` returned as tool errors with their code; and
  instructions that give the call order and the untrusted-content rule.
- **Host limits.**
  - The grant's tool groups.
  - The service's `--allow-schemes` for every navigation, agent ones
    included.
  - `--simple` for observe, act and wait only.
  - The prompt policy (which prompt kinds an agent may answer).
- **What it does not do.** `--join` cannot be combined with named commands
  or `--connect`.

### The session service as a product target

- Built: `native/client/session/`, in the client CMake project
  (`SUITECUT_CLIENT_BUILD_SESSION`, on by default except on Windows). It
  builds `suitecut-session` against `SuiteCut::Client`.
- Modules:
  - `control_session.cpp`: socket, startup lock, accept loop, peer checks.
  - `host.cpp`: tabs, control, per-owner agents, events and grants.
  - `protocol.cpp`: protocol 0 and 1 parsing and replies.
  - `main.cpp`: the CLI.
- `suitecut-native-control` keeps its single-client named sessions. The
  shared socket code is a private static library used by both programs.
- The `suitecut-shared-session` name ships as a second program built from
  the same objects, for as long as protocol 0 is served. The Napdock
  packager places the service under both names.

| Today | Becomes |
| ----- | ------- |
| Lives in `examples/`, built on every platform | Product target; macOS and Linux; Windows refused at configure time until ported |
| Only http and https (`shared_session.cpp:75-81`), and only for the person | `--allow-schemes`, default `http,https`, for tab opens and a person's navigations; agent navigations later |
| Tools fixed to `core,debug,media` (`:107`) | `--tools` and grants |
| Scale fixed at launch, default 2 (`:595`, `:609`) | Per view; `--scale` is the default |
| Idle default 30 min (`:597`) | Unchanged; already an option |
| Bootstrap name checked in the session (`:293`) | Checked by the platform sink code, one rule shared with the engine |
| Titles polled with `Runtime.evaluate` (`:430-445`) | Engine title events |
| Screenshot options fixed (`:308-311`) | Parameters, and `to:{file}` |
| `tabs.open` defaults to `by:"agent"` (`:221`) | Decided by the role |
| Default directory `/tmp/suitecut-control-<uid>` (`control_session.cpp:94`) | Unchanged for compatibility; hosts pass `--session-dir` |
| `getpeereid` | `getpeereid` on macOS, `SO_PEERCRED` on Linux |
| Every same-user client may close the session | `session.close` is host-only, and a protocol 1 connection cannot send `_session_close`; a protocol 0 connection still can |
| One lock for every owner's agent calls | Per owner (rule 9), built |
| No tests | Built: see "Tests" |

### A second sample host

Built in phase 2 (`native/client/examples/embed-macos/`). It is part of the
client project's examples on macOS and is not installed. It builds
`suitecut-embed-sample`, an AppKit app in about 490 lines of Objective-C++,
using only `embed.h`:

- One window: a bar with back, forward, reload, an address field, a
  Take over / Hand back button and a status line, over a view whose
  `CALayer` shows the frames.
- It starts from `--runtime PATH`, with an optional
  `--signing-requirement`. It starts or joins session `suitecut-sample` as
  owner `sample`, opens one tab and attaches an `SC_SINK_LIBRARY` view the
  size of its view.
- A thread waits in `sc_view_next_frame` and hands each frame to the main
  thread. The main thread sets the layer's contents to the surface and
  releases the frame that was on screen before it.
- Mouse, scroll and keys go to `sc_view_pointer`, `sc_view_wheel` and
  `sc_view_key`. Keys that are not text map to DOM key names, and modifier
  changes become key presses and releases. Text goes to `sc_view_text`.
  Resizing the window resizes the view.
- An event thread posts `tab.changed` to the main queue, which updates the
  address, the button and the status line.
- Quitting shuts its session down unless `--keep-session` is given.
- `--click X,Y`, `--type TEXT`, `--snapshot PNG` and `--exit-after SECONDS`
  make an unattended run. The snapshot is the frame on screen at quit.

What it proves:

- Nothing in the API is named after Napdock.
- A host can receive frames directly, with no relay process.
- Owners are the host's own strings.

Phase 3 added to it (compiled on the Mac, not yet run against an engine
with host events): the window title from `tab.title`; the reload button
stopping a load, and back and forward following `canGoBack` and
`canGoForward`; the page's cursor as an `NSCursor`; dialogs as `NSAlert`
sheets answered with `sc_prompt_answer_dialog` (session flag
`SC_SESSION_PROMPTS`); `NSTextInputClient` for IME; and an Edit menu whose
commands go to `sc_view_edit`. Item 5 added open and save
panels and sheets for a person's prompts, also unrun. Still to add: a menu item that mints a grant and prints the
`suitecut-native-control --mcp --join …` command.

### What Napdock's helper would change

For the Napdock side to pick up; this design changes nothing there. Since
phase 2 the helper has two routes. It can keep its own protocol code and
move it to protocol 1, or it can link `SuiteCut::Client` and call `embed.h`
from Swift. The second route removes the framing, request ids, connection
handling and frame validation from the helper.

1. Find the runtime with `sc_runtime_inspect` and Napdock's signing
   requirement, or do the same by hand: look for
   `Contents/MacOS/suitecut-session` first, then `suitecut-shared-session`,
   and ask it `--version --json`. Refuse a runtime whose range excludes 1,
   or fall back to protocol 0. The runtime manifest replaces the version
   query in phase 4.
2. Send `hello` (`sc_session_open`): role `host` from `browserhost serve`,
   and `agent` from `browserhost mcp`. A helper that keeps its own code
   declares `maxMessageBytes` (8 MB today).
3. Replace `watch` with `events.next` (`sc_session_next_event`), and drop
   the duplicate check in `TabWatcher`.
4. Probe with `session.status` instead of `tabs.list` for the owner
   `browserhost`.
5. Keep the relay with `SC_SINK_MACH_BOOTSTRAP`, so the app's helper-pid
   check stays. Alternatively, the app could link the library itself and
   use `SC_SINK_LIBRARY`, with no relay. Compare the trailer's pid version
   with `engine.pidVersion` once the engine reports one.
6. Ask for screenshots with `to:{file}` (`sc_tab_screenshot`). The helper
   already writes a PNG to its data folder (`tabs.screenshot`).
7. Remove the 1920×1080 viewport limit (`Helper.swift:70`). Agree one
   largest frame side between the helper (16384) and the app (8192), or
   none.
8. Send pointer coordinates in CSS pixels. Send modifiers as held keys
   (`input.key` down and up), because input events carry no `modifiers`.
9. Phase 3, once built: per-view `scale` from the panel's backing scale;
   IME (`input.ime.*`) and Cmd+C/V/X/Z (`input.edit`); answering
   `prompt.opened` (dialogs, file pickers, permissions, downloads, auth);
   showing `tab.loading`, titles and the cursor.
10. Optionally replace `browserhost mcp` with
    `suitecut-native-control --mcp --join`, once the executor can pass a
    grant. Otherwise keep it and send `expectGeneration`.
11. After starting the runtime, check the running engine's signature by
    process (`SecCodeCopyGuestWithAttributes`), not only the files.
12. Update the decision record: drop "at most 16" tabs, and note protocol 1.
13. Handle `engine.exited` by restarting the session once.
14. Keep "Quit browser" on protocol 0's `_session_close` until the helper
    speaks protocol 1 as a host, then use `session.close`
    (`sc_session_close` with `shutdown`).

### Tests per phase

| Phase | Tests |
| ----- | ----- |
| 0 | **Built**: `shared_session_test` against a new `tests/support/session_fake_engine.cpp`, with the Napdock envelope replay in the same file; the Linux build is still to do. **Planned**: `native/client/tests/unit/shared_session_test.cpp`, running the built tool against `tests/support/transport_fake_engine.cpp` or `browser_facade_engine.cpp` (both fake engines exist). Covers: framing (empty, partial, 2 s deadline, many requests per connection), `_session_status` and `_session_close`, owner isolation (`unknown_page`), `human_in_control`, take and release generations, popup inheritance, `engine_exited`. **Build**: a Linux configure and build of `native/client` to settle the `getpeereid` question. |
| 1 | **Built**: `session_protocol_test` in `native/client/tests/unit`; still to do: the golden fixtures as files, the script, and the real-engine workflow. **Planned**: **Protocol conformance** tests in `native/session/tests/unit` with a fake engine: `hello`, detecting protocol 0, every op and error code, roles and grants, `expectGeneration`, claiming before the call, the event cursor and `resync`, `reply_too_large`, per-owner agent concurrency. **Golden protocol 0 fixtures** (`native/session/tests/fixtures/protocol0/*.json`), recorded from the shapes Napdock sends (`Helper.swift`, `AgentTools.swift`) and replayed against the new service. **Layout**: `native/client/tests/architecture` and `tools/check-layout.py` extended to the new component. **Script**: `scripts/check-session-protocol.py`, standard library only like the other `check-*` scripts, as a protocol 0 client and a protocol 1 client. **Real engine**: a C++ workflow `native/client/tests/integration/session_workflows.cpp`, registered in the `tests/cef` manifests: two owners, an agent against a person, popups, engine death. |
| 2 | **Built**: `embed_test` in `native/client/tests/unit`, against `session_fake_engine`, which on macOS now sends real `420f` IOSurface frames at the viewport times the scale, then a forged frame from another process. It covers: runtime inspect (symlinked directory, an old service, missing, relative, a signing requirement, a wrong ABI), joining before start, owner isolation, `invalid_url`, navigation, history, take and release, screenshots, grants and revocation, `human_in_control`, `stale_generation`, the agent catalog, events (`resync`, `tab.changed`, timeout), an input-only view with every input kind and four threads of moves, library frames (size, format, use count, the forged frame dropped, resize, `view_busy`, `replace`, detach, an unsafe bootstrap name), host-only shutdown, idempotent close and `engine.exited`. `embed_header_test.c` compiles the header as C and checks the init functions and `sc_error_refusal`; `c_abi_test.c` is unchanged. Both pass on macOS and on Linux, where frames and signing requirements are `SC_UNSUPPORTED`. **Sample**: built, and run by hand against the real engine with `--click`, `--type` and `--snapshot`. **Packages**: `tests/consumer` compiles `embed.h` first and alone as C++, and `c_embed_consumer.c` includes only `embed.h` and calls it, in the C-only shared consumer too. `tools/verify-package.py` requires `client.h` and `embed.h` by name. `scripts/package-native-runtime.py` requires `embed.h` among its SDK inputs. `tools/package-source.py` now archives `native/client/session/`, which the source archive had lacked since phase 1. On Linux (the devbox, 2026-11-21), `verify-package.py --source-archive` passed from the exported archive. That covered static and shared builds, unit and architecture tests, relocated installs, and four consumers: static C++, shared C++, shared C-only and an embedding parent. `installed_embed_c_abi` passed in each. On macOS, the static install's consumer passed, with `embed.h` compiled alone. **Real engine, opt-in**: `suitecut-native-cef_embed_frames` (`ctest -L embed-frames` with `SUITECUT_EMBED_RUNTIME`) and `scripts/verify-embed-sample-macos.py`; see "Built in phase 2". |
| 3 | **Built**: `session_protocol_test`'s `host_events` section, against `session_fake_engine`, which now models host events and held dialogs with `--suitecut-host-events=1`. It covers: the capabilities in `hello`; `tab.loading` (start, progress, finish, history) and `tab.title`; a new tab's `tab.changed` before its loading events; `tabs.list` fields; `tabs.stop` taking the tab; IME set, commit and cancel, with ranges and which of them take the tab; the edit commands; `view.cursor` and `view.ime`; no prompt without a prompt host; a prompt host holding its owner's dialogs; `prompt.opened` fields; agent answers on its own tab, refusal on the person's; another owner's prompt; a second answer; a prompt's default text; a host navigation dismissing a held alert, and a `beforeunload` left to the engine's reset; a popup's `opener`, with its dialogs held; protocol 0's tab fields unchanged; and the last prompt host leaving. `embed_test` adds `SC_SESSION_PROMPTS`, an unknown flag, the loading and title kinds, `sc_tab_stop`, the IME and edit calls in order, bad ranges and commands, and a dialog answered through `sc_prompt_answer_dialog`. `embed_header_test.c` checks `sc_ime_composition_init` and the new constants. The engine's `suitecut-page-before-unload-test` checks the dialog reset report. **Real engine, opt-in**: `suitecut-native-cef_host_events` (`ctest -L embed-host-events` with `SUITECUT_EMBED_RUNTIME`), Linux and macOS, headless; see "Built in phase 3". **Item 5** adds `session_protocol_test`'s `person_prompts` section: no person prompts without a host connection, and an agent connection listing `prompts` does not count; each kind's `prompt.opened` fields; file answers with good and bad paths (`invalid_path` naming the path) for single, folder and save choosers, and cancel; a file chooser closed by a navigation; a permission refused, and a non-boolean answer; a certificate by index and an index not listed; auth answered by the agent's credentials on its tab with nobody asked, the person asked on the agent's tab without them and on the person's tab despite them, a login cancelled and one answered; a download saved to a chosen path and one cancelled, and none on an agent's tab; an agent's answer refused `forbidden`; a second answer `prompt_gone`; the last host leaving answering by policy; and the fake engine's log carrying no password. `embed_test` answers each kind through the new `sc_prompt_answer_*` calls and `sc_prompt_cancel`, and `embed_header_test.c` checks their arguments. The engine's `suitecut-person-prompts-test` checks the holder: hold, answer, a refused answer kept, `{policy:true}`, a dismissal, a reset that keeps what is not the document's, unsubscribing, and clearing. `cef_host_events` adds a file chooser answered, cancelled and closed by a navigation; geolocation refused and notifications granted, each asked again; HTTP basic auth answered, with no trace of the password; no download prompt on the agent's tab; a download saved to a chosen path from a host's tab; and `sc_tab_navigate` returning at commit on a stalled page. Its first-load check no longer expects `about:blank` behind the page. **Still to do**: client certificates on a real engine; a dialog left to time out (30 s); `beforeunload` with a user gesture; audio audible while not recording. |
| 4 | **Bundle**: `scripts/check-runtime-bundle-macos.py` checks that `otool -L` shows only system and in-bundle libraries, that every Mach-O has the same `minos`, hardened runtime flags, the manifest present and signed, and the protocol range. **Relocation**: `scripts/verify-native-runtime-macos.py` extended to run on a relocated copy with `/opt/homebrew` out of reach. **Gatekeeper**: `spctl` and `stapler validate` only after a notarization the owner approved. |
| 5 | **MCP**: a join-mode test with a fake session (in the style of `control_cli_test.cpp`): grant narrowing, `--simple`, `human_in_control` as a tool error, opening the first tab. **Fuzzing**: `scripts/fuzz-native-control.py` extended to `--join`. |
| 6 | **Linux spike**: half done on a machine without a GPU (2026-11-20, `suitecut-accelerated-paint-probe`): shared textures need GBM and give no frames without a render node; CPU frames work. **Still to do**: the same probe on a named GPU and driver, recording planes, formats and modifiers, then the frame workflow ported. |

### Open items for this design

- **Who may close a session.** A protocol 0 connection, which is any
  same-user process, can still send `_session_close`. Refusing it is not a
  security boundary: a same-user process can also signal the worker. It
  guards against mistakes. The choices are:
  - keep it, as now;
  - have the CLI's `close` speak protocol 1 as a host, and stop answering
    `_session_close` when `--protocol-0 off`;
  - stop answering it altogether once Napdock's helper no longer relies on
    the CLI's `close`.

  It stays as it is for now: Napdock's "Quit browser" sends it.
- **The Linux frame path** (see "How frames reach the app" in the
  [direction](direction.md)).
- Per-view scale in the engine.
- The engine's pid version in `hello`, so receivers can check it as well as
  the pid.
- Whether `embed.h` should also take a socket path directly. Today a long
  session directory is refused with the limit named (see "Built in phase 2").
- A platform field for `tests/cef` cases, so macOS-only workflows such as
  `cef_embed_frames` can join `manifests/cpp.json`.
- Whether `view.focus` is needed separately from input for caret and IME.
- Whether a shared per-machine runtime later needs a broker process above
  sessions. With per-app pinned runtimes it does not.
- **A navigation's commit without host events.** Protocol 0's
  `tabs.navigate` still waits for the load's end, and a page that never
  finishes loading fails it with `navigation_failed` after 30 s.
- **Client certificates on a real engine**, and remembering permissions per
  origin (see "Prompts that go to the person").
- **Popup disposition.** A popup's `tab.changed` carries `opener` but not
  whether it asked for a tab, a window or a popup, nor its size.
- **Custom cursors.** `view.cursor` says `custom` with the hotspot and
  size, but not the image.
- **A popup's first dialog** can come before the session subscribes the
  popup's dialogs, and then gets the engine's default answer.
- **Dialog deadline.** The engine's 30 s applies when a person answers too.
