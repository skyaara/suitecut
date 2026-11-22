// The hand-drawn diagrams on the home page, About and the docs, each with the
// numbered explanation printed under it. Steps name the nodes they explain; the
// first node of each step gets the step's number drawn on its corner.
import { type Doodle } from '../lib/doodle.js'

import { packageBySlug } from './packages.js'

export interface Step {
  readonly nodes: readonly string[]
  readonly title: string
  readonly text: string
}

export interface Explained {
  readonly doodle: Doodle
  readonly steps: readonly Step[]
}

const engine: Explained = {
  doodle: {
    width: 960,
    height: 470,
    seed: 41,
    flow: true,
    title:
      'A TypeScript SDK, a C++ SDK, command-line sessions and an MCP server all drive the suitecut engine. Out of it come MP4 recordings, RTMP streams, a live viewer and traces.',
    nodes: [
      {
        id: 'ts',
        x: 16,
        y: 34,
        w: 214,
        h: 80,
        label: 'TypeScript SDK',
        detail: 'record, render, stream',
      },
      { id: 'cpp', x: 16, y: 136, w: 214, h: 80, label: 'C++ SDK', detail: 'no Node.js' },
      {
        id: 'cli',
        x: 16,
        y: 238,
        w: 214,
        h: 80,
        label: 'CLI sessions',
        detail: 'one command a call',
      },
      { id: 'mcp', x: 16, y: 340, w: 214, h: 80, label: 'MCP server', detail: 'for your agent' },
      {
        id: 'engine',
        x: 330,
        y: 112,
        w: 300,
        h: 236,
        label: 'suitecut engine',
        detail: 'Chromium, CEF 156\npaint + audio callbacks',
        shape: 'window',
        fill: 'red',
      },
      {
        id: 'mp4',
        x: 730,
        y: 34,
        w: 214,
        h: 80,
        label: 'MP4 recording',
        detail: 'at the rate you choose',
        fill: 'yellow',
      },
      { id: 'rtmp', x: 730, y: 136, w: 214, h: 80, label: 'live stream', detail: 'RTMP or RTMPS' },
      {
        id: 'viewer',
        x: 730,
        y: 238,
        w: 214,
        h: 80,
        label: 'live viewer',
        detail: 'a person takes over',
        fill: 'blue',
      },
      {
        id: 'trace',
        x: 730,
        y: 340,
        w: 214,
        h: 80,
        label: 'traces and HAR',
        detail: 'what happened',
      },
    ],
    edges: [
      { from: 'ts', to: 'engine', carries: 'command' },
      { from: 'cpp', to: 'engine', carries: 'command' },
      { from: 'cli', to: 'engine', carries: 'command' },
      { from: 'mcp', to: 'engine', carries: 'command' },
      { from: 'engine', to: 'mp4' },
      { from: 'engine', to: 'rtmp' },
      { from: 'engine', to: 'viewer', both: true },
      { from: 'engine', to: 'trace' },
    ],
    texts: [
      { x: 280, y: 454, text: 'one protocol in', muted: true, anchor: 'middle' },
      { x: 680, y: 454, text: 'one clock out', muted: true, anchor: 'middle' },
    ],
  },
  steps: [
    {
      nodes: ['ts', 'cpp', 'cli', 'mcp'],
      title: 'Drive it from anything',
      text: 'TypeScript, C++, a shell or an MCP client. They all speak the same small protocol to one native driver.',
    },
    {
      nodes: ['engine'],
      title: 'Chromium runs inside your program',
      text: 'The renderer hands over every painted frame and every audio packet. There is no screenshot loop, extension or open debugging port.',
    },
    {
      nodes: ['mp4', 'rtmp'],
      title: 'Record or stream on one clock',
      text: 'Frames and sound are timestamped together. The recorder keeps the newest real frame for each output frame and never invents motion; the publisher sends the same pipeline live.',
    },
    {
      nodes: ['viewer', 'trace'],
      title: 'Hand over, and look back',
      text: 'A person can watch and take control in the live viewer, and traces and HAR files keep a record of the run.',
    },
  ],
}

const together: Explained = {
  doodle: {
    width: 960,
    height: 390,
    seed: 52,
    flow: true,
    title:
      'A script, a model and a person all reach the same native driver, so they share one session with the same page, cookies and storage.',
    nodes: [
      {
        id: 'script',
        x: 16,
        y: 20,
        w: 214,
        h: 84,
        label: 'a script',
        detail: 'TypeScript or C++',
        fill: 'yellow',
      },
      { id: 'model', x: 16, y: 152, w: 214, h: 84, label: 'a model', detail: 'through MCP' },
      {
        id: 'person',
        x: 16,
        y: 284,
        w: 214,
        h: 84,
        label: 'a person',
        detail: 'in the live viewer',
        fill: 'blue',
      },
      {
        id: 'driver',
        x: 336,
        y: 140,
        w: 232,
        h: 108,
        label: 'native driver',
        detail: 'one for all three',
        shape: 'round',
        fill: 'chrome',
      },
      {
        id: 'session',
        x: 660,
        y: 84,
        w: 284,
        h: 220,
        label: 'one session',
        detail: 'same page, cookies\nand storage',
        shape: 'window',
        fill: 'red',
      },
    ],
    edges: [
      { from: 'script', to: 'driver', carries: 'command' },
      { from: 'model', to: 'driver', carries: 'command' },
      { from: 'person', to: 'driver', carries: 'command' },
      { from: 'driver', to: 'session', both: true, carries: 'command' },
    ],
  },
  steps: [
    {
      nodes: ['script', 'model', 'person'],
      title: 'Three ways in',
      text: 'A script you wrote, a model working through the MCP server, and a person in the live viewer.',
    },
    {
      nodes: ['driver'],
      title: 'One driver',
      text: 'Browser control, the agent tools and the MCP server all sit on the same native driver.',
    },
    {
      nodes: ['session'],
      title: 'One session',
      text: 'So all three work on the same page with the same cookies and storage, and can take turns.',
    },
  ],
}

const recordAndRender: Explained = {
  doodle: {
    width: 960,
    height: 410,
    seed: 63,
    flow: true,
    title:
      'Your script runs in the native engine, which saves clips and a manifest. The renderer composes them with narration into one video.',
    nodes: [
      {
        id: 'script',
        x: 16,
        y: 44,
        w: 214,
        h: 104,
        label: 'your script',
        detail: 'actions, narration,\nhighlights, zoom',
        fill: 'yellow',
      },
      {
        id: 'record',
        x: 330,
        y: 20,
        w: 260,
        h: 156,
        label: 'record()',
        detail: 'runs it natively,\nkeeps a timeline',
        shape: 'window',
        fill: 'chrome',
      },
      {
        id: 'clips',
        x: 702,
        y: 44,
        w: 242,
        h: 104,
        label: 'clips + manifest',
        detail: 'every interval',
      },
      {
        id: 'voice',
        x: 16,
        y: 270,
        w: 214,
        h: 104,
        label: 'narration',
        detail: 'Kokoro or a\nsystem voice',
        shape: 'round',
        fill: 'blue',
      },
      {
        id: 'render',
        x: 340,
        y: 270,
        w: 240,
        h: 104,
        label: 'renderSuiteCut()',
        detail: 'composes and mixes',
        shape: 'round',
      },
      {
        id: 'video',
        x: 702,
        y: 270,
        w: 242,
        h: 104,
        label: 'one video',
        detail: 'MP4, at any frame rate',
        fill: 'red',
      },
    ],
    edges: [
      { from: 'script', to: 'record', carries: 'command' },
      { from: 'record', to: 'clips' },
      { from: 'clips', to: 'render' },
      { from: 'voice', to: 'render', carries: 'sound' },
      { from: 'render', to: 'video' },
    ],
  },
  steps: [
    {
      nodes: ['script'],
      title: 'Write the flow',
      text: 'Page actions, narration, highlights and zoom are steps in one script.',
    },
    {
      nodes: ['record'],
      title: 'Record it',
      text: 'record() runs the script in the native engine and keeps a timeline of every step.',
    },
    {
      nodes: ['clips'],
      title: 'Clips and a manifest',
      text: 'When the run ends, the recorder closes its browser, saves the source clips and writes a manifest.',
    },
    {
      nodes: ['voice', 'render'],
      title: 'Render',
      text: 'The renderer composes the intervals you keep and mixes page audio with the narration.',
    },
    {
      nodes: ['video'],
      title: 'One finished video',
      text: 'At the size, frame rate and quality you choose.',
    },
  ],
}

const streaming: Explained = {
  doodle: {
    width: 960,
    height: 380,
    seed: 74,
    flow: true,
    title:
      'A page in the native browser gives frames and audio to an encoder, which publishes to an RTMP or RTMPS server and reconnects if the connection drops.',
    nodes: [
      {
        id: 'page',
        x: 16,
        y: 96,
        w: 230,
        h: 184,
        label: 'your page',
        detail: 'in the native browser',
        shape: 'window',
        fill: 'chrome',
      },
      {
        id: 'frames',
        x: 318,
        y: 40,
        w: 196,
        h: 92,
        label: 'frames',
        detail: 'from paint',
        shape: 'round',
        fill: 'blue',
      },
      {
        id: 'audio',
        x: 318,
        y: 244,
        w: 196,
        h: 92,
        label: 'audio',
        detail: '48 kHz PCM',
        shape: 'round',
        fill: 'blue',
      },
      { id: 'encoder', x: 586, y: 134, w: 176, h: 110, label: 'encoder', detail: 'fixed bitrate' },
      { id: 'server', x: 818, y: 134, w: 126, h: 110, label: 'RTMP\nserver', fill: 'red' },
    ],
    edges: [
      { from: 'page', to: 'frames' },
      { from: 'page', to: 'audio', carries: 'sound' },
      { from: 'frames', to: 'encoder' },
      { from: 'audio', to: 'encoder', carries: 'sound' },
      { from: 'encoder', to: 'server' },
    ],
    texts: [
      { x: 881, y: 272, text: 'reconnects', size: 18, anchor: 'middle' },
      { x: 881, y: 296, text: 'if it drops', muted: true, size: 18, anchor: 'middle' },
    ],
  },
  steps: [
    {
      nodes: ['page'],
      title: 'Pick a page',
      text: 'Any page in the native browser. selectSource() switches to another page in the same helper.',
    },
    {
      nodes: ['frames', 'audio'],
      title: 'Capture',
      text: 'Frames come from the paint callback, and audio as 48 kHz stereo PCM. Audio is off unless you turn it on.',
    },
    {
      nodes: ['encoder'],
      title: 'Encode on one clock',
      text: 'Output size, frame rate and a fixed bitrate are set up front; output rates can be fractional, such as 29.97.',
    },
    {
      nodes: ['server'],
      title: 'Publish',
      text: 'To an RTMP or RTMPS URL, with an optional reconnect policy for when the connection drops.',
    },
  ],
}

const backends: Explained = {
  doodle: packageBySlug('browser-control').doodle,
  steps: [
    {
      nodes: ['code', 'control'],
      title: 'One API',
      text: 'Your Node.js program uses one Playwright-style API, whatever browser is on the other end.',
    },
    {
      nodes: ['engine'],
      title: 'cef, the default',
      text: "suitecut's embedded engine over a binary pipe, with native capture.",
    },
    {
      nodes: ['chrome'],
      title: 'cdp',
      text: 'An installed Chrome, Chromium, Edge or Brave over a private CDP pipe.',
    },
    {
      nodes: ['firefox'],
      title: 'bidi',
      text: 'An installed Firefox over WebDriver BiDi.',
    },
    {
      nodes: ['viewer'],
      title: 'Live viewer',
      text: 'On the suitecut engine, a person can watch the run, take control and hand it back.',
    },
  ],
}

const simpleAgent: Explained = {
  doodle: {
    width: 960,
    height: 400,
    seed: 85,
    title:
      'Your model uses three tools, observe, act and wait, against the page. Observe returns short refs; act and wait use them, all within one deadline.',
    nodes: [
      {
        id: 'model',
        x: 16,
        y: 146,
        w: 200,
        h: 108,
        label: 'your model',
        detail: 'decides what next',
        fill: 'yellow',
      },
      {
        id: 'observe',
        x: 318,
        y: 22,
        w: 214,
        h: 88,
        label: 'observe',
        detail: 'controls + refs',
        shape: 'round',
        fill: 'blue',
      },
      {
        id: 'act',
        x: 318,
        y: 156,
        w: 214,
        h: 88,
        label: 'act',
        detail: 'one or many steps',
        shape: 'round',
      },
      {
        id: 'wait',
        x: 318,
        y: 290,
        w: 214,
        h: 88,
        label: 'wait',
        detail: 'for a visible change',
        shape: 'round',
      },
      {
        id: 'page',
        x: 640,
        y: 90,
        w: 304,
        h: 220,
        label: 'the page',
        detail: 'new refs replace\nthe old ones',
        shape: 'window',
        fill: 'chrome',
      },
    ],
    edges: [
      { from: 'observe', to: 'model' },
      { from: 'model', to: 'act' },
      { from: 'model', to: 'wait' },
      { from: 'page', to: 'observe' },
      { from: 'act', to: 'page' },
      { from: 'wait', to: 'page' },
    ],
    texts: [
      { x: 425, y: 392, text: 'one deadline, 5 s by default', muted: true, anchor: 'middle' },
    ],
  },
  steps: [
    {
      nodes: ['observe'],
      title: 'observe',
      text: 'Every control and its text by default, or fewer with optional limits that mark the result truncated, as short refs. Each look replaces the previous refs, so stale ones are rejected.',
    },
    {
      nodes: ['act'],
      title: 'act',
      text: 'Runs a list of known actions in order and stops at the first failure. Mutations are never retried.',
    },
    {
      nodes: ['wait'],
      title: 'wait',
      text: 'Waits for a visible condition. delivered says the browser acknowledged an action and effect checks the condition; neither proves the task succeeded.',
    },
    {
      nodes: ['model'],
      title: 'Your model decides',
      text: 'No model or planner is built in; your host brings the agent.',
    },
  ],
}

const narrationTimeline: Explained = {
  doodle: {
    width: 960,
    height: 280,
    seed: 96,
    title:
      'The recording runs, then pauses while the narration is synthesized, then records the caption for the length of the audio, then carries on. The pause never appears in the video.',
    nodes: [
      {
        id: 'before',
        x: 16,
        y: 60,
        w: 180,
        h: 100,
        label: 'recording',
        detail: 'page plays',
        fill: 'blue',
      },
      {
        id: 'synth',
        x: 262,
        y: 52,
        w: 214,
        h: 116,
        label: 'clock paused',
        detail: 'voice is made',
        shape: 'round',
        fill: 'red',
      },
      {
        id: 'caption',
        x: 542,
        y: 60,
        w: 200,
        h: 100,
        label: 'narration',
        detail: 'its real length',
        fill: 'yellow',
      },
      {
        id: 'after',
        x: 806,
        y: 60,
        w: 138,
        h: 100,
        label: 'recording',
        detail: 'carries on',
        fill: 'blue',
      },
    ],
    edges: [
      { from: 'before', to: 'synth' },
      { from: 'synth', to: 'caption' },
      { from: 'caption', to: 'after' },
    ],
    texts: [
      { x: 369, y: 216, text: 'not in the video', size: 20, anchor: 'middle' },
      {
        x: 369,
        y: 246,
        text: 'however long the model takes',
        muted: true,
        size: 18,
        anchor: 'middle',
      },
    ],
  },
  steps: [
    {
      nodes: ['before'],
      title: 'One at a time',
      text: 'Narrations run in order, never on top of each other.',
    },
    {
      nodes: ['synth'],
      title: 'The clock stops',
      text: 'suitecut ignores frames while the voice is synthesized, so model startup never shows. After five seconds it prints a notice, so a cold load is not mistaken for a stall.',
    },
    {
      nodes: ['caption'],
      title: 'Narration takes its real length',
      text: 'The caption is recorded for the measured duration of the audio, then the page carries on.',
    },
  ],
}

const sharedBrowser: Explained = {
  doodle: packageBySlug('shared-browser').doodle,
  steps: [
    {
      nodes: ['you'],
      title: 'You use the window',
      text: "suitecut's own tab strip and toolbar around real Chromium tabs. You click, type and open tabs as usual.",
    },
    {
      nodes: ['agent', 'mcp'],
      title: 'The agent uses the same tabs',
      text: 'Through the MCP server or the TypeScript SDK: navigate, snapshot, click, type, tabs, dialogs and permission prompts.',
    },
    {
      nodes: ['port'],
      title: 'No port to find',
      text: 'Commands go over the browser’s own input and DevTools runs inside the process, so there is no remote debugging port.',
    },
    {
      nodes: ['window'],
      title: 'One window, one profile',
      text: 'Same tabs, cookies and screen for both of you. Prompts and dialogs wait for an answer from either.',
    },
  ],
}

const nativeClient: Explained = {
  doodle: packageBySlug('native-sdk').doodle,
  steps: [
    {
      nodes: ['cpp', 'abi'],
      title: 'From C++ or any language',
      text: 'Link the C++ library, or call its stable C ABI from any language that can load one.',
    },
    {
      nodes: ['agent', 'cli'],
      title: 'Or from a shell or an MCP client',
      text: 'suitecut-native-control runs one command per call, keeps named sessions, and serves MCP over stdio.',
    },
    {
      nodes: ['client'],
      title: 'One native library',
      text: 'No Node.js, no CEF headers and no automation library to install alongside it.',
    },
    {
      nodes: ['engine', 'chrome', 'firefox'],
      title: 'Three browsers',
      text: 'The suitecut engine, an installed Chrome over a private CDP pipe, or Firefox over WebDriver BiDi.',
    },
  ],
}

const windowFrames: Explained = {
  doodle: {
    width: 960,
    height: 470,
    seed: 63,
    flow: true,
    title:
      "Chromium's GPU process hands each frame of the shared browser's window to the browser process as a shared surface. A hook on the window's layer sees it arrive, keeps one per output frame, crops and converts it on the GPU and sends it to the hardware encoder. The page's sound joins on the same clock and is also played to the speakers.",
    nodes: [
      { id: 'gpu', x: 16, y: 30, w: 200, h: 84, label: 'GPU process', detail: 'draws the window' },
      {
        id: 'surface',
        x: 256,
        y: 30,
        w: 200,
        h: 84,
        label: 'IOSurface',
        detail: 'shared GPU memory',
      },
      {
        id: 'layer',
        x: 496,
        y: 30,
        w: 200,
        h: 84,
        label: "window's layer",
        detail: 'the hook sees it land',
        fill: 'red',
      },
      {
        id: 'gate',
        x: 744,
        y: 30,
        w: 200,
        h: 84,
        label: 'rate gate',
        detail: 'skips extra paints',
        shape: 'round',
      },
      {
        id: 'crop',
        x: 744,
        y: 196,
        w: 200,
        h: 84,
        label: 'crop + NV12',
        detail: 'on the GPU',
        shape: 'round',
        fill: 'chrome',
      },
      {
        id: 'encoder',
        x: 496,
        y: 196,
        w: 200,
        h: 84,
        label: 'VideoToolbox',
        detail: 'hardware H.264',
        fill: 'yellow',
      },
      { id: 'mp4', x: 256, y: 196, w: 200, h: 84, label: 'MP4', detail: 'constant frame rate' },
      {
        id: 'audio',
        x: 496,
        y: 362,
        w: 200,
        h: 84,
        label: 'page sound',
        detail: '48 kHz from CEF',
        fill: 'blue',
      },
      {
        id: 'speakers',
        x: 744,
        y: 362,
        w: 200,
        h: 84,
        label: 'speakers',
        detail: 'played back',
        shape: 'round',
      },
    ],
    edges: [
      { from: 'gpu', to: 'surface' },
      { from: 'surface', to: 'layer' },
      { from: 'layer', to: 'gate' },
      { from: 'gate', to: 'crop' },
      { from: 'crop', to: 'encoder' },
      { from: 'encoder', to: 'mp4' },
      { from: 'audio', to: 'encoder', carries: 'sound' },
      { from: 'audio', to: 'speakers', dashed: true, carries: 'sound' },
    ],
  },
  steps: [
    {
      nodes: ['gpu', 'surface'],
      title: 'Chromium hands the frame over',
      text: "Normally the window shows a layer only macOS's window server can read. The shared browser turns that off, so each finished frame comes to the browser process as a surface in GPU memory.",
    },
    {
      nodes: ['layer'],
      title: 'A hook sees it land',
      text: "The window's layer gets a small subclass, the way macOS key-value observing works, so it reports each new surface the moment it arrives. No polling, no screen recording.",
    },
    {
      nodes: ['gate'],
      title: 'Only what the video needs',
      text: 'Paints come at up to the display rate. At 30 fps the recorder keeps one per output frame and skips the rest before any copying.',
    },
    {
      nodes: ['crop', 'encoder', 'mp4'],
      title: 'Cropped and encoded on the GPU',
      text: 'The kept frame is cropped to the tab in front, converted to NV12 and encoded in hardware. When the page is still, the last frame is held, which costs almost nothing.',
    },
    {
      nodes: ['audio', 'speakers'],
      title: 'Sound on the same clock',
      text: 'With audio on, CEF hands over the page’s sound with timestamps moved onto the frame clock. CEF mutes what it captures, so the browser plays it back to you.',
    },
  ],
}

export const diagrams = {
  windowFrames,
  engine,
  together,
  recordAndRender,
  streaming,
  backends,
  simpleAgent,
  narrationTimeline,
  sharedBrowser,
  nativeClient,
} as const
