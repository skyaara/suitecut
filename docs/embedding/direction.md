# SuiteCut as an embeddable browser runtime

Status: **proposed**, 2026-11-20. Nothing here is built yet beyond what
"Today" says. The protocol and C API are drafted in the
[embedding API design](api-design.md).

## What SuiteCut becomes

SuiteCut becomes a Chromium browser runtime that other apps install and run
beside themselves. An app shows SuiteCut's tabs in its own window and passes
the user's input to them. An AI agent can drive the same tabs through
SuiteCut's own observe, act and wait tools, and the person can take a tab
back at any moment. A program that only wants browser control uses the same
runtime with no window at all. The app links a small C library. The browser
itself runs in its own process, so the app never links Chromium.

It has three kinds of users:

- **An app embedding a browser view.** It shows a tab, forwards pointer, key
  and text input, and answers what the page asks for: dialogs, file pickers,
  downloads, permissions, new windows. Napdock, a native macOS app for coding
  agents, is the first such app. It should not be the only one the design
  fits.
- **An AI agent driving the browser.** It uses the runtime's MCP server or
  its tool calls, scoped to the tabs of one owner (a chat, a project). It is
  refused while the person holds a tab.
- **A program doing plain browser control.** Tests, scrapers and scripts use
  the existing C ABI and C++ API (page, locator and context commands), with
  or without a shared session.

## How it runs

The runtime runs as its own process: one engine per session, serving any
number of apps and agents over a private local socket. The app links a thin
C library that finds the runtime, checks its signature and version, starts or
joins a session, and receives frames as GPU surfaces. This is close to how
Microsoft's WebView2 works, and unlike linking CEF into every app.

**Recommendation (not yet confirmed): separate process only, with no
in-process library.** The reasons:

- Chromium is about 350 MB as a macOS bundle (Napdock's decision record)
  and gets security fixes every few weeks. Linked into each app, every app ships and patches its own copy,
  and a crash in a page or in CEF takes the app down with it.
- CEF owns its process: its message loop, its sandbox and helper
  executables, and on macOS its bundle layout and signing. An app that links
  it has to adopt all of that.
- One engine can serve the person's view and the agent's tool calls
  together. An agent process cannot reach into another app's address space,
  but it can reach a socket.
- Frames already cross processes with no pixel copy on macOS. The engine
  paints into an IOSurface and only a port right moves
  ([surface_output.mm](../../native/browser/surface_output.mm)).

The alternatives considered:

| Alternative                                                | Why it lost                                                                                                                                                                                                                                         |
| ---------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Link CEF into the host app                                 | Each app carries and patches Chromium, adopts CEF's process and bundle rules, and crashes with its pages. An agent in another process cannot share its tabs.                                                                                          |
| An in-process SuiteCut library (CEF inside, our API on top) | Has every cost of linking CEF, plus a second API to keep stable. It could come later for apps that insist on it. It is not needed for the three users above.                                                                                          |
| Plain Chromium with CDP and an outside automation client   | No zero-copy view for the app: frames become screenshots or a screencast. A remote debugging port is an open door on the machine. Each tool (Playwright, an MCP server) is a second client with its own idea of tabs, so a person and an agent cannot share control cleanly. SuiteCut's engine runs CDP in-process instead and exposes no port ([shared-browser.md](../shared-browser.md)). |

## The four pieces

### 1. The runtime

The CEF engine (`suitecut-browser`) plus its helpers. It is built per
platform, versioned and signed.

- **Today.** macOS arm64 and Linux x86_64 engines exist. Windows runtime
  packaging is not implemented ([native-full-runtime.md](../native-full-runtime.md)
  says Linux is the qualified runtime and Windows/macOS are separate work;
  the macOS ARM64 recipe is in [native-full-runtime-macos.md](../native-full-runtime-macos.md)).
  The bundle Napdock runs is built by
  [package-napdock-browser-macos.py](../../scripts/package-napdock-browser-macos.py).
- **Missing.** A runtime anyone can redistribute: dependencies bundled
  inside, a minimum OS that matches what it claims, hardened runtime and
  notarization, and a version and protocol number a host can read. See
  "Distribution" below.

### 2. The embedding library

The C ABI (`native/client/include/suitecut/client.h`) and the C++ API on top.

- **Today.** ABI version 1 has 23 functions. They launch the CEF engine or
  installed Chrome or Firefox, or attach to them, then submit JSON commands
  and poll packets ([client.h](../../native/client/include/suitecut/client.h)).
  There is nothing for sessions, owners, views, frames, input or host events.
- **Missing.** Finding and checking a runtime, joining a session as an
  owner, tabs, views with a platform surface sink, input including IME
  composition, host events the app answers, and taking and releasing
  control. The design adds these in a new header, so ABI v1 callers keep
  working.

### 3. The session service

One process per session. It owns the engine, keeps each owner's tabs, says
who controls each tab, and serves the private socket.

- **Today.** It is `suitecut-shared-session`, built from
  [native/client/examples/shared_session.cpp](../../native/client/examples/shared_session.cpp)
  with the socket code in
  [control_session.cpp](../../native/client/examples/control_session.cpp).
  It has owners and per-tab control (`agent`, `human`, `unclaimed`, with a
  generation counter). It offers tab operations, view operations (attach to
  a Mach port, detach, screenshot, resize, input) and agent tool calls, plus
  a long-poll `watch` for tab changes.
- **Missing.**
  - It lives under `examples/`, has no tests at all, and its protocol has no
    version or handshake.
  - Any process of the same user can act as any owner, or close the whole
    session.
  - Agent calls from every owner run one at a time
    ([shared_session.cpp:169](../../native/client/examples/shared_session.cpp)),
    so one agent's long wait holds up every other app's agent.
  - It passes on none of the engine's dialogs, downloads, permission
    requests, loading state or title changes; only navigations, new popups
    and closed pages ([shared_session.cpp:518-567](../../native/client/examples/shared_session.cpp)).

### 4. AI and control through one channel

The same session serves the agent's tools and plain control.

- **Today.** `suitecut-native-control --mcp` is a complete MCP server over
  SuiteCut's agent tools, with `--simple` (observe, act, wait only) and
  `--tools` groups ([control.cpp](../../native/client/examples/control.cpp)).
  However, it always launches its own browser and cannot join a shared
  session or act as an owner (control.cpp:179-180 refuse `--session` with
  `--mcp`, and its worker launches a browser at control.cpp:206). So Napdock
  wrote its own nine-tool MCP server in Swift over the session socket.
- **Missing.** The MCP server joins a session as one owner, with the tool
  groups the host allows, and reports `human_in_control` the way Napdock's
  does.

## What an embedding app needs that the engine does not give yet

The engine is an automation engine first. Several things a person in a
window expects are handled by policy or not reported at all. Evidence is in
the [design document's gap table](api-design.md#host-events-today).

| Need                                  | Today                                                                                                     |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| Page view as GPU frames               | macOS only                                                                                                |
| Popups and `<select>` menus           | Drawn into the view on macOS. Whether clicks reach an open menu through the view has not been checked.    |
| New windows (`target=_blank`, `window.open`) | Become tabs of the opener's owner                                                                  |
| Mouse, wheel, keys, typed text        | Yes                                                                                                       |
| IME composition (Chinese, Japanese, Korean input) | Missing: typed text only                                                                      |
| Copy, cut, paste, undo shortcuts      | Missing on macOS: only Cmd+A is mapped to an edit command                                                 |
| Cursor shape                          | Missing                                                                                                   |
| JavaScript dialogs                    | Suppressed with the default answer unless a client subscribes. The agent's tools can list and answer them; the app never hears of them. |
| File chooser and uploads              | The agent can upload. The person gets no file picker.                                                     |
| Downloads                             | Handled by engine policy; not reported to the app                                                         |
| Permission prompts (camera, location, notifications) | Answered by a fixed policy; never asked                                                    |
| HTTP authentication, client certificates | From preset credentials only                                                                           |
| Loading state, title, favicon         | Navigations only. Titles are read on demand; no loading state or favicon.                                 |
| Audio                                 | Captured for recording, which mutes the page; the session discards it, so the person hears nothing       |
| Drag and drop with the desktop        | Missing                                                                                                   |
| Accessibility for screen readers      | Missing (the agent's accessibility snapshot is not a platform accessibility tree)                         |

These gaps are the largest part of the work to make SuiteCut a general
embedded browser. Most need engine changes, not only protocol changes.

## How frames reach the app

Each platform has its own GPU surface, and the app receives that surface
with no pixel copy.

- **macOS (built).** The engine converts each painted frame on the GPU to
  NV12 and sends it as an IOSurface in a 64-byte Mach message, to a port the
  receiver registered. A slow receiver misses frames and never sees a torn
  one.
- **Linux (unknown).** CEF 156 describes an accelerated paint on Linux as up
  to four dmabuf file descriptors. Yet the same header still says shared
  textures for windowless rendering are "currently only supported on
  Windows". SuiteCut never turns them on outside macOS, so its Linux engine
  paints into CPU memory today. Whether Linux GPU frames work in this CEF
  version, on which drivers, and in which formats needs a spike before any
  promise. The proposal is to pass dmabuf descriptors over a Unix socket.
- **Windows (unknown, blocked).** CEF hands over a Direct3D 11 shared texture
  handle that is valid only during the paint callback. There is no Windows
  SuiteCut engine yet, so this waits for an engine port. The proposal is
  that the engine copies into its own texture and duplicates an NT handle
  into the app's process.

On every platform the engine must copy CEF's frame into a surface it owns,
because CEF reclaims its buffer when the paint callback returns. The macOS
code already does this.

## Distribution

- **Versions.** A runtime carries three numbers: its own release version,
  the Chromium and CEF version inside it, and the session protocol versions
  it speaks. Today nothing carries a protocol version. The packagers stamp
  `0.1.0` and the CEF provenance into a manifest
  ([package-native-runtime-macos.py](../../scripts/package-native-runtime-macos.py),
  [package-native-runtime.py](../../scripts/package-native-runtime.py)), and
  the Napdock bundle has no manifest at all.
- **A check before use.** A host should be able to read the protocol range
  from a file inside the signed bundle without running anything. It should
  then confirm it in the session handshake.
- **Signing and notarization on macOS.** Today's bundle is Developer ID
  signed and timestamped, but has no hardened runtime and is not notarized.
  Gatekeeper rejects it as "Unnotarized Developer ID" (checked on the
  installed copy with `spctl`). The signing helper never passes
  `--options runtime`
  ([native_macos_signing.py:38](../../scripts/native_macos_signing.py)).
  Notarization needs hardened runtime. The entitlements CEF's helper
  processes need under it have not been worked out or tested here. Submitting to Apple is an outward step that needs the
  owner's explicit approval each time.
- **Dependencies inside the bundle.** The engine in Napdock's bundle loads
  curl, OpenSSL 4, libpsl and brotli from `/opt/homebrew` by absolute path.
  So it runs only on a Mac that happens to have the same Homebrew formulas.
  The general runtime packager already copies such libraries into the
  bundle and rewrites their paths. The Napdock packager skips that step for
  the engine. It does check the session tool.
- **Minimum OS.** The engine says macOS 13; the session tool in the same
  bundle says macOS 27, because the client build sets no deployment target.
  A runtime must declare one minimum and build every binary for it, and
  bundled Homebrew libraries can raise it
  ([native-full-runtime-macos.md](../native-full-runtime-macos.md)). Linux
  states glibc 2.38+.
- **Codecs.** The macOS pin is a third-party CEF build with proprietary
  codecs (H.264, AAC, HEVC). It is downloaded from a personal GitHub release
  and carries no codec patent licence
  ([cef-build.json](../../native/browser/cef-build.json)). Linux uses the
  official build without them. A runtime other people ship needs an explicit
  decision here (see open questions). Its security updates also depend on
  that third party rebuilding each CEF release.
- **Media tools.** Every engine recording needs an FFmpeg executable at an
  absolute path. Napdock's bundle has none, so the `recording` tool the
  shared session offers its agents cannot work there.
- **Pinned per app or shared.** **Recommendation (not yet confirmed): each
  app pins and installs its own runtime version first; a shared per-machine
  runtime can come later.** A pinned runtime lets an app ship knowing exactly
  which protocol and Chromium it gets, and nobody else's update can break
  it. The costs: disk (about 350 MB per app), one engine per app instead of
  one per Mac, and each app must ship Chromium security updates itself. A
  shared runtime needs a stable protocol with real compatibility rules, an
  updater, and a policy for which app may start or stop the shared engine.
  The handshake designed here is what makes a shared runtime possible later.

## Trust between the app and the runtime

**Is this runtime genuine?** The host checks the code signature against a
pinned team and requirement before running it. Napdock does this today for
both programs in the bundle. Two things make it stronger:

- Checking the running process as well as the files on disk, so a file
  swapped between the check and the launch is caught.
- Notarization with hardened runtime, so the operating system enforces
  library validation.

On Linux the equivalent is the package manager's signature or a pinned
digest. On Windows it is Authenticode.

**May this client connect, and as whom?** Today the socket is 0600 in a 0700
folder and the peer's user id is checked. Every process of that user is
fully trusted: it may act as any owner and close everyone's tabs. A process
of the same user can also read the browser profile directly, so a socket
check cannot be a real security boundary against malware running as the
user. What it can stop is mistakes and an agent that was talked into
overreaching. The design adds grants: the app that starts a session mints a
token for an owner and a set of tool groups. An agent's MCP server presents
that token, and the session holds it to that owner and those tools.

**Is this frame from the engine?** The receiver checks the kernel's audit
trailer on every Mach message and accepts only the engine's process id.
Napdock does this in its helper and in its app. The design moves the check
into the C library and compares the process's version number as well, so a
reused process id cannot pass.

## Plan

Each phase ends with its tests passing and its documents updated.

| Phase | Work | Done when |
| ----- | ---- | --------- |
| 0. Land what exists | Merge `claude/napdock-156` into `main` after review. Add the first tests for the session tool. Confirm whether the session code builds on Linux. | The branch is on `main`, and the session's framing, ownership and control rules have unit tests against a fake engine. |
| 1. Session protocol v1 and product target | Move the session out of `examples/` as `suitecut-session`, add the versioned handshake, request ids, typed errors, grants and event cursor. Keep the current unversioned protocol working. | Today's installed Napdock helper works unchanged against the new service, and a v1 client detects an old service. |
| 2. Embedding library and a second host | The `suitecut/embed.h` C API (runtime check, session, tabs, views, input, events, control) and a minimal macOS sample app. | The sample app shows and drives a tab using only the public API, and Napdock's helper has a written path onto it. |
| 3. Host events | Engine and session work for dialogs, file choosers, downloads, permission prompts, HTTP auth, loading state, titles, cursor, IME, edit commands and audio playback. | Each event has an engine test and appears in the sample app. |
| 4. A generic runtime | Bundle dependencies, one minimum macOS, hardened runtime and notarization readiness, a runtime manifest with protocol versions, and a decision on codecs and FFmpeg. | A relocated bundle passes the verifier on a Mac without Homebrew, and the manifest is read by the C library. |
| 5. MCP in a session | `suitecut-native-control --mcp` joins a session as an owner with a grant. | An agent driven through it is refused on a person's tab, and Napdock could drop its Swift MCP server. |
| 6. Linux embedding | GPU frames as dmabuf (after a spike), the session on glibc, and a sample host. | A Linux sample host shows a tab with no CPU copy, or the spike documents why it cannot. |
| 7. Windows | An engine port, then Direct3D shared textures. | Scoped separately. |

The recommended first piece of work is phase 0 and the start of phase 1.
Every later phase depends on a contract that has tests, and the session has
none today.

## Open questions

1. **Process model.** Confirm separate process only (recommended), or keep
   an in-process library on the table.
2. **Pinned or shared runtime.** Confirm per-app pinned installs first
   (recommended).
3. **Codecs.** Ship the third-party codec build, ship the official build
   without H.264/AAC, build our own codec CEF (it needs a much larger build
   host), or offer both. This also decides who rebuilds for each Chromium
   security release on macOS.
4. **FFmpeg.** Bundle it in the runtime (licence review needed), make
   recording optional, or require the host to supply it.
5. **Names.** The service's name (proposed `suitecut-session`) and the
   runtime bundle's name and identifier.
6. **Who answers prompts.** When an agent holds a tab and the page opens a
   dialog or asks for a permission, may the agent answer, or must the
   person?
7. **Per-agent control.** Today all agents of an owner count as one
   "agent". Should two agents in one owner hold separate tabs?
8. **Pushing `claude/napdock-156`.** It exists only on this Mac.
