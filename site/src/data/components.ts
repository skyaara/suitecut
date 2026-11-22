// Every part suitecut ships: what it is, its diagram, a quick start, facts and
// limits. The component pages, the home page and the diagrams all read from here.
import { type Doodle } from '../lib/doodle.js'

export type Spec = readonly [string, string]

export type Language = 'cpp' | 'c' | 'json' | 'sh' | 'cmake'

export interface Example {
  readonly title: string
  readonly language: Language
  readonly code: string
}

export interface Component {
  readonly slug: string
  readonly name: string
  readonly kind: string
  readonly status: string
  readonly line: string
  /** The one sentence on the page that carries the highlighter. */
  readonly marker: string
  readonly intro: readonly string[]
  readonly doodle: Doodle
  /** The quickest way in. */
  readonly start: Example
  readonly examples: readonly Example[]
  readonly facts: readonly Spec[]
  readonly limits: readonly string[]
  readonly links: readonly Spec[]
}

const repo = 'https://github.com/skyaara/suitecut/blob/main'

const engineDoodle: Doodle = {
  width: 960,
  height: 420,
  seed: 7,
  title:
    'Your program sends commands to the suitecut engine, which runs Chromium. Frames and audio come out of the renderer and become a recording or a live stream.',
  nodes: [
    {
      id: 'program',
      x: 16,
      y: 160,
      w: 176,
      h: 104,
      label: 'your program',
      detail: 'JSON commands\non stdin',
      fill: 'yellow',
    },
    {
      id: 'engine',
      x: 246,
      y: 120,
      w: 220,
      h: 184,
      label: 'Chromium',
      detail: 'inside the\nengine',
      shape: 'window',
      fill: 'chrome',
    },
    {
      id: 'frames',
      x: 528,
      y: 40,
      w: 178,
      h: 84,
      label: 'frames',
      detail: 'as they paint',
      shape: 'round',
      fill: 'blue',
    },
    {
      id: 'audio',
      x: 528,
      y: 170,
      w: 178,
      h: 84,
      label: 'audio',
      detail: 'from the page',
      shape: 'round',
      fill: 'blue',
    },
    {
      id: 'packets',
      x: 528,
      y: 300,
      w: 178,
      h: 84,
      label: 'packets',
      detail: 'on stdout',
      shape: 'round',
    },
    {
      id: 'video',
      x: 776,
      y: 40,
      w: 168,
      h: 100,
      label: 'recording',
      detail: 'MP4 or WebM',
      fill: 'red',
    },
    { id: 'live', x: 776, y: 196, w: 168, h: 100, label: 'live stream', detail: 'RTMP or TCP' },
  ],
  edges: [
    { from: 'program', to: 'engine', carries: 'command' },
    { from: 'engine', to: 'frames' },
    { from: 'engine', to: 'audio', carries: 'sound' },
    { from: 'engine', to: 'packets', carries: 'command' },
    { from: 'frames', to: 'video' },
    { from: 'audio', to: 'live', carries: 'sound' },
    { from: 'packets', to: 'program', bend: 30, dashed: true, carries: 'command' },
  ],
}

const nativeClientDoodle: Doodle = {
  width: 920,
  height: 410,
  seed: 59,
  title:
    'A C++ app, any language through the C ABI, or an MCP client through the native-control CLI all use SuiteCut::Client, which drives the suitecut engine, Chrome or Firefox.',
  nodes: [
    {
      id: 'cpp',
      x: 12,
      y: 16,
      w: 196,
      h: 92,
      label: 'C++ app',
      detail: 'C++20 headers',
      fill: 'yellow',
    },
    { id: 'abi', x: 12, y: 160, w: 196, h: 92, label: 'any language', detail: 'through the C ABI' },
    {
      id: 'agent',
      x: 12,
      y: 304,
      w: 196,
      h: 92,
      label: 'MCP client',
      detail: 'or a shell script',
      fill: 'blue',
    },
    {
      id: 'cli',
      x: 290,
      y: 304,
      w: 212,
      h: 92,
      label: 'native-control',
      detail: 'CLI and MCP server',
      shape: 'round',
    },
    {
      id: 'client',
      x: 282,
      y: 140,
      w: 232,
      h: 112,
      label: 'SuiteCut::Client',
      detail: 'no CEF headers',
      shape: 'round',
      fill: 'chrome',
    },
    { id: 'engine', x: 684, y: 8, w: 222, h: 112, label: 'suitecut engine', shape: 'window' },
    {
      id: 'chrome',
      x: 684,
      y: 148,
      w: 222,
      h: 112,
      label: 'Chrome',
      detail: 'CDP pipe',
      shape: 'window',
    },
    {
      id: 'firefox',
      x: 684,
      y: 288,
      w: 222,
      h: 112,
      label: 'Firefox',
      detail: 'WebDriver BiDi',
      shape: 'window',
    },
  ],
  edges: [
    { from: 'cpp', to: 'client' },
    { from: 'abi', to: 'client' },
    { from: 'agent', to: 'cli' },
    { from: 'cli', to: 'client' },
    { from: 'client', to: 'engine' },
    { from: 'client', to: 'chrome' },
    { from: 'client', to: 'firefox' },
  ],
}

const controlDoodle: Doodle = {
  width: 960,
  height: 400,
  seed: 33,
  title:
    'A shell runs one command per call against a named session, and an MCP client talks to the same executable over stdio. Both reach one browser that stays alive between calls.',
  nodes: [
    {
      id: 'shell',
      x: 16,
      y: 40,
      w: 196,
      h: 100,
      label: 'a shell',
      detail: 'one command\na call',
      fill: 'yellow',
    },
    {
      id: 'mcp',
      x: 16,
      y: 250,
      w: 196,
      h: 100,
      label: 'MCP client',
      detail: 'stdio',
      fill: 'blue',
    },
    {
      id: 'control',
      x: 300,
      y: 140,
      w: 252,
      h: 116,
      label: 'native-control',
      detail: 'named sessions,\ntool groups',
      shape: 'round',
      fill: 'chrome',
    },
    {
      id: 'browser',
      x: 640,
      y: 90,
      w: 304,
      h: 220,
      label: 'one browser',
      detail: 'CEF, Chrome\nor Firefox',
      shape: 'window',
    },
  ],
  edges: [
    { from: 'shell', to: 'control', carries: 'command' },
    { from: 'mcp', to: 'control', both: true, carries: 'command' },
    { from: 'control', to: 'browser', both: true, carries: 'command' },
  ],
}

const embedDoodle: Doodle = {
  width: 960,
  height: 440,
  seed: 48,
  title:
    'Your Mac app opens a session with the session service, which owns the engine. Each tab’s frames come back to the app as IOSurfaces, and an agent the app grants access works in the same tabs.',
  nodes: [
    {
      id: 'app',
      x: 16,
      y: 40,
      w: 236,
      h: 176,
      label: 'your Mac app',
      detail: 'shows the tab,\npasses on input',
      shape: 'window',
      fill: 'yellow',
    },
    {
      id: 'agent',
      x: 36,
      y: 300,
      w: 196,
      h: 100,
      label: 'agent',
      detail: 'joins with a grant',
      shape: 'circle',
      fill: 'blue',
    },
    {
      id: 'session',
      x: 352,
      y: 160,
      w: 236,
      h: 120,
      label: 'suitecut-session',
      detail: 'tabs, owners,\nwho is in control',
      shape: 'round',
      fill: 'chrome',
    },
    {
      id: 'engine',
      x: 690,
      y: 110,
      w: 254,
      h: 220,
      label: 'suitecut engine',
      detail: 'inside your\napp bundle',
      shape: 'window',
    },
  ],
  edges: [
    { from: 'app', to: 'session', both: true, carries: 'command' },
    { from: 'agent', to: 'session', carries: 'command' },
    { from: 'session', to: 'engine', both: true, carries: 'command' },
    { from: 'engine', to: 'app', label: 'IOSurface frames', bend: -40 },
  ],
}

const engineStart = `cmake -S native/browser -B .suitecut/native-build -G Ninja \\
  -DCEF_ROOT="$PWD/.suitecut/cef-sdk" \\
  -DLIBYUV_ROOT="$PWD/.suitecut/libyuv" \\
  -DPROJECT_ARCH=arm64 -DCMAKE_BUILD_TYPE=Release
cmake --build .suitecut/native-build --target suitecut-browser --parallel 1`

const engineProtocol = `{"version":1,"id":1,"command":"navigate","url":"https://example.com"}
{"version":1,"id":2,"command":"devtools","method":"Runtime.evaluate","params":{"expression":"document.title","returnByValue":true}}`

const recordCode = `suitecut-native-record --url https://example.com/ --output /tmp/page.webm \\
  --duration-ms 5000 --fps 30 --audio`

const clientStart = `find_package(SuiteCutClient CONFIG REQUIRED)
target_link_libraries(app PRIVATE SuiteCut::Client)`

const cppCode = `#include <suitecut/browser.hpp>

suitecut::LaunchOptions options;
options.executable = "/absolute/path/to/suitecut-browser";
auto browser = suitecut::Browser::launch(options);
auto context = browser.new_context();
auto page = context.new_page();
page.navigate("https://example.com/");
page.get_by_label("Name").fill("Ada");
page.get_by_role("button", "Save").click();
auto observation = page.observe(); // accessibility nodes and refs
browser.close();`

const agentCode = `#include <suitecut/simple_agent.hpp>

suitecut::AgentDriver driver(browser);
suitecut::SimpleAgent agent(driver);

auto seen = agent.call("observe", {{"format", "compact"}});
auto done = agent.call("act", {
    {"action", "fill"}, {"ref", "e1"}, {"value", "Ada"}});
auto ready = agent.call("wait", {
    {"text", "Saved"}, {"timeoutMs", 2000}});`

const cCode = `#include <suitecut/client.h>

sc_launch_options options;
sc_launch_options_init(&options);
options.executable = "/absolute/path/to/suitecut-browser";
sc_error error;
sc_client *client = NULL;
if (sc_client_launch(&options, &client, &error) != SC_OK) return 1;
const char *command = "{\\"command\\":\\"evaluate\\",\\"expression\\":\\"40+2\\"}";
sc_packet *packet = NULL;
sc_client_call(client, command, strlen(command), 30000, &packet, &error);
sc_packet_destroy(packet);
sc_client_close(client, &error);
sc_client_destroy(client);`

const controlStart = `suitecut-native-control --session shop --browser /usr/bin/chromium open https://example.com/`

const cliCode = `suitecut-native-control --session shop snapshot
# Choose the search box's ref from the returned controls.
suitecut-native-control --session shop --response none fill e12 'mechanical keyboard'
suitecut-native-control --session shop press Enter
suitecut-native-control --session shop screenshot --output /tmp/results.png
suitecut-native-control --session shop close`

const mcpConfig = `{
  "mcpServers": {
    "suitecut": {
      "command": "/absolute/install/prefix/bin/suitecut-native-control",
      "args": ["--browser", "/usr/bin/chromium", "--mcp", "--tools", "core,network"]
    }
  }
}`

const jsonLines = `{"tool":"tools"}
{"tool":"navigate","arguments":{"url":"https://example.com"}}
{"tool":"snapshot","arguments":{}}
{"tool":"get","arguments":{"kind":"title"}}`

const kitStart = `python3 scripts/package-embed-kit-macos.py fetch-cef --cef official --dest .suitecut/cef-official-sdk
python3 scripts/package-embed-kit-macos.py package --cef official --cef-sdk .suitecut/cef-official-sdk \\
  --engine-build .suitecut/kit-engine-build --session .suitecut/kit-client-build/suitecut-session \\
  --deps .suitecut/kit-deps --output .suitecut/embed-kit --replace`

const embedCode = `#include <suitecut/embed.h>

sc_error error;
sc_runtime_options runtime_options;
sc_runtime_options_init(&runtime_options);
runtime_options.path = "/path/to/embed-kit";
sc_runtime_info runtime;
sc_runtime_info_init(&runtime);
if (sc_runtime_inspect(&runtime_options, &runtime, &error) != SC_OK) return 1;

sc_session_options session_options;
sc_session_options_init(&session_options);
session_options.runtime = &runtime;
session_options.name = "example";
session_options.directory = "/absolute/private/run";
session_options.profile = "/absolute/private/profile";
session_options.owner = "app";
session_options.flags = SC_SESSION_START | SC_SESSION_PROMPTS;
sc_session *session = NULL;
if (sc_session_open(&session_options, &session, &error) != SC_OK) return 1;

sc_tab tab;
sc_tab_init(&tab);
sc_tab_open(session, "https://example.com/", 0, &tab, &error);

sc_surface_sink sink;
sc_surface_sink_init(&sink); // SC_SINK_LIBRARY: read frames with sc_view_next_frame
sc_view_options view_options;
sc_view_options_init(&view_options);
view_options.page = tab.page;
view_options.sink = &sink;
sc_view *view = NULL;
sc_view_attach(session, &view_options, &view, &error);

sc_frame frame;
sc_frame_init(&frame);
if (sc_view_next_frame(view, 200, &frame, &error) == SC_OK) {
  // frame.surface is an IOSurfaceRef in NV12; draw it, then release it.
  sc_frame_release(&frame);
}`

export const components: readonly Component[] = [
  {
    slug: 'engine',
    name: 'suitecut-browser',
    kind: 'CEF 156 engine',
    status: 'Linux x64 · macOS ARM64',
    line: 'Chromium embedded through CEF, with frames and sound taken from the renderer',
    marker:
      'Each frame comes from the paint callback and the sound from the audio stream, so there is no screenshot loop.',
    intro: [
      'suitecut-browser is a Chromium Embedded Framework program pinned to CEF 156.0.30. Your program starts it and talks to it over stdin and stdout: newline-delimited JSON commands in, length-prefixed binary packets out.',
      'Each frame comes from the paint callback and the sound from the audio stream, so there is no screenshot loop.',
      'One launch owns one sandboxed engine that hosts isolated contexts and many pages. Recording and live publishing run inside it on one clock, through FFmpeg or, on macOS, the hardware H.264 encoder. Most programs drive it through SuiteCut::Client rather than the raw protocol.',
    ],
    doodle: engineDoodle,
    start: { title: 'Build it on macOS ARM64', language: 'sh', code: engineStart },
    examples: [
      { title: 'Talk to it directly', language: 'json', code: engineProtocol },
      { title: 'Record a page with the bundled recorder', language: 'sh', code: recordCode },
    ],
    facts: [
      ['Pinned build', 'CEF 156.0.30 on Chromium 156.0.8078.96, stable channel'],
      ['Video', 'CefRenderHandler::OnPaint, BGRA8 or I420 frames with paint IDs'],
      ['Audio', 'CefAudioHandler::OnAudioStreamPacket, 48 kHz stereo PCM'],
      ['Protocol', 'JSON commands on stdin, length-prefixed binary packets on stdout'],
      ['Backpressure', 'Callbacks never wait on pipes; one pending frame, audio queued whole'],
      ['Recording', 'WebM, or hardware H.264 MP4 on macOS, at any output rate'],
      ['Publishing', 'RTMP, RTMPS or TCP, with an optional reconnect policy'],
      ['Playback', 'H.264, AAC and HEVC on macOS ARM64 through a third-party codec build'],
    ],
    limits: [
      'Windows configuration fails on purpose until its sandbox and packaging exist.',
      'The official Linux CEF build plays no H.264 or AAC.',
      'The macOS codec build is a third-party build and carries no codec patent licence.',
    ],
    links: [
      ['Engine README', `${repo}/native/browser/README.md`],
      ['Build requirements', `${repo}/docs/building.md`],
      ['Playback codecs', `${repo}/native/browser/CODECS.md`],
      ['How it works', '/docs/how-it-works'],
      ['Recording and streaming', '/docs/recording'],
    ],
  },
  {
    slug: 'native-sdk',
    name: 'SuiteCut::Client',
    kind: 'C++20 library and C ABI',
    status: 'Linux · macOS ARM64',
    line: 'Browser control from C++ and any language, for the engine, Chrome and Firefox',
    marker: 'Three agent tools: observe, act and wait.',
    intro: [
      'SuiteCut::Client is a C++20 library with a versioned C ABI. It drives the suitecut engine, an installed Chrome or Chromium over a private CDP pipe, or Firefox over WebDriver BiDi, with no CEF headers and no automation library.',
      'Three agent tools: observe, act and wait.',
      'Browser, context, page and locator objects cover navigation, input, frames, uploads, downloads, network, HAR, storage and screenshots. The same library holds the agent driver behind suitecut-native-control and the embed API.',
    ],
    doodle: nativeClientDoodle,
    start: { title: 'Link it from CMake', language: 'cmake', code: clientStart },
    examples: [
      { title: 'Launch the engine and fill a form', language: 'cpp', code: cppCode },
      { title: 'Give an agent three tools', language: 'cpp', code: agentCode },
      { title: 'Or call the C ABI', language: 'c', code: cCode },
    ],
    facts: [
      ['Library', 'C++20 headers and a versioned C ABI, static or shared'],
      ['Backends', 'suitecut engine, Chrome or Chromium over CDP, Firefox over BiDi'],
      ['Attach', 'Connect to a running browser, with headers and TLS client certificates'],
      ['Agent tools', 'observe, act and wait; any number of actions in one act'],
      ['Refs', 'Short element refs, replaced on every observation; stale refs are rejected'],
      ['Safety', 'Mutations are never retried; password values are redacted'],
      ['Extras', 'Encrypted credential vault, PNG comparison, Chromium recording and tracing'],
    ],
    limits: [
      'Linux and macOS on Apple silicon are qualified separately. Windows transport is not implemented.',
      'Safari and WebKit are not supported.',
      'Feature support differs by backend; unsupported calls fail with an explicit error.',
    ],
    links: [
      ['Client README', `${repo}/native/client/README.md`],
      ['Architecture', `${repo}/native/client/ARCHITECTURE.md`],
      ['Validation', `${repo}/native/client/VALIDATION.md`],
      ['Agents and MCP', '/docs/agents'],
    ],
  },
  {
    slug: 'native-control',
    name: 'suitecut-native-control',
    kind: 'CLI and MCP server',
    status: 'Linux · macOS ARM64',
    line: 'A command line with persistent sessions that also runs as an MCP server',
    marker: 'It is one native executable; your host brings the agent.',
    intro: [
      'suitecut-native-control is the agent driver from SuiteCut::Client as a program. A named session keeps a browser alive between shell commands, one request per line works for scripts, and --mcp serves the same tools to an MCP client over stdio.',
      'It is one native executable; your host brings the agent.',
      'It launches or attaches to the suitecut engine, Chrome or Firefox. The default core group has the three agent tools and a few navigation and reading tools; tool groups add network, state, debug and media tools, up to the full 53-tool driver.',
    ],
    doodle: controlDoodle,
    start: { title: 'Open a session', language: 'sh', code: controlStart },
    examples: [
      { title: 'Use it from a shell', language: 'sh', code: cliCode },
      { title: 'Add it to an MCP client', language: 'json', code: mcpConfig },
      { title: 'Or send one JSON request per line', language: 'json', code: jsonLines },
    ],
    facts: [
      ['Transport', 'stdio MCP server, or one JSON request per line'],
      ['Catalogue', '53 native driver tools plus the three agent tools'],
      ['Groups', 'core by default; add network, state, debug, media or all'],
      ['Backends', 'Launch or attach to Chrome, Firefox or the suitecut CEF engine'],
      ['Sessions', 'Named sessions keep a browser alive; idle after ten minutes'],
      ['Artifacts', 'Screenshots as image content, PDFs as embedded resources'],
      ['Remote', 'Connection headers and TLS client certificates for remote browsers'],
    ],
    limits: [
      'Named sessions and exclusive output files need POSIX.',
      'Tool groups limit what is advertised; they are not a sandbox.',
      'Page content and WebMCP descriptions are untrusted data.',
    ],
    links: [
      ['Command-line sessions', `${repo}/native/client/README.md#native-command-line-sessions`],
      ['Agent skill', `${repo}/native/client/skills/browser-control/SKILL.md`],
      ['Agents and MCP', '/docs/agents'],
      ['Benchmarks', '/benchmarks'],
    ],
  },
  {
    slug: 'embed-kit',
    name: 'macOS embed kit',
    kind: 'Signed folder and embed.h',
    status: 'macOS ARM64',
    line: 'The engine inside your own Mac app, with tabs you can share with an agent',
    marker:
      'Each tab’s frames reach your app as IOSurfaces in GPU memory, so your window shows the page without screenshots.',
    intro: [
      'The embed kit is one folder of signed files: the engine’s app bundle, the suitecut-session service, licences and a kit.json manifest. An app copies it into its own bundle; there is no separate app to install. Napdock’s Browser extension is the first app that does.',
      'Each tab’s frames reach your app as IOSurfaces in GPU memory, so your window shows the page without screenshots.',
      'suitecut/embed.h, in the same client library, starts or joins a session, opens tabs, attaches views and passes on pointer, wheel, key, text and IME input. Your app answers dialogs, file choosers and permission prompts, and can grant an agent access to chosen tool groups. Each tab records whether the agent or the person is in control.',
    ],
    doodle: embedDoodle,
    start: { title: 'Package the kit', language: 'sh', code: kitStart },
    examples: [{ title: 'Open a session, a tab and a view', language: 'c', code: embedCode }],
    facts: [
      ['Contents', 'suitecut-browser.app, suitecut-session, licences and kit.json'],
      ['CEF', 'The official 156.0.30 build by default, which plays no H.264, AAC or HEVC'],
      ['Frames', 'NV12 IOSurfaces at the tab’s size times the session scale'],
      ['Control', 'Per tab: unclaimed, agent or person, with a generation counter'],
      ['Agents', 'Grants that name tool groups and can expire or be revoked'],
      ['Signing', 'Developer ID, hardened runtime, JIT only for the GPU and renderer helpers'],
      ['Size', '333 MB, mostly the CEF framework'],
    ],
    limits: [
      'The kit is not notarized.',
      'It is built for macOS 14 but was only run on macOS 27.',
      'Views with frames are macOS only.',
    ],
    links: [
      ['The embed kit', `${repo}/docs/embedding/embed-kit-macos.md`],
      ['Embedding API and session protocol', `${repo}/docs/embedding/api-design.md`],
      ['embed.h', `${repo}/native/client/include/suitecut/embed.h`],
      ['Embedding guide', '/docs/embedding'],
    ],
  },
]

export const componentBySlug = (slug: string) => {
  const found = components.find((item) => item.slug === slug)
  if (!found) throw new Error(`No component called ${slug}`)
  return found
}

/** The quick-start snippets, for pages that show one outside a component page. */
export const snippets = {
  engineStart,
  engineProtocol,
  recordCode,
  clientStart,
  cppCode,
  agentCode,
  cCode,
  controlStart,
  cliCode,
  mcpConfig,
  jsonLines,
  kitStart,
  embedCode,
} as const
