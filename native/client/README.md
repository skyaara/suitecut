# Native browser control SDK

Standalone C++20 library and versioned C ABI for browser control. Applications
can launch the SuiteCut native engine over its binary protocol, installed
Chrome/Chromium over a private CDP pipe, or Firefox through WebDriver BiDi.
No Node.js process, npm package, CEF
headers, or browser automation dependency is needed to build or run the client.

See [measured validation and performance](VALIDATION.md) for tested scope,
latency, memory, sanitizer results and limitations.

The native client targets Linux/POSIX. Browser binaries are distributed
separately from the client. Chromium and Firefox expose launch and attach APIs;
their supported command families differ and fail explicitly when unavailable.
The supported backends are SuiteCut CEF, external Chrome/Chromium and Firefox.
Safari/WebKit is not a supported backend.

## Simple agent tools

`suitecut/simple_agent.hpp` provides `SimpleAgent`, a synchronous C++ wrapper
over an `AgentDriver`. Its three tools are `observe`, `act`, and `wait`:

```cpp
#include <suitecut/simple_agent.hpp>

suitecut::AgentDriver driver(browser);
suitecut::SimpleAgent agent(driver);
auto observation = agent.call("observe", {{"boxes", true}});
// Choose a ref from this observation's snapshot or controls.
auto result = agent.call("act", {
    {"action", "fill"}, {"ref", "e1"}, {"value", "Hello"}});
auto ready = agent.call("wait", {{"text", "Saved"}, {"timeoutMs", 2000}});
```

`act` returns a fresh observation, including after an ordinary action error when
time permits. Each semantic observation replaces the short refs; old refs are
rejected. `act.actions` executes up to 64 already known UI actions in order,
stops on failure, and observes once. `wait` accepts rendered text, a URL, a ref
state, or a change from the previous observation. Calls share one deadline,
defaulting to five seconds and capped at 30 seconds; mutations are never retried.

`delivered` records browser acknowledgement (`null` means an uncertain outcome).
`effect` confirms only an optional visible UI condition; neither field proves
that an application task succeeded. Screenshot, frame, and dialog inspection
are modes of `observe`. The installed control executable's `--simple` mode
publishes only these three tools and rejects raw evaluation and driver calls.

`observe({"format":"compact"})` returns bounded interactive controls with
their supported actions and separate text context. Inert layout nodes are not
advertised as controls. `scope` selects a CSS subtree, `query` filters literal
case-insensitive text, and `maxControls`/`maxTextBytes` bound the control output
(defaults: 100 controls and 8192 text bytes; context has a separate 4096-byte
budget). Truncation is explicit. Compact input controls omit current values;
password values are also redacted by the underlying observation projection.

`observe({"format":"compact","delta":true})` compares semantic UI content
with the previous compact observation of the same page, scope and budget. A
compatible delta returns `changes` and a current `refIndex`; the first call or
a scope change returns `reset:true` with controls. Every snapshot still retires
old refs. A similar-looking node is never assigned an old ref. Diffs flag
ambiguity and suppress removals when their input was incomplete.

`act` and `wait` accept `response:full|compact|delta|none`. The C++ API keeps its
existing full default. `none` avoids the post-action snapshot and its browser
round trips; refs remain usable for a known sequence until a later snapshot or
navigation. Inspect the UI before treating an acknowledgement as success. A
coordinate drag accepts `from:{x,y}`, `to:{x,y}`, and `steps` (default 20, range
1..1000); mouse `event:doubleclick` is also supported.

## Native command-line sessions

The installed executable also supports one command per invocation. A named
session keeps a native worker and browser alive between invocations:

```sh
suitecut-native-control --session shopping --browser /usr/bin/chromium open https://example.com
suitecut-native-control --session shopping snapshot
# Choose the current Name control's ref from the returned controls.
suitecut-native-control --session shopping --response none fill e12 'Ada'
# With response none, the other refs from that observation remain available.
suitecut-native-control --session shopping click e15
suitecut-native-control --session shopping snapshot --scope '#checkout' --delta
suitecut-native-control --session shopping screenshot --output /tmp/checkout.png
suitecut-native-control --session shopping close
```

Use `help`, `help debug`, `tools`, or `profiles` for discovery without launching
a browser. Commands include ref/coordinate input, interpolated drag, conditional
waits, batches, tabs, viewport changes, frame inspection and text search. `call
TOOL JSON_ARGUMENTS` exposes an explicitly selected advanced native tool. Use
`--` before arguments that begin with `--`; text is passed literally without a
shell. `--output FILE` saves JSON or screenshots in an exclusively created
mode-0600 file and returns its path and size. A screenshot without `--output`
gets a unique temporary PNG path. Files remain caller-owned after session close.

Only `open` starts an absent session. `status` reports the native worker PID;
`close` retires the owned browser before acknowledging shutdown. The idle timeout
defaults to ten minutes and is configurable at startup with `--idle-ms`.
Concurrent opens serialize startup, and each worker serializes browser commands
without creating a thread per request. IPC uses a private Unix socket, bounded
messages, peer checks on Linux, and deadlines; dispatched input is never
automatically replayed on timeout or disconnect. The default session directory
is `$XDG_RUNTIME_DIR/suitecut-control` or `/tmp/suitecut-control-UID`; override it
with a short absolute `--session-dir` owned by the user with mode 0700. Small
startup lock files remain after close to prevent lock-file replacement races.
Named sessions and exclusive CLI artifacts currently require POSIX.

## Capability groups for CLI and MCP

Named CLI commands and MCP default to `core`: the three agent tools, `profiles`,
and eight native navigation/read/tab/frame/viewport/get/upload/history tools.
`--tools network,state` adds both groups; `debug` and `media` are also available.
Every group includes core. `--tools all` exposes the complete 53-tool native
driver plus the three agent tools and profile discovery. Hidden tools are
rejected. This is a way to control the advertised tool surface, not a security
sandbox: capabilities such as navigation, uploads, or browser debugging still
need a trusted host. Named CLI invocations select groups per request; MCP keeps
the selected catalogue for the lifetime of the connection.

```sh
suitecut-native-control --session shopping --tools debug call evaluate '{"expression":"document.title"}'
suitecut-native-control --browser /usr/bin/chromium --mcp --tools core,network
```

The public C++ catalogue helpers live in `suitecut/agent_tools.hpp`. CLI and MCP
share these schemas and the same native implementation. The optional installed
[agent skill](skills/browser-control/SKILL.md) describes the CLI workflow; no
model, inference service, JavaScript runtime, or task planner is embedded in the
engine. Existing JSONL without `--tools` retains its original 53-tool catalogue,
and `--simple` retains exactly the original three-tool interface.

## Build and install

```sh
cmake -S native/client -B build/native-client -DCMAKE_BUILD_TYPE=Release
cmake --build build/native-client -j 2
ctest --test-dir build/native-client --output-on-failure
cmake --install build/native-client --prefix "$PWD/build/native-client-install"
```

Use `-DBUILD_SHARED_LIBS=ON` for a shared library. A downstream CMake application
can use `find_package(SuiteCutClient CONFIG REQUIRED)` and link `SuiteCut::Client`.
Headers are installed under `include/suitecut`, and the C ABI is declared in
`suitecut/client.h`. C programs may need to select the C++ linker when linking
the static library manually. For a static CMake consumer, enable both `C` and
`CXX` in `project(... LANGUAGES C CXX)`; the imported target selects the linker.
The shared C ABI also works in a C-only project.

Build dependencies include a C++20 compiler, threads, OpenSSL and ZLIB. Chromium recording
defaults to `/usr/bin/ffmpeg` and `/usr/bin/ffprobe` at runtime; supply absolute
`ffmpegPath` and `ffprobePath` recording options to use another installation. Ordinary control,
PNG decoding/comparison, TLS and the credential vault do not launch those tools.

The optional full-runtime archive adds the SuiteCut engine, CEF resources and
FFmpeg/ffprobe alongside this SDK. `suitecut-native-record` discovers those
bundled executables relative to itself and records without Node.js; use `--help`
for CEF page-audio and external Chromium options. The separate SDK-only archives
still do not contain browsers or media binaries. See `docs/native-full-runtime.md`
in the repository for the full-runtime recipe and qualification limits.

The engine path must be absolute. Keep its runtime libraries and resource files
together. Temporary profiles are created and removed by the client; an explicit
profile remains owned by the caller. Sandboxing stays enabled unless the caller
explicitly supplies a browser switch disabling it.

## Installed Chrome/Chromium

The same C++ browser, context, page and locator facade can launch an unmodified
installed Chrome/Chromium. This backend does not load or require SuiteCut's CEF
runtime or its resources:

```cpp
#include <suitecut/browser.hpp>

suitecut::LaunchOptions options;
options.executable = "/usr/bin/chromium"; // Absolute path to the installed browser.
options.headless = true;
auto browser = suitecut::Browser::launch_chromium(options);
auto page = browser.main_page();
page.navigate("https://example.com");
auto title = page.evaluate("Promise.resolve(document.title)").as_string();
auto screenshot = page.screenshot();
browser.close();
```

Alternatively set `options.backend = suitecut::BrowserBackend::chromium` and use
`Browser::launch(options)`. The default backend remains `BrowserBackend::cef`.
`Browser::ready()` identifies the selected backend, and `Browser::version()`
returns its browser version. `LaunchOptions::arguments` forwards explicit
browser switches. Use a separate profile; the SDK owns and removes a temporary
profile when `profile` is empty.

C callers use `sc_client_launch_chromium(&options, &client, &error)` after
`sc_launch_options_init(&options)` and setting `options.executable`. It uses the
existing `sc_launch_options` layout and ABI version 1. `sc_client_launch` keeps
its CEF launch behavior.

The installed example prints browser version, title and a semantic observation:

```sh
build/native-client-install/bin/suitecut-native-chromium /usr/bin/chromium \
  https://example.com
```

Launch mode owns its process and uses `--remote-debugging-pipe`; it opens no
debugging TCP port. Attach mode uses the SDK's native HTTP/WebSocket
client and borrows a browser the caller started with remote debugging enabled:

```cpp
auto attached = suitecut::Browser::connect_chromium("9222");
// Also accepts ws://127.0.0.1:9222/devtools/browser/ID.
auto page = attached.main_page();
auto title = page.title();
attached.close(); // Detach; the caller's browser stays open.
```

Numeric ports, HTTP(S) discovery and WS(S) browser URLs are supported, including
remote hosts. No Node or JavaScript runtime is needed by either connection mode.
Attached Chromium browsers retain their caller-owned
default context; create an explicit `new_context()` for SDK-owned tabs. Creating
tabs in the caller's default context is refused. Release held keys/buttons
before detaching; automatic restoration of held inputs is not guaranteed.
Core
controls include navigation, Promise evaluation, open-shadow-root CSS and
semantic locators, form controls, trusted CDP pointer/keyboard input, semantic
observations with document-bound references, PNG/JPEG/WebP screenshots,
viewport changes, isolated contexts/pages, cookies, and raw `Page::devtools`.
Independent asynchronous requests use the existing bounded transport API.
Ordinary commands execute in order. A narrow continuation queue allows
`Page.handleJavaScriptDialog`, `Fetch.*`, `Debugger.*`,
`Runtime.runIfWaitingForDebugger` and `Runtime.terminateExecution` DevTools calls
to unblock an active evaluation or navigation. Applications poll raw CDP events
and send those continuations themselves. Chromium uses four fixed control
threads: pipe/socket I/O, ordinary commands, continuations and deadlines. One
media worker is created lazily on first recording/tracing/profiling use and
blocks while idle.
The two command queues share bounded pending-request and queued-byte budgets;
requests do not create threads.

Queue deadlines and explicit cancellation settle their futures independently.
Cancellation cannot undo work already dispatched, and a dispatched slow Promise
can delay later ordinary commands. Input is never retried after a request is
retired. Closing the browser interrupts pending input delays so shutdown does
not wait for their full duration.

The generic command API has a backend-specific scope. CEF-only commands and
unimplemented features return explicit errors. The coverage table below records
the native Chromium surfaces and remaining limits. Raw CDP access remains
available for applications implementing additional Chromium features.

The standalone integration executable accepts a browser path and writes a JSON
report plus a viewport PNG. Repeated `--browser-arg` options forward arguments:

```sh
build/native-client/suitecut-native-chromium-real-browser \
  --browser /usr/bin/chromium --output /tmp/chromium-proof/results.json
```

It uses a self-contained data URL and needs no server or network connection.
It is separate from the default tests because an external browser is required.
See [validation scope](VALIDATION.md) for the evidence and limitations.

## Firefox and authenticated remote connections

The public Firefox backend uses native WebDriver BiDi and the same core page,
locator and agent interfaces:

```cpp
suitecut::LaunchOptions options;
options.executable = "/usr/bin/firefox";
auto browser = suitecut::Browser::launch_firefox(options);
auto page = browser.main_page();
page.navigate("https://example.com");
auto title = page.title();
// Page::bidi(method, params) exposes the actual BiDi protocol.
```

`Browser::connect_firefox(endpoint, options)` attaches to a caller-provided BiDi
WebSocket endpoint. Native input targets each real frame with local BiDi
coordinates, including cross-origin frames. Firefox downloads use native BiDi
download policy and lifecycle events. Geolocation, timezone, locale and the
user-agent string use native BiDi emulation. Media/CSS preferences and active
download cancellation use Gecko's native browsing-context and Downloads APIs.
The SDK does not replace `matchMedia` or `navigator` JavaScript APIs.

Owned Firefox launches enable `-remote-allow-system-access` for these privileged
automation operations. An attached caller browser must already have that
explicit Firefox permission to use media emulation or download cancellation.
The SDK never restarts it or changes its profile to acquire permission; missing
permission returns an actionable error. Media overrides retain their original
values for restoration before disconnect. Ordinary BiDi operations remain
available without privileged access. BiDi context UUIDs belong to a session;
reconnecting can assign new UUIDs to the same surviving caller tab.

Firefox's native HAR collector is separate from Chromium's CDP implementation.
Firefox does not implement Chromium CDP, WebMCP, browser tracing or Chromium
capture helpers. Unsupported screenshot/PDF options, user-agent platform or
Accept-Language overrides, and other backend differences return explicit errors.
The Chromium capability table does not establish Firefox parity. The new
download/emulation/HAR workflow passed 75 checks across nine scenarios on each
of CEF, Chromium and Firefox, in both Release and ASan/UBSan builds. Cross-frame
drag limits are recorded separately in
[VALIDATION.md](VALIDATION.md).

Firefox158 supports same-context pointer dragging. Cross-context dragging and
draggable HTML5 drag-and-drop are rejected before input: the tested stock
runtime pins held frame-origin input to the child, its headless drag service is
unavailable, and the native headless mouse route does not supply held-button
state. The SDK does not fabricate DOM drag events to conceal those limits.

## Shared downloads, emulation and HAR

The same C++ page methods, C ABI raw commands and native agent families address
CEF, external Chromium and Firefox. The following contract is shared; browser
capabilities and runtime proof remain specific to each backend.

| Family | Shared surface | Behavior |
| --- | --- | --- |
| Downloads | `set_download_directory`, `downloads`, `download_status`, `cancel_download`, `download_path`, `save_download` | Native page-owned download IDs; `inProgress`, `completed` or `canceled` state; actual downloaded bytes |
| Media | `emulate_media` / agent `emulation` | Media type, color scheme, reduced motion and forced colors; contrast where supported; omitted fields retain their values, and JSON `null` clears the named override |
| Other emulation | `emulate_geolocation`, `clear_geolocation`, `emulate_timezone`, `emulate_user_agent` | Actual browser engine overrides; unsupported extra fields fail explicitly |
| HAR | `start_har`, `stop_har`, `export_har` / agent `network` | HAR 1.2 output; `start_har({{"content","embed"}})` captures bounded bodies, while `"omit"` excludes response body text and retains available POST data |

Download directory policy applies to a browser context; individual records and
ID-based operations remain scoped to the owning page. Local paths and save
destinations must be absolute. Firefox checks completed sources against the
canonical directory captured when that download began. Remote Firefox download
metadata is available, but its browser-host files cannot be read or copied from
the SDK host. Download completion and cancellation are established by native
events. Inspect the actual result after a deadline or cancellation because a
dispatched operation may already have changed browser state.

Firefox 158 has no per-context contrast override, so `contrast` returns an
explicit capability error. Its native print medium uses light color matching;
print and screen/dark combinations follow the engine's real CSS behavior.

HAR collection is bounded. CEF output is filtered to the capturing page and
admits one active capture per context. Unsupported options and unavailable native body data
must remain explicit rather than appearing as captured empty content. These
APIs do not implement a complete archive/replay system. New three-backend live
validation is recorded under `.suitecut/cross-browser-complete/`. The available
shared-feature receipts and verified drag limits are listed in
[VALIDATION.md](VALIDATION.md).

`Context::grant_permissions` and `clear_permissions` scope grants to a browser
context and origin. Supply an explicit HTTP(S) origin before creating pages;
Firefox can infer it only when existing context pages share one nonopaque
origin. Unsupported permission descriptors fail explicitly. Firefox snapshots
the exact native permission entry and expiry before granting, then restores it
on clear/close because the native permission store is persistent. That snapshot
requires the same explicit privileged permission on an attached Firefox.

For authenticated CDP/BiDi connections, `LaunchOptions::connect_headers` supplies
HTTP headers for discovery and WebSocket upgrade. `tls_ca_file` adds a PEM trust
source; `tls_client_certificate` and `tls_client_key` provide an optional PEM
client-certificate pair. TLS verifies both the peer chain and hostname/IP;
there is no insecure-verification switch. Headers cannot replace managed
WebSocket handshake fields. These are connection options, distinct from page
HTTP request headers:

```cpp
suitecut::LaunchOptions remote;
remote.connect_headers["Authorization"] = "Bearer TOKEN_FROM_HOST";
remote.tls_ca_file = "/absolute/path/to/ca.pem";
auto browser = suitecut::Browser::connect_chromium(
    "wss://browser.example/devtools/browser/ID", remote);
```

The C ABI adds `sc_client_launch_firefox`, `sc_client_connect_firefox` and
`sc_client_connect_remote`. The last accepts a separately initialized
`sc_connection_options` structure for TLS/authentication, preserving the ABI-v1
`sc_launch_options` layout.

## Drive the browser from an agent

`suitecut-native-control` retains one browser session and accepts one JSON
request per line. It is a native executable; the host supplies the agent or
model. Each request produces a structured result or error on stdout:

```sh
build/native-client/suitecut-native-control --browser /usr/bin/chromium
```

```json
{"tool":"tools"}
{"tool":"navigate","arguments":{"url":"https://example.com"}}
{"tool":"snapshot","arguments":{}}
{"tool":"fill","arguments":{"target":{"kind":"label","value":"Email"},"text":"person@example.com"}}
{"tool":"click","arguments":{"target":{"kind":"ref","value":"REFERENCE_FROM_SNAPSHOT"}}}
{"tool":"get","arguments":{"kind":"title"}}
```

The legacy full driver catalogue contains **53 tools**, including semantic/ref actions,
frame targets, file uploads, screenshots,
PDF, waits, history, shared-context tabs, state, semantic diffs, and the network,
dialog, console and emulation families. `tools` returns the authoritative input
schemas. `submit` returns a request ID; `result` polls it while `events` and
dialog/DevTools continuations remain usable. `cancel` retires it without
replaying input. Check the page before repeating any dispatched mutation.

For an MCP host, configure the installed executable directly as a stdio server:

```json
{
  "mcpServers": {
    "suitecut": {
      "command": "/absolute/install/prefix/bin/suitecut-native-control",
      "args": ["--browser", "/usr/bin/chromium", "--mcp"]
    }
  }
}
```

MCP defaults to the core capability group described above; add `--tools all`
for the complete driver catalogue. `--connect 9222` attaches instead of launching; `--backend firefox --browser
/usr/bin/firefox` selects Firefox/BiDi; `--backend cef --browser
/absolute/path/to/suitecut-browser` selects CEF. `--connect-header Name:value`,
`--tls-ca`, `--tls-cert` and `--tls-key` configure remote connections.
`--profile`, repeated
`--browser-arg` values, and `--headed` are also supported. Closing stdin retires
the session. MCP screenshots are image content; PDFs are embedded resources.
Website observations and WebMCP descriptions/results are untrusted page data.
On the tested Chrome 156 runtime, WebMCP uses the real
`document.modelContext.getTools()` API and passes the selected live
`RegisteredTool` plus a parameter object to `executeTool`. The driver returns
JSON metadata without the record's cyclic Window reference, then reacquires the
matching record for explicit invocation. Duplicate names require an origin or
frame scope. Legacy browser `listTools`/testing APIs are also supported when
present. WebMCP requires browser support and may require experimental feature
flags; absence is an explicit error.

C++ applications can call `AgentDriver::tools()` and
`AgentDriver(browser).call(name, arguments)` from `suitecut/agent_driver.hpp`.
The driver creates no model runtime or per-call threads. Keep one driver per
ordered agent session and serialize its calls. `Agent::run` remains the bounded
batch interface for dependent actions with optional UI postconditions.

## Credentials, image comparison and capture artifacts

`suitecut/credentials.hpp` provides `CredentialVault`, an authenticated encrypted
store with private POSIX paths and bounded entries. Applications supply the
master password and attach a shared vault using
`AgentDriver::set_credential_vault`. Agent `credentials` actions save/list/remove
records or explicitly log in through native form actions. Responses contain
names/status, never saved secret values. Guarded login is available on the
native Chromium and Firefox backends; the vault itself is browser-independent.
Login checks the saved origin inside each private DOM fill operation, including
locator waits. Guarded fills use native DOM value setters and JavaScript
input/change notifications; the final submit uses trusted browser input.
A submitted login still needs a UI
readback to confirm its outcome. Direct C++ `CredentialVault::get` returns the
credential to its application caller.

The CLI accepts `--credential-vault /private/path/vault` and
`--credential-password-env ENVIRONMENT_VARIABLE_NAME`. It reads and removes that
master-password environment variable before launching browser/encoder children.
Browser cookie/storage snapshots remain a separate plaintext JSON interface;
they are not automatically persisted in the encrypted vault.

`suitecut/image.hpp` decodes/encodes static PNG and compares RGBA pixels with a
per-channel tolerance, optional alpha comparison, dimension reporting and exact
changed-pixel counts/ratio. It supports all standard PNG color types/depths and
Adam7, validates CRC/order/zlib streams, and enforces image/byte limits. APNG is
rejected; color-profile/gamma conversion and perceptual similarity are not
performed. Optional diff PNGs mark changed/missing pixels magenta and unchanged
pixels transparent. Dimension mismatches use the documented bounding canvas.
The native `image_compare` tool reads two PNG paths, or captures the active page
when `actualPath` is omitted; `diffPath` must name a new output file:

```json
{"tool":"image_compare","arguments":{"expectedPath":"/absolute/baseline.png","channelTolerance":3,"diffPath":"/absolute/new-diff.png"}}
```

Chromium capture adds `Page::start_recording/stop_recording/recording_status/
cancel_recording`, `start_tracing/stop_tracing` and
`start_profiler/stop_profiler`, with corresponding `recording`, `tracing` and
`profiler` agent tools. Recording accepts MP4/WebM, bounded capture settings and
1–120 output FPS. Source frames are resampled to that output timeline; output
FPS is not a promise of distinct compositor frames at that rate. A recording
uses a bounded source spool and an FFmpeg/ffprobe finalization helper, with
metadata alongside the video. Trace/profile JSON and media artifacts are
created exclusively. These Chromium features have separate runtime validation;
their presence does not imply Firefox support.

## Browser-control coverage

The public command inventory of **agent-browser 0.39.1** is the coverage
reference. The table describes the Chromium backend; CEF retains its native
engine's command availability. This is an independently implemented native browser SDK and agent
tool interface. It does not include that product's AI chat application,
installer/updater or provider ecosystem. The vault, image and media features
above are independent native implementations. Implemented surfaces
and their finite validation evidence are described separately in
[VALIDATION.md](VALIDATION.md).

| Control family | Native surface | Scope and limits |
| --- | --- | --- |
| Launch and connect | Chromium/Firefox launch/connect, C ABI remote connect | POSIX; CDP or BiDi; HTTP(S)/WS(S), authentication headers, verified TLS and optional client certificates |
| Tabs and contexts | `Browser::new_page/pages`, `new_context`, driver `pages` | Launched-browser tabs share storage; explicit contexts isolate it; attached caller-default tab creation is refused |
| Navigation/history | `navigate`, `go_back`, `go_forward`, `reload`, driver `history` | Commit, DOM-content, load and bounded network-idle waits |
| Agent observations | `observe`, scoped observations, driver `snapshot/diff` | Document-bound real element refs; semantic diff ignores ref churn |
| Locators and getters | CSS, role/label/text, placeholder/test ID, alt/title, `nth`, getters | Open shadow roots; strict targets and deadlines |
| Frames | `frame`, nested `frame_locator`, frame targets in driver tools | Resolve frame IDs or selector paths; same- and cross-origin/OOPIF controls |
| Pointer/keyboard/forms | Click/double-click, hover, drag, focus, fill/type, keys, select/check | Native CDP input; inspect outcomes after failure/cancellation |
| File upload | `Locator::set_input_files`, driver `upload` | Absolute local files or one directory on a `webkitdirectory` input; multiple files and clear; no in-memory upload API |
| Waits | Locator/page/frame waits; driver `wait_for` | URL, predicate, text, selector/state and load-state controls |
| Screenshots | PNG/JPEG/WebP, clip/full page, masks/color/style, animation handling | Bounded main-document/open-shadow masks; framed masks and disabling animations across frames fail explicitly |
| PDF | `Page::pdf`, driver `pdf` | CDP paper dimensions/options; unsupported convenience option names fail explicitly |
| Downloads | Configure directory, list/status/cancel/path/save | Browser-local filesystem; GUID-owned artifacts and main-frame download records |
| Network and HAR | Requests/responses/body, HAR start/stop/export | Bounded journals and HAR metadata; not a full traffic archive/replay system |
| Routes | Context-scoped glob continue/abort/mock; raw `Fetch.*` | Static rules and explicit host-managed CDP continuations; no JavaScript callback runtime |
| Headers/auth/offline | Context/page setters | Extra HTTP headers, HTTP Basic challenge credentials and offline mode |
| Console/errors/dialogs | Typed journals and dialog replies; driver families/events | Bounded event history; modal continuations can unblock active evaluation |
| Init scripts/media | Add/list/remove init scripts; media emulation and raw DevTools | Context-scoped scripts; additional CDP emulation remains explicitly addressable |
| Browser state | Cookie APIs, storage get/restore, driver `state` | Context cookies and current-origin local/session storage; restore applies once to the current origin; no deferred-origin or IndexedDB/service-worker state |
| Credentials | `CredentialVault`, driver `credentials` | Host-configured encrypted store; secret values are not returned by agent tools; login submits actual form actions |
| Agent/MCP/WebMCP | `AgentDriver`, JSON lines, MCP stdio, page WebMCP tools | Host supplies the agent/model; WebMCP requires the browser API |
| Trace/video/profiling | Typed Chromium capture and agent families | Bounded output, exclusive artifacts, FFmpeg finalization for MP4/WebM; Firefox helpers explicitly unavailable |
| Pixel comparison | `decode_png/encode_png/compare_png`, driver `image_compare` | Native bounded PNG/ZLIB; exact channel tolerances, dimension changes and optional diff; no perceptual/color-profile comparison |
| Native escape hatch | Asynchronous transport, events, `Page::devtools` | Backend-specific commands; unsupported operations return errors |

Populated file/directory selections use native CDP. Clearing a selection uses
JavaScript `input`/`change` notifications with `isTrusted:false`.

The new HTTP workflow executable embeds its fixture server in C++ and exercises
actual frames, uploaded/downloaded bytes, network interception, storage and
agent tool calls without Node:

```sh
build/native-client/suitecut-native-chromium-workflows \
  --browser /usr/bin/chromium --output /tmp/chromium-workflows/results.json
```

## API and agent control

`suitecut/browser.hpp` provides browser, context, page, and locator objects.
`suitecut/transport.hpp` exposes asynchronous requests and event polling for all
native engine command families. `suitecut/json.hpp` supplies owning JSON values.
The agent example demonstrates native launch and browser interaction:

```sh
build/native-client/suitecut-native-agent /absolute/path/to/suitecut-browser
build/native-client/suitecut-native-command /absolute/path/to/suitecut-browser \
  '{"command":"evaluate","expression":"40+2"}'
```

```cpp
#include <suitecut/browser.hpp>

suitecut::LaunchOptions options;
options.executable = "/absolute/path/to/suitecut-browser";
auto browser = suitecut::Browser::launch(options);
auto context = browser.new_context();
auto page = context.new_page();
page.navigate("https://example.com");
auto observation = page.observe(); // Native accessibility nodes and references.
// Agent selects an observed ref, then calls page.get_by_ref(ref).click().
auto title = page.evaluate("document.title").as_string();
// Destruction closes owned resources. Explicit close reports shutdown errors.
browser.close();
```

`suitecut/observation.hpp` adds `compact_observation(page.observe(), options)`
and stateless `observation_diff(previous, current)`. Compact output retains
actionable references, states and ancestor context, with explicit truncation
flags for node, text and traversal budgets. Differences ignore reference-only
changes; ambiguous duplicate controls remain explicit. These helpers run in the
client after a full engine snapshot; they reduce model input, not DOM traversal.
Use `page.locator(selector).observe()` to capture a specific real subtree on a
large page. Each successful scoped capture replaces the active references in
that frame; omitted references expire, while retained elements keep their tokens.

`suitecut/agent.hpp` provides `Agent::run(page, actions, options)` for a bounded
ordered list of native actions, with optional final observation. For example:

```cpp
#include <suitecut/agent.hpp>
#include <suitecut/observation.hpp>

auto result = suitecut::Agent::run(page, suitecut::Json::array{
  suitecut::Json{{"action", "fill"}, {"target", suitecut::Json{{"kind", "label"},
    {"value", "Email"}}}, {"value", "person@example.com"}},
  suitecut::Json{{"action", "click"}, {"target", suitecut::Json{{"kind", "role"},
    {"value", "button"}, {"name", "Continue"}}}}
});
auto structured_result = result.json();
```

The complete action list is validated before execution. Execution stops on the
first failure; subsequent actions are marked skipped. Acknowledged successes
are marked completed. A failure after dispatch is conservatively marked
`outcomeUnknown`, because the current facade does not expose whether input
reached the page. Inspect the page before repeating a mutation. Actions are
sequential, not an atomic transaction, and the helper does not retry them.
These are C++ helpers; the C ABI continues to expose individual raw commands.

`AgentBatchOptions::max_duration` defaults to 60 seconds and caps the entire
batch, including readbacks and its final observation. Each command receives the
smaller of the page timeout and the remaining batch time. The caller's page
timeout is unchanged. Budget failures retain whether a mutation was sent or
acknowledged, and later steps stay skipped.

An action can optionally include `expect: {kind, target, value}`. Kinds are
`inputValue`, `text` (textContent), and `count`; `url` accepts only `kind` and
`value`. Checks read once and compare exactly. For example:

```cpp
suitecut::Json email{{"kind", "label"}, {"value", "Email"}};
auto result = suitecut::Agent::run(page, suitecut::Json::array{
  suitecut::Json{{"action", "fill"}, {"target", email}, {"value", "person@example.com"},
    {"expect", suitecut::Json{{"kind", "inputValue"}, {"target", email},
      {"value", "person@example.com"}}}}
});
```

An acknowledged action has `effect: unverified` unless a supplied check matches.
A match sets `confirmed`, confirming that specific UI state at readback time.
A mismatch sets `unconfirmed`, stops the batch, and keeps `execution: completed`
because input was already acknowledged. Readback errors leave the effect
unverified. Evidence includes kind/matched without echoing expected or observed
values. Errors identify validation, action, verification, or observation phase.

`suitecut/visual.hpp` adds an optional capture-aware pointer path:

```cpp
#include <suitecut/visual.hpp>
suitecut::VisualController visual(page);
auto capture = visual.capture(); // CSS-scale viewport PNG by default.
// Send capture.png, capture.id and its pixel dimensions to the agent.
visual.click(capture.id, image_x, image_y); // Coordinates in that exact PNG.
```

Captures retain actual PNG dimensions, the effective CSS crop, and independent
pixel-to-CSS scales. `VisualCaptureOptions::clip` is viewport-relative CSS;
partially outside crops are intersected with the viewport. Set `scale` to
`device` for native HiDPI pixels. The controller retains one metadata entry and
no second PNG copy. Capturing again retires the old token. A matching click
attempt consumes its token even if validation or input fails; tokens expire
after 30 seconds. Document identity, viewport, scroll and scale are rechecked
before input. The checks add browser round trips and cannot detect every DOM
layout shift or eliminate the final check/input race. Captures are viewport
only and refuse pinch zoom. Other mutation of the same page must be serialized.

These independently authored additions adapt ideas reviewed in
[Cua Driver](https://github.com/trycua/cua/tree/7487f66931c696f55c392476eda2c1698376604c/libs/cua-driver/rust/crates/cua-driver-core).
They add no Cua runtime or model dependencies. Existing native locator and
held-input cleanup paths remain responsible for their own input lifecycles.

The equivalent C entry point is `sc_client_launch`. Initialize options with
`sc_launch_options_init`, set `options.executable`, and submit command objects
with `sc_client_submit` or `sc_client_call`. Each returned `sc_packet` exposes its
header and payload through length-bearing accessors. Use `sc_packet_destroy`,
`sc_request_destroy` and `sc_client_destroy` to release the corresponding handles.
`sc_client_ready` supplies the native handshake and capability metadata.
The plain C command example launches once per invocation; agents should retain
one library client across actions to avoid repeated browser startup.

Raw requests are JSON objects containing a `command`, a `pageId` where needed,
and that command's fields. The transport assigns `version` and an increasing
request `id`; the complete serialized command must fit the engine's 65,536-byte
line limit. Raw responses retain the original header and binary payload,
including engine `ok:false` error responses. The typed facade checks engine
errors. Binary buffers are moved between the decoder, queue and consumer.

For the CEF backend, the engine's existing commands for navigation, input,
locators, evaluation, contexts, network, storage, downloads, recording and
streaming remain accessible through the generic transport. This is protocol access, not a claim that every
JavaScript convenience method has a typed C++ equivalent. Advanced route,
binding, dialog and predicate callbacks require polling their events and
sending the appropriate engine continuation command. The client runs no user
callbacks on its I/O thread, so application work cannot block response decoding.

For agents, use semantic observations and their engine-issued references rather
than retaining DOM handles unnecessarily. Multiple independent requests may be
submitted before awaiting their futures; dependent actions must retain their
order. Cancellation is explicit, and dropping a future does not cancel browser
work. A command deadline also initiates remote cancellation and retires local
pending state. Waiting for a C request with a shorter timeout leaves that
request usable for a later wait or cancellation.

## Memory and lifetime

The default transport permits 256 pending commands, bounds outgoing command
bytes, and caps queued events at 64 MiB without preallocating that space. Raw
frame transmission is on demand. The event budget can be lowered for agents
which do not consume frames. Poll subscribed events regularly: disposable
media events may be dropped under pressure, while critical event overflow fails
the connection instead of losing a callback that browser work depends on.
Protocol headers, payloads, JSON depth, nodes and strings have explicit limits.
Completed futures and packets belong to the application; retaining an arbitrary
number of completed results also retains their buffers. The queue limits do not
cap application-owned results. JSON preserves exact signed 64-bit integers and
uses finite doubles for larger numeric values. It requires valid Unicode scalar
strings; use raw packet payloads if you need to preserve serialized lone UTF-16
surrogates from JavaScript.

C++ objects use RAII. C callers must destroy returned packets and request handles
with their matching functions. Those objects may outlive the client handle;
client destruction still closes the browser. Destroying any handle requires
that no concurrent call is using that same handle. Request results are consumed
once. The C ABI catches exceptions and returns status/error data.

Shutdown closes engine stdin, waits for graceful exit, and escalates within the
owned process group if necessary. The library reaps its direct child and does
not change process-wide SIGPIPE handling or install itself as a subreaper.
Applications supervising arbitrary descendants should retain their own process
supervisor; processes that deliberately escape the owned group are outside
this transport's ownership. Windows transport is not implemented.

## Sanitizers and fuzzing

```sh
cmake -S native/client -B build/native-client-asan \
  -DCMAKE_BUILD_TYPE=Debug -DSUITECUT_CLIENT_SANITIZERS=ON
cmake --build build/native-client-asan -j 2
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  ctest --test-dir build/native-client-asan --output-on-failure
```

The client includes strict JSON tests, fragmented/corrupt packet tests,
deterministic mutation fuzzing, C ABI tests and fake-engine lifecycle tests.
Browser integration and measured workload tools are kept separate from the
default test run because they require a built native runtime. Sanitizers on
this library do not instrument the separately compiled CEF/Chromium library.

## Bounded independent reads

The engine manages its internal background worker threads automatically from
CPU affinity and container CPU quotas. Workers start on demand and are reused
within a bounded shared pool. Normal SDK calls require no worker or request
window setting; the SDK uses one persistent I/O thread for browser communication.
The example below is an optional way to overlap independent application reads.

The `suitecut-native-read-window` example submits independent evaluations through
the existing asynchronous transport, using a sliding window of four requests by
default and a hard maximum of sixteen. It creates no request worker threads.
Each consumed result opens a slot for the next request; output is streamed in
submission order, so a slow earlier request can delay replenishing the window.
`LaunchOptions::max_pending` is set to the window size. Each request has a
30-second deadline, and an error cancels the remaining outstanding request IDs.
Cancellation is best effort and does not undo work already performed.

```sh
build/native-client/suitecut-native-read-window /absolute/path/to/suitecut-browser
build/native-client/suitecut-native-read-window /absolute/path/to/suitecut-browser 4 \
  'document.title' 'location.href' 'document.readyState'
```

The sample reads the initial page. Applications can use the same window with
their existing pages by supplying each page's `id()` as the request's `pageId`.
The caller must ensure expressions are read-only and independent; arbitrary
JavaScript can mutate the page, and this example does not inspect or sandbox it.
Keep dependent navigation, input, and verification steps ordered, as in
`Agent::run`. Pending requests are bounded, but results retained by application
code still consume application-owned memory.

Each output line contains `index`, `pageId`, and `nativeResult`. The latter is
the original evaluation envelope returned by `response_json()`, including
`result` and any `exceptionDetails` or unserializable-value metadata. Engine
`ok:false` responses fail the example; JavaScript exception metadata remains in
the output. This differs from `Page::evaluate()`, which converts JSON-compatible
values and throws for JavaScript exceptions or unsupported values. See
[`examples/read_window.cpp`](examples/read_window.cpp) for submission, bounded
storage, deadline, and cancellation handling.
