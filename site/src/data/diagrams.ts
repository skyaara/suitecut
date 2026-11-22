// The hand-drawn diagrams on the home page, About and the docs, each with the
// numbered explanation printed under it. Steps name the nodes they explain; the
// first node of each step gets the step's number drawn on its corner.
import { type Doodle } from '../lib/doodle.js'

import { componentBySlug } from './components.js'

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
      'A C++ SDK, its C ABI, command-line and MCP sessions and the macOS embed kit all drive the suitecut engine. Out of it come recordings, live streams, views in your own app, and screenshots, PDFs and HAR files.',
    nodes: [
      { id: 'cpp', x: 16, y: 34, w: 214, h: 80, label: 'C++ SDK', detail: 'SuiteCut::Client' },
      { id: 'abi', x: 16, y: 136, w: 214, h: 80, label: 'C ABI', detail: 'any language' },
      {
        id: 'cli',
        x: 16,
        y: 238,
        w: 214,
        h: 80,
        label: 'CLI and MCP',
        detail: 'native-control',
      },
      { id: 'kit', x: 16, y: 340, w: 214, h: 80, label: 'embed kit', detail: 'your Mac app' },
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
        id: 'recording',
        x: 730,
        y: 34,
        w: 214,
        h: 80,
        label: 'recording',
        detail: 'at the rate you choose',
        fill: 'yellow',
      },
      {
        id: 'rtmp',
        x: 730,
        y: 136,
        w: 214,
        h: 80,
        label: 'live stream',
        detail: 'RTMP, RTMPS or TCP',
      },
      {
        id: 'view',
        x: 730,
        y: 238,
        w: 214,
        h: 80,
        label: 'app view',
        detail: 'IOSurface frames',
        fill: 'blue',
      },
      {
        id: 'artifacts',
        x: 730,
        y: 340,
        w: 214,
        h: 80,
        label: 'artifacts',
        detail: 'screenshots, PDF, HAR',
      },
    ],
    edges: [
      { from: 'cpp', to: 'engine', carries: 'command' },
      { from: 'abi', to: 'engine', carries: 'command' },
      { from: 'cli', to: 'engine', carries: 'command' },
      { from: 'kit', to: 'engine', carries: 'command' },
      { from: 'engine', to: 'recording' },
      { from: 'engine', to: 'rtmp' },
      { from: 'engine', to: 'view', both: true },
      { from: 'engine', to: 'artifacts' },
    ],
    texts: [
      { x: 280, y: 454, text: 'one protocol in', muted: true, anchor: 'middle' },
      { x: 680, y: 454, text: 'one clock out', muted: true, anchor: 'middle' },
    ],
  },
  steps: [
    {
      nodes: ['cpp', 'abi', 'cli', 'kit'],
      title: 'Drive it from native code',
      text: 'C++, any language through the C ABI, a shell, an MCP client or a Mac app. They all use the same client library and its native driver.',
    },
    {
      nodes: ['engine'],
      title: 'Chromium runs inside the engine',
      text: 'The renderer hands over every painted frame and every audio packet. There is no screenshot loop, extension or open debugging port.',
    },
    {
      nodes: ['recording', 'rtmp'],
      title: 'Record or stream on one clock',
      text: 'Frames and sound are timestamped together. The recorder keeps the newest real frame for each output frame and never invents motion; the publisher sends the same pipeline live.',
    },
    {
      nodes: ['view', 'artifacts'],
      title: 'Show it, and keep a record',
      text: 'On macOS a tab can be drawn in your own window from IOSurface frames. Screenshots, PDFs and HAR files keep a record of the run.',
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
      'A person in your app and an agent with a grant both reach the same session service, so they share tabs with the same page, cookies and storage, and each tab says who is in control.',
    nodes: [
      {
        id: 'person',
        x: 16,
        y: 40,
        w: 214,
        h: 92,
        label: 'a person',
        detail: 'in your app',
        fill: 'yellow',
      },
      {
        id: 'agent',
        x: 16,
        y: 240,
        w: 214,
        h: 92,
        label: 'an agent',
        detail: 'with a grant',
        fill: 'blue',
      },
      {
        id: 'session',
        x: 336,
        y: 140,
        w: 232,
        h: 108,
        label: 'session service',
        detail: 'one for both',
        shape: 'round',
        fill: 'chrome',
      },
      {
        id: 'tabs',
        x: 660,
        y: 84,
        w: 284,
        h: 220,
        label: 'shared tabs',
        detail: 'same page, cookies\nand storage',
        shape: 'window',
        fill: 'red',
      },
    ],
    edges: [
      { from: 'person', to: 'session', carries: 'command' },
      { from: 'agent', to: 'session', carries: 'command' },
      { from: 'session', to: 'tabs', both: true, carries: 'command' },
    ],
  },
  steps: [
    {
      nodes: ['person', 'agent'],
      title: 'Two ways in',
      text: 'Your app opens the session as its host and shows the tab. It can grant an agent access to chosen tool groups.',
    },
    {
      nodes: ['session'],
      title: 'One service',
      text: 'suitecut-session owns the engine, keeps each owner’s tabs and serves a private socket.',
    },
    {
      nodes: ['tabs'],
      title: 'Taking turns',
      text: 'Each tab is unclaimed, held by the agent or held by the person. Every change of hands raises a generation counter, and an action against an old generation is refused.',
    },
  ],
}

const recording: Explained = {
  doodle: {
    width: 960,
    height: 380,
    seed: 63,
    flow: true,
    title:
      'A page in the engine gives frames and audio to the recorder, which resamples them onto one clock and encodes a file with FFmpeg, or with the hardware H.264 encoder on macOS.',
    nodes: [
      {
        id: 'page',
        x: 16,
        y: 96,
        w: 230,
        h: 184,
        label: 'your page',
        detail: 'in the engine',
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
      {
        id: 'clock',
        x: 586,
        y: 134,
        w: 176,
        h: 110,
        label: 'one clock',
        detail: 'newest real frame',
      },
      { id: 'file', x: 818, y: 134, w: 126, h: 110, label: 'MP4 or\nWebM', fill: 'red' },
    ],
    edges: [
      { from: 'page', to: 'frames' },
      { from: 'page', to: 'audio', carries: 'sound' },
      { from: 'frames', to: 'clock' },
      { from: 'audio', to: 'clock', carries: 'sound' },
      { from: 'clock', to: 'file' },
    ],
  },
  steps: [
    {
      nodes: ['page'],
      title: 'Start a recording',
      text: 'The engine’s recording command, sent for one page, with an output path, a size, a rate and whether to keep the sound.',
    },
    {
      nodes: ['frames', 'audio'],
      title: 'Capture',
      text: 'Frames come from the paint callback, and audio as 48 kHz stereo PCM. Capture runs at an integer rate; CEF applies its own ceiling.',
    },
    {
      nodes: ['clock'],
      title: 'Resample',
      text: 'At each output tick the newest real frame is kept; nothing is blended or interpolated. Sound is cut at whole-sample boundaries, so fractional rates do not drift.',
    },
    {
      nodes: ['file'],
      title: 'Encode',
      text: 'WebM through FFmpeg, or on macOS an IOSurface converted to NV12 with Metal and encoded by h264_videotoolbox into MP4.',
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
      'A page in the engine gives frames and audio to an encoder, which publishes to an RTMP, RTMPS or TCP destination and reconnects if the connection drops.',
    nodes: [
      {
        id: 'page',
        x: 16,
        y: 96,
        w: 230,
        h: 184,
        label: 'your page',
        detail: 'in the engine',
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
      text: 'Any page in the engine. The select action switches the stream to another page in the same engine.',
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
      text: 'H.264 and AAC in FLV to an RTMP or RTMPS URL, or VP9 and Opus in WebM over TCP, with an optional reconnect policy for when the connection drops.',
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

const nativeClient: Explained = {
  doodle: componentBySlug('native-sdk').doodle,
  steps: [
    {
      nodes: ['cpp', 'abi'],
      title: 'From C++ or any language',
      text: 'Link the C++ library, or call its versioned C ABI from any language that can load one.',
    },
    {
      nodes: ['agent', 'cli'],
      title: 'Or from a shell or an MCP client',
      text: 'suitecut-native-control runs one command per call, keeps named sessions, and serves MCP over stdio.',
    },
    {
      nodes: ['client'],
      title: 'One native library',
      text: 'No CEF headers and no automation library to install alongside it.',
    },
    {
      nodes: ['engine', 'chrome', 'firefox'],
      title: 'Three browsers',
      text: 'The suitecut engine, an installed Chrome over a private CDP pipe, or Firefox over WebDriver BiDi.',
    },
  ],
}

const embed: Explained = {
  doodle: componentBySlug('embed-kit').doodle,
  steps: [
    {
      nodes: ['app'],
      title: 'Your app is the host',
      text: 'It copies the kit into its bundle, opens a session, opens tabs and attaches a view for the tab it shows. It passes on the person’s pointer, keys, text and IME input.',
    },
    {
      nodes: ['session'],
      title: 'The session service',
      text: 'suitecut-session owns the engine and a private socket, keeps each owner’s tabs and records who controls each tab.',
    },
    {
      nodes: ['engine'],
      title: 'Frames stay on the GPU',
      text: 'The engine converts each paint to NV12 with Metal, into an IOSurface it owns, and sends the app a Mach port for it. The app shows the surface until it releases the frame.',
    },
    {
      nodes: ['agent'],
      title: 'An agent joins',
      text: 'With a grant from the host it works in the same tabs, limited to the tool groups the grant names.',
    },
  ],
}

export const diagrams = {
  engine,
  together,
  recording,
  streaming,
  simpleAgent,
  nativeClient,
  embed,
} as const
