// Every package suitecut ships: its pitch, diagram, quick start, facts and the
// film that explains it. The package pages, the home page and the film pages
// (recorded by scripts/record-site-films.mjs) all read from here.
import { type Doodle } from '../lib/doodle.js'

export type Spec = readonly [string, string]

/** One scene of a package's explainer film, shown while its narration plays. */
export interface Scene {
  readonly kind: 'title' | 'doodle' | 'code' | 'list' | 'end'
  readonly heading: string
  readonly body?: string
  /** What the narrator says while the scene is on screen. */
  readonly narration: string
  readonly doodle?: Doodle
  readonly code?: string
  /** Text of a code line, or a diagram node id, that the recording highlights. */
  readonly focus?: string
  readonly items?: readonly Spec[]
}

export interface Package {
  readonly slug: string
  readonly name: string
  /** How to install it, as one shell line. */
  readonly install: string
  readonly kind: string
  readonly line: string
  /** The one sentence on the page that carries the highlighter. */
  readonly marker: string
  readonly intro: readonly string[]
  readonly doodle: Doodle
  /** Short examples, the first being the quickest way in. */
  readonly examples: readonly {
    readonly title: string
    readonly language: string
    readonly code: string
  }[]
  readonly facts: readonly Spec[]
  readonly limits: readonly string[]
  readonly links: readonly Spec[]
  readonly film: readonly Scene[]
  readonly status: string
}

const recordCode = `import { record, renderSuiteCut } from 'suitecut'

const run = await record('Tour', async ({ page, suitecut }) => {
  await page.goto('https://www.ebay.com/')
  await suitecut.narrate('A live store, driven from a script.')
  await suitecut.type(page.getByRole('combobox', { name: 'Search for anything' }), 'mechanical keyboard')
  await page.keyboard.press('Enter')
  await suitecut.highlight(page.locator('h1'))
})

await renderSuiteCut({
  manifestPath: run.manifestPath,
  outputPath: 'tour.mp4',
})`

const pipeline: Doodle = {
  width: 960,
  height: 420,
  seed: 7,
  title:
    'Your script drives Chromium inside your program. Frames, audio and a timeline come out of it, and become a finished video or a live stream.',
  nodes: [
    {
      id: 'script',
      x: 16,
      y: 160,
      w: 176,
      h: 104,
      label: 'your script',
      detail: 'clicks, typing,\nnarration',
      fill: 'yellow',
    },
    {
      id: 'engine',
      x: 246,
      y: 120,
      w: 220,
      h: 184,
      label: 'Chromium',
      detail: 'inside your\nprogram',
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
      id: 'timeline',
      x: 528,
      y: 300,
      w: 178,
      h: 84,
      label: 'timeline',
      detail: 'every step',
      shape: 'round',
      fill: 'blue',
    },
    { id: 'video', x: 776, y: 84, w: 168, h: 100, label: 'finished\nvideo', fill: 'red' },
    { id: 'live', x: 776, y: 248, w: 168, h: 100, label: 'live stream', detail: 'RTMP' },
  ],
  edges: [
    { from: 'script', to: 'engine' },
    { from: 'engine', to: 'frames' },
    { from: 'engine', to: 'audio' },
    { from: 'engine', to: 'timeline' },
    { from: 'frames', to: 'video' },
    { from: 'timeline', to: 'video', bend: -20 },
    { from: 'audio', to: 'live' },
  ],
}

const screenshotLoop: Doodle = {
  width: 960,
  height: 400,
  seed: 21,
  title:
    'Most recorders screenshot the page in a loop, which gives choppy video and drifting sound. suitecut takes each frame and the audio from the renderer, so the video is smooth and in sync.',
  nodes: [
    { id: 'page-a', x: 32, y: 36, w: 180, h: 124, label: 'page', shape: 'window' },
    {
      id: 'shot',
      x: 276,
      y: 58,
      w: 186,
      h: 84,
      label: 'screenshot',
      detail: 'again and again',
      shape: 'round',
    },
    { id: 'choppy', x: 140, y: 230, w: 220, h: 84, label: 'choppy video', detail: 'sound drifts' },
    { id: 'page-b', x: 528, y: 36, w: 180, h: 124, label: 'page', shape: 'window', fill: 'chrome' },
    {
      id: 'paint',
      x: 764,
      y: 58,
      w: 180,
      h: 84,
      label: 'renderer',
      detail: 'frame + audio',
      shape: 'round',
      fill: 'blue',
    },
    {
      id: 'smooth',
      x: 636,
      y: 230,
      w: 220,
      h: 84,
      label: 'smooth video',
      detail: 'in sync',
      fill: 'yellow',
    },
  ],
  edges: [
    { from: 'page-a', to: 'shot' },
    { from: 'shot', to: 'choppy' },
    { from: 'page-b', to: 'paint' },
    { from: 'paint', to: 'smooth' },
  ],
  crossed: ['choppy'],
  texts: [
    { x: 250, y: 372, text: 'most recorders', muted: true },
    { x: 746, y: 372, text: 'suitecut', muted: true },
  ],
}

const backends: Doodle = {
  width: 960,
  height: 410,
  seed: 33,
  title:
    'Your Node program uses browser-control, which drives the suitecut engine over a binary pipe, Chrome over a private CDP pipe, or Firefox over WebDriver BiDi. The suitecut engine also has a live viewer where a person can take control.',
  nodes: [
    { id: 'code', x: 16, y: 156, w: 176, h: 100, label: 'your Node\nprogram', fill: 'yellow' },
    {
      id: 'control',
      x: 252,
      y: 150,
      w: 222,
      h: 112,
      label: 'browser-control',
      detail: 'one API',
      shape: 'round',
      fill: 'chrome',
    },
    {
      id: 'engine',
      x: 548,
      y: 8,
      w: 214,
      h: 112,
      label: 'suitecut engine',
      detail: 'binary pipe',
      shape: 'window',
      fill: 'blue',
    },
    {
      id: 'chrome',
      x: 548,
      y: 148,
      w: 214,
      h: 112,
      label: 'Chrome, Edge',
      detail: 'CDP, private pipe',
      shape: 'window',
    },
    {
      id: 'firefox',
      x: 548,
      y: 288,
      w: 214,
      h: 112,
      label: 'Firefox',
      detail: 'WebDriver BiDi',
      shape: 'window',
    },
    {
      id: 'viewer',
      x: 812,
      y: 20,
      w: 136,
      h: 88,
      label: 'viewer',
      detail: 'take control',
      fill: 'red',
    },
  ],
  edges: [
    { from: 'code', to: 'control' },
    { from: 'control', to: 'engine' },
    { from: 'control', to: 'chrome' },
    { from: 'control', to: 'firefox' },
    { from: 'engine', to: 'viewer', both: true },
  ],
}

const sharedWindow: Doodle = {
  width: 960,
  height: 440,
  seed: 48,
  title:
    'You and an agent use one browser window. You click and type in it; the agent reaches it through the MCP server. There is no debugging port for anything else to attach to.',
  nodes: [
    { id: 'you', x: 28, y: 36, w: 170, h: 100, label: 'you', shape: 'circle', fill: 'yellow' },
    { id: 'agent', x: 28, y: 300, w: 170, h: 100, label: 'agent', shape: 'circle', fill: 'blue' },
    { id: 'mcp', x: 262, y: 314, w: 150, h: 72, label: 'MCP', detail: '19 tools', shape: 'round' },
    { id: 'port', x: 262, y: 176, w: 150, h: 72, label: 'debug port', shape: 'round' },
    {
      id: 'window',
      x: 486,
      y: 40,
      w: 446,
      h: 360,
      label: 'one window',
      detail: 'same tabs, cookies\nand screen',
      shape: 'window',
      fill: 'chrome',
    },
  ],
  edges: [
    { from: 'you', to: 'window', label: 'click, type', bend: -10 },
    { from: 'agent', to: 'mcp' },
    { from: 'mcp', to: 'window', label: 'navigate, snapshot,\nclick, type', bend: 16 },
  ],
  crossed: ['port'],
}

const nativeClient: Doodle = {
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
      detail: 'no Node, no CEF\nheaders',
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

const browserControlCode = `import { launchBrowser } from '@suitecut/browser-control'

const browser = await launchBrowser({
  backend: 'cdp',
  executablePath: '/path/to/chrome',
})
const page = await (await browser.newContext()).newPage()
await page.goto('https://www.ebay.com/')
await page.getByRole('combobox', { name: 'Search for anything' }).fill('mechanical keyboard')
await page.keyboard.press('Enter')
await browser.close()`

const sharedConfig = `{
  "mcpServers": {
    "shared-browser": {
      "command": "npx",
      "args": ["-y", "@suitecut/shared-browser"],
      "env": { "SUITECUT_SHARED_BROWSER": "/path/to/browser" }
    }
  }
}`

const sharedCode = `import { launchSharedBrowser } from '@suitecut/shared-browser'

const browser = await launchSharedBrowser({ executablePath })
const tab = await browser.activeTab()
await tab.goto('https://www.ebay.com/')
console.log(await tab.snapshot()) // text and controls with refs
await tab.fillRef('e3', 'mechanical keyboard')
await tab.press('Enter') // you can keep using the window`

const agentCode = `suitecut::AgentDriver driver(browser);
suitecut::SimpleAgent agent(driver);

auto seen = agent.call("observe", {{"format", "compact"}});
auto done = agent.call("act", {
    {"action", "fill"}, {"ref", "e1"}, {"value", "Ada"}});
auto ready = agent.call("wait", {
    {"text", "Saved"}, {"timeoutMs", 2000}});`

const cliCode = `alias sc='suitecut-native-control --session wiki'

sc --browser /usr/bin/chromium \\
  open https://www.ebay.com/
sc snapshot
sc fill e12 'mechanical keyboard'
sc press Enter

suitecut-native-control --browser /usr/bin/chromium \\
  --mcp --tools core,network`

export const packages: readonly Package[] = [
  {
    slug: 'suitecut',
    name: 'suitecut',
    install: 'npm install suitecut',
    kind: 'npm package',
    status: '2.1.0',
    line: 'Record, narrate and stream browser flows as finished videos',
    marker: 'Every frame comes from the renderer as it paints, with the page’s own sound.',
    intro: [
      'Write a product tour, a bug report or a test as a script. suitecut runs it in Chromium inside your own program and records it.',
      'Every frame comes from the renderer as it paints, with the page’s own sound.',
      'Narration, the cursor, highlights, zoom and captions are steps in the same script. suitecut keeps a timeline of all of them and renders a finished video, or streams the run live over RTMP.',
    ],
    doodle: pipeline,
    examples: [{ title: 'Record a tour and render it', language: 'ts', code: recordCode }],
    facts: [
      ['Capture', 'Any frame rate from 1 a second, straight from the renderer'],
      ['Output', 'MP4 at any rate from 1, including 29.97 and 59.94'],
      ['Audio', 'Page audio and narration on whole-sample boundaries, so nothing drifts'],
      ['Narration', 'Kokoro 82M runs locally, with word-by-word captions'],
      ['Effects', 'Cursor, click ripples, highlight, spotlight and zoom'],
      ['Streaming', 'Any number of RTMP or RTMPS publishers, with reconnect'],
      ['macOS', 'IOSurface capture, Metal colour conversion and VideoToolbox H.264'],
    ],
    limits: [
      'Needs the suitecut browser runtime, which ships separately from npm.',
      'Linux x64 and macOS on Apple silicon. Windows is not packaged.',
    ],
    links: [
      ['Guide', '/docs'],
      ['Streaming', '/docs/streaming'],
      ['Narration with Kokoro', '/docs/kokoro'],
      ['Benchmarks', '/benchmarks'],
    ],
    film: [
      {
        kind: 'title',
        heading: 'suitecut',
        body: 'Chromium you can record, stream and drive.',
        narration:
          'This is suitecut. It runs Chromium inside your own program, and takes video and sound straight from the page as it draws.',
      },
      {
        kind: 'doodle',
        heading: 'No screenshot loop',
        body: 'Each frame comes from the renderer, with the page’s sound.',
        narration:
          'Most screen recorders take screenshots in a loop, so motion stutters and the sound drifts. suitecut gets every painted frame, and every audio packet, from the renderer itself.',
        doodle: screenshotLoop,
        focus: 'smooth',
      },
      {
        kind: 'doodle',
        heading: 'Script in, video out',
        body: 'A timeline keeps every click and every line of narration.',
        narration:
          'You write the flow as a script. suitecut records it, keeps a timeline of every click and every line of narration, then renders a finished video, or streams it live.',
        doodle: pipeline,
        focus: 'timeline',
      },
      {
        kind: 'code',
        heading: 'Effects are steps',
        body: 'Narration, cursor, highlight and zoom live in the script.',
        narration:
          'Narration, the cursor, highlights, zoom and captions are all steps in the same script. Like the ones in this video.',
        code: recordCode,
        focus: 'suitecut.highlight',
      },
      {
        kind: 'list',
        heading: 'Four packages',
        narration:
          'It comes as four packages. suitecut for videos. browser-control to drive Chrome, Firefox or the suitecut engine. shared-browser for working beside an agent. And a C++ SDK, for when you do not want Node.',
        items: [
          ['suitecut', 'Record, narrate and stream browser flows'],
          ['@suitecut/browser-control', 'One API for Chromium, Firefox and the engine'],
          ['@suitecut/shared-browser', 'You and an agent in one window'],
          ['SuiteCut::Client', 'Browser control from C++ and MCP'],
        ],
      },
      {
        kind: 'end',
        heading: 'This video was made by suitecut',
        body: 'Recorded, narrated and rendered from one script.',
        code: 'npm install suitecut',
        narration: 'This video was recorded, narrated and rendered by suitecut.',
      },
    ],
  },
  {
    slug: 'browser-control',
    name: '@suitecut/browser-control',
    install: 'npm install @suitecut/browser-control',
    kind: 'npm package',
    status: '2.1.0',
    line: 'One API for Chromium, Firefox and the suitecut engine',
    marker: 'It talks to each browser over a private pipe, so there is no open debugging port.',
    intro: [
      'browser-control is a TypeScript SDK for three kinds of browser: the suitecut engine, an installed Chrome, Edge or Brave, and Firefox.',
      'It talks to each browser over a private pipe, so there is no open debugging port.',
      'If you have used Playwright it will read the same: contexts, pages, locators, routing, downloads and tracing, across 31 feature families. On the suitecut engine a live viewer lets a person take control from an agent and hand it back.',
    ],
    doodle: backends,
    examples: [{ title: 'Open a page in Chrome', language: 'ts', code: browserControlCode }],
    facts: [
      ['Backends', 'suitecut engine, Chrome, Edge or Brave over CDP, Firefox over WebDriver BiDi'],
      ['Transport', 'Private pipes; nothing listens on a port'],
      ['API', 'Contexts, pages, frames, locators, routing, HAR, storage and tracing'],
      ['Viewer', 'Watch live, take control, then resume the agent'],
      ['Remote', 'Attach to a running browser, with headers and TLS client certificates'],
      ['Size', 'About 320 KiB packed, no browser-automation dependencies'],
    ],
    limits: [
      'Does not download browsers. Point it at one you have installed, or the suitecut runtime.',
      'Feature support differs by backend; unsupported calls fail with a clear error.',
    ],
    links: [
      ['Browser control guide', '/docs/browser-control'],
      ['Agents', '/docs/agents'],
      ['API reference', '/docs/api'],
    ],
    film: [
      {
        kind: 'title',
        heading: '@suitecut/browser-control',
        body: 'One API for Chromium, Firefox and the suitecut engine.',
        narration:
          'This is browser-control. One TypeScript API for Chromium, Firefox, and the suitecut engine.',
      },
      {
        kind: 'doodle',
        heading: 'Three browsers, one API',
        body: 'Each over a private pipe. No open debugging port.',
        narration:
          'It talks to Chrome over a private pipe, to Firefox over WebDriver BiDi, and to the suitecut engine directly. Nothing listens on a debugging port.',
        doodle: backends,
        focus: 'control',
      },
      {
        kind: 'code',
        heading: 'Reads like Playwright',
        body: 'Pages, locators, routing, downloads and tracing.',
        narration:
          'If you have used Playwright, it reads the same. Pages, locators, routing, downloads and tracing. Thirty-one feature families in all.',
        code: browserControlCode,
        focus: 'getByRole',
      },
      {
        kind: 'doodle',
        heading: 'Take control',
        body: 'Watch an agent work, step in, hand it back.',
        narration:
          'On the suitecut engine there is a live viewer. A person can watch the agent, take control, and hand it back.',
        doodle: backends,
        focus: 'viewer',
      },
      {
        kind: 'end',
        heading: 'One API, three browsers',
        body: 'Recorded and narrated by suitecut.',
        code: 'npm install @suitecut/browser-control',
        narration: 'Install it from npm. This video was recorded and narrated by suitecut.',
      },
    ],
  },
  {
    slug: 'shared-browser',
    name: '@suitecut/shared-browser',
    install: 'npx -y @suitecut/shared-browser --executable /path/to/suitecut-browser',
    kind: 'npm package · preview',
    status: '0.1.0 preview',
    line: 'You and an AI agent in one browser window',
    marker: 'The agent works in the same tabs you can see, click and type in.',
    intro: [
      'Most agent browsers run where you cannot see them. shared-browser opens one real window with suitecut’s own tabs and toolbar.',
      'The agent works in the same tabs you can see, click and type in.',
      'Connect any MCP client, or drive it from TypeScript. Commands go over the browser’s own input and DevTools runs inside the process, so there is no port for anything else to find. Permission prompts and dialogs wait for you, or for the agent if you let it answer.',
    ],
    doodle: sharedWindow,
    examples: [
      { title: 'Add it to your MCP client', language: 'json', code: sharedConfig },
      { title: 'Or drive it from TypeScript', language: 'ts', code: sharedCode },
    ],
    facts: [
      ['Window', 'suitecut’s tab strip and toolbar; every tab a full Chromium page'],
      ['MCP', '19 tools over stdio: navigate, snapshot, click, type, tabs, dialogs'],
      ['SDK', 'launchSharedBrowser() with tabs, refs, input, events and screenshots'],
      ['Input', 'Trusted events, so pages see a person, not a script'],
      ['Detection', 'sannysoft, CreepJS, BrowserScan, incolumitas and BotD saw a normal browser'],
      ['Prompts', 'Allow and Block in the toolbar; dialogs wait for an answer'],
    ],
    limits: [
      'Preview: macOS on Apple silicon only, and the API may change before 1.0.',
      'The window is on screen; there is no headless mode.',
      'Not tested against commercial bot protection such as Cloudflare or DataDome.',
    ],
    links: [
      ['Package README', 'https://github.com/skyaara/suitecut/tree/main/packages/shared-browser'],
      ['Control protocol', 'https://github.com/skyaara/suitecut/blob/main/docs/shared-browser.md'],
    ],
    film: [
      {
        kind: 'title',
        heading: '@suitecut/shared-browser',
        body: 'You and an AI agent in one browser window.',
        narration:
          'This is shared-browser. One browser window that you and an AI agent use together.',
      },
      {
        kind: 'doodle',
        heading: 'Same window, same tabs',
        body: 'You click and type. The agent works through MCP.',
        narration:
          'Most agent browsers run somewhere you cannot see. shared-browser opens a real window. The agent works in its tabs, and you can click, type and open tabs in the same window.',
        doodle: sharedWindow,
        focus: 'window',
      },
      {
        kind: 'code',
        heading: 'One entry in your MCP config',
        body: 'Nineteen tools, from navigate to permission prompts.',
        narration:
          'Add it to any MCP client with one entry. Nineteen tools: navigate, snapshot, click, type, tabs, dialogs and permission prompts.',
        code: sharedConfig,
        focus: '@suitecut/shared-browser',
      },
      {
        kind: 'doodle',
        heading: 'Nothing to attach to',
        body: 'No debugging port. Input arrives as trusted events.',
        narration:
          'Commands go over the browser’s own input, and DevTools runs inside the process, so there is no port for anything else to find. Five bot detectors saw a normal browser.',
        doodle: sharedWindow,
        focus: 'port',
      },
      {
        kind: 'end',
        heading: 'Work beside your agent',
        body: 'Preview for macOS on Apple silicon. Recorded by suitecut.',
        code: 'npx -y @suitecut/shared-browser',
        narration:
          'It is a preview for macOS on Apple silicon. This video was recorded and narrated by suitecut.',
      },
    ],
  },
  {
    slug: 'native-sdk',
    name: 'SuiteCut::Client',
    install: 'find_package(SuiteCutClient CONFIG REQUIRED)',
    kind: 'C++20 library and C ABI',
    status: 'archives per platform',
    line: 'Browser control from C++, any language and MCP, without Node',
    marker: 'Three agent tools: observe, act and wait.',
    intro: [
      'SuiteCut::Client is a C++20 library with a versioned C ABI. It drives the suitecut engine, an installed Chrome or Chromium, or Firefox, with no Node.js, no CEF headers and no automation library.',
      'Three agent tools: observe, act and wait.',
      'Its suitecut-native-control CLI keeps a browser session alive between shell commands, and runs as an MCP server with 53 driver tools for agents that want more than three.',
    ],
    doodle: nativeClient,
    examples: [
      { title: 'Give an agent three tools', language: 'cpp', code: agentCode },
      { title: 'Or use the CLI', language: 'sh', code: cliCode },
    ],
    facts: [
      ['Library', 'C++20 headers and a stable, versioned C ABI, static or shared'],
      ['Agent tools', 'observe, act and wait; any number of actions in one act'],
      ['Refs', 'Short element refs, replaced on every observation; stale refs are rejected'],
      ['Safety', 'Mutations are never retried; password values are redacted'],
      ['CLI', 'Named sessions that stay alive between commands'],
      ['MCP', '53 driver tools in groups: core, network, state, debug and media'],
    ],
    limits: [
      'Linux and macOS on Apple silicon are qualified separately. Windows is not packaged.',
      'Safari and WebKit are not supported.',
    ],
    links: [
      [
        'Native SDK guide',
        'https://github.com/skyaara/suitecut/blob/main/docs/native-browser-sdk.md',
      ],
      ['Agents', '/docs/agents'],
      ['Performance', '/docs/performance'],
    ],
    film: [
      {
        kind: 'title',
        heading: 'SuiteCut::Client',
        body: 'Browser control from C++, any language and MCP.',
        narration:
          'This is SuiteCut Client. Browser control from C++, from any language through a C interface, and from MCP.',
      },
      {
        kind: 'doodle',
        heading: 'No Node required',
        body: 'One library for the engine, Chrome and Firefox.',
        narration:
          'It is one native library. It drives the suitecut engine, Chrome or Firefox, with no Node, no CEF headers, and no automation library.',
        doodle: nativeClient,
        focus: 'client',
      },
      {
        kind: 'code',
        heading: 'observe, act, wait',
        body: 'Three tools an agent cannot misuse.',
        narration:
          'For agents there are three tools. Observe, act and wait. Refs are short and replaced on every look, and a click is never retried by accident.',
        code: agentCode,
        focus: 'agent.call("act"',
      },
      {
        kind: 'code',
        heading: 'A CLI that remembers',
        body: 'Sessions survive between commands. Add --mcp for agents.',
        narration:
          'The command line tool keeps a browser session alive between commands, and runs as an MCP server with fifty-three tools.',
        code: cliCode,
        focus: '--mcp',
      },
      {
        kind: 'end',
        heading: 'Browser control without Node',
        body: 'Linux and macOS. Recorded by suitecut.',
        code: 'find_package(SuiteCutClient)',
        narration:
          'It ships for Linux and macOS. This video was recorded and narrated by suitecut.',
      },
    ],
  },
]

export const packageBySlug = (slug: string) => {
  const found = packages.find((item) => item.slug === slug)
  if (!found) throw new Error(`No package called ${slug}`)
  return found
}
