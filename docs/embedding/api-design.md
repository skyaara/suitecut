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
  Windows". Whether 156 delivers dmabufs off screen is unknown.
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
  - `download` (`suggestedName`)
  - `auth` (`origin`, `realm`, `isProxy`)
- The matching answers:
  - `{accept, text?}`
  - `{grant:[...]}`
  - `{paths:[ABS]}` or `{cancel:true}`
  - `{path:ABS}` or `{cancel:true}`
  - `{username, password}` or `{cancel:true}`
- The first answer wins; later ones get `prompt_gone`. An unanswered prompt
  takes the engine's default when its deadline passes (dialogs: 30 s today,
  `dialog_controls.h:34`), and `prompt.closed` says `timeout`.
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
- Unknowns for the spike: whether CEF 156 delivers accelerated paint off
  screen on Linux at all; which formats and modifiers; whether a GPU process
  is available on the target machines; and how to fence (implicit sync or
  sync files).

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

`native/examples/embed-macos/` is a separate CMake project that is not
installed. It builds `suitecut-embed-sample`, a small AppKit app in about
600 lines of Objective-C++, using only `embed.h`:

- One window: a toolbar with back, forward, reload, an address field and a
  Take over / Hand back button, over a view whose `CALayer` shows the frames.
- It starts from `--runtime PATH` (`sc_runtime_inspect` with SuiteCut's
  team requirement), opens session `suitecut-sample` as owner `sample` with
  role `host`, opens one tab, and attaches an `SC_SINK_LIBRARY` view.
- A thread calls `sc_view_next_frame`, raises each surface's use count, and
  sets the layer's contents on the main thread.
- Mouse, scroll and keys go to `sc_view_pointer`, `sc_view_wheel` and
  `sc_view_key`. `NSTextInputClient` maps marked text to
  `sc_view_ime_set` and `sc_view_ime_commit`. Cmd+C/V/X/Z go to
  `sc_view_edit`.
- An event thread posts to the main queue:
  - a dialog becomes an `NSAlert`;
  - a file prompt becomes an `NSOpenPanel`;
  - a cursor event sets an `NSCursor`;
  - `tab.changed` updates the toolbar.
- A menu item mints a grant and prints the `suitecut-native-control --mcp --join …`
  command, so an agent can drive the same tab while the window watches.

What it proves:

- Nothing in the API is named after Napdock.
- A host can receive frames directly, with no relay process.
- Owners are the host's own strings.
- Every host event can be answered by a plain window.

It is also the place to run the phase 3 event tests by hand.

### What Napdock's helper would change

For the Napdock side to pick up; this design changes nothing there.

1. Look for `Contents/MacOS/suitecut-session` first, then
   `suitecut-shared-session`. Read `Contents/Resources/suitecut-runtime.json`
   and refuse a runtime whose protocol range excludes 1 (or fall back to
   protocol 0).
2. Send `hello`: role `host` from `browserhost serve`, `agent` from
   `browserhost mcp`. Declare `maxMessageBytes` (8 MB today).
3. Replace `watch` with `events.next`, and drop the duplicate check in
   `TabWatcher`.
4. Probe with `session.status` instead of `tabs.list` for the owner
   `browserhost`.
5. Keep the relay with `SC_SINK_MACH_BOOTSTRAP`, so the app's helper-pid
   check stays. Compare the trailer's pid version with `engine.pidVersion`.
6. Ask for screenshots with `to:{file}`. The helper already writes a PNG to
   its data folder (`tabs.screenshot`).
7. Remove the 1920×1080 viewport limit (`Helper.swift:70`). Agree one
   largest frame side between the helper (16384) and the app (8192), or
   none.
8. Send per-view `scale` from the panel's backing scale, not the session's
   fixed 2.
9. Use pointer coordinates in CSS pixels, and send modifiers with pointer
   and key events.
10. Forward IME (`input.ime.*`) and Cmd+C/V/X/Z (`input.edit`) from the
    panel.
11. Answer `prompt.opened` in the panel: dialogs, file pickers, permissions,
    downloads, auth. Show `tab.loading`, titles and the cursor.
12. Optionally replace `browserhost mcp` with
    `suitecut-native-control --mcp --join`, once the executor can pass a
    grant. Otherwise keep it and send `expectGeneration`.
13. After starting the runtime, check the running engine's signature by
    process (`SecCodeCopyGuestWithAttributes`), not only the files.
14. Update the decision record: drop "at most 16" tabs, and note protocol 1.
15. Handle `engine.exited` by restarting the session once.

### Tests per phase

| Phase | Tests |
| ----- | ----- |
| 0 | **Built**: `shared_session_test` against a new `tests/support/session_fake_engine.cpp`, with the Napdock envelope replay in the same file; the Linux build is still to do. **Planned**: `native/client/tests/unit/shared_session_test.cpp`, running the built tool against `tests/support/transport_fake_engine.cpp` or `browser_facade_engine.cpp` (both fake engines exist). Covers: framing (empty, partial, 2 s deadline, many requests per connection), `_session_status` and `_session_close`, owner isolation (`unknown_page`), `human_in_control`, take and release generations, popup inheritance, `engine_exited`. **Build**: a Linux configure and build of `native/client` to settle the `getpeereid` question. |
| 1 | **Built**: `session_protocol_test` in `native/client/tests/unit`; still to do: the golden fixtures as files, the script, and the real-engine workflow. **Planned**: **Protocol conformance** tests in `native/session/tests/unit` with a fake engine: `hello`, detecting protocol 0, every op and error code, roles and grants, `expectGeneration`, claiming before the call, the event cursor and `resync`, `reply_too_large`, per-owner agent concurrency. **Golden protocol 0 fixtures** (`native/session/tests/fixtures/protocol0/*.json`), recorded from the shapes Napdock sends (`Helper.swift`, `AgentTools.swift`) and replayed against the new service. **Layout**: `native/client/tests/architecture` and `tools/check-layout.py` extended to the new component. **Script**: `scripts/check-session-protocol.py`, standard library only like the other `check-*` scripts, as a protocol 0 client and a protocol 1 client. **Real engine**: a C++ workflow `native/client/tests/integration/session_workflows.cpp`, registered in the `tests/cef` manifests: two owners, an agent against a person, popups, engine death. |
| 2 | **C ABI**: `native/client/tests/unit/c_embed_abi_test.c` (`struct_size` and `abi_version` checks, null handles, `SC_REFUSED` and `sc_error_refusal`), with `c_abi_test.c` unchanged as the proof that v1 callers keep working. **Packages**: `tests/consumer` and `tools/verify-package.py` include `embed.h` in isolation. **Frames on macOS**: `cef_surface_workflows.cpp` attaches a library sink, then checks `420f`, even sides, timestamps, the pid and pid-version check, and that a held surface is never redrawn (in the style of `suitecut-mac-capture-test`). **Sample**: it builds, and `scripts/verify-embed-sample-macos.py` drives it once and compares a screenshot. |
| 3 | One real-engine test per host event, in the `tests/cef` C++ workflows: `alert`, `confirm` and `prompt` answered and timed out; `beforeunload`; a file chooser answered and cancelled; a permission granted and denied; a download to a chosen path; HTTP basic auth (the fixture server under `tests/cef/fixtures`); cursor changes; IME composition and commit; copy and paste; title and loading events; audio audible while not recording. |
| 4 | **Bundle**: `scripts/check-runtime-bundle-macos.py` checks that `otool -L` shows only system and in-bundle libraries, that every Mach-O has the same `minos`, hardened runtime flags, the manifest present and signed, and the protocol range. **Relocation**: `scripts/verify-native-runtime-macos.py` extended to run on a relocated copy with `/opt/homebrew` out of reach. **Gatekeeper**: `spctl` and `stapler validate` only after a notarization the owner approved. |
| 5 | **MCP**: a join-mode test with a fake session (in the style of `control_cli_test.cpp`): grant narrowing, `--simple`, `human_in_control` as a tool error, opening the first tab. **Fuzzing**: `scripts/fuzz-native-control.py` extended to `--join`. |
| 6 | **Linux spike**: a recorded check that accelerated paint delivers dmabufs off screen on a named GPU and driver, then the frame workflow ported. |

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
- **The Linux frame path** (see "How frames reach the app" in the
  [direction](direction.md)).
- Per-view scale in the engine.
- Whether `view.focus` is needed separately from input for caret and IME.
- Whether a shared per-machine runtime later needs a broker process above
  sessions. With per-app pinned runtimes it does not.
