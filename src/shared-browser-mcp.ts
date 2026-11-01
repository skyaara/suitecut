// SPDX-License-Identifier: MIT
import { createInterface } from 'node:readline'
import { type Readable, type Writable } from 'node:stream'

import * as z from 'zod'

import {
  describeError,
  type JsonValue,
  type SharedBrowser,
  type SharedBrowserOptions,
  type SharedRecording,
  type SharedTab,
  launchSharedBrowser,
} from './shared-browser.js'
import { type UntrustedInput } from './untrusted.js'

/**
 * A Model Context Protocol server (stdio, newline-delimited JSON-RPC) that lets
 * an agent drive the SuiteCut shared browser. The window stays on screen,
 * so a person sees and can use the same tabs the agent works in.
 */

type Content = { type: 'text'; text: string } | { type: 'image'; data: string; mimeType: string }

interface ToolDefinition<Shape extends z.ZodRawShape> {
  readonly description: string
  readonly input: z.ZodObject<Shape>
  readonly run: (
    args: z.infer<z.ZodObject<Shape>>,
    tab: () => Promise<SharedTab>,
  ) => Promise<Content[] | string>
}

/** A tool with its arguments checked when it is called. */
export interface Tool {
  readonly description: string
  readonly input: z.ZodType
  readonly invoke: (
    args: JsonValue,
    browser: () => Promise<SharedBrowser>,
  ) => Promise<Content[] | string>
}

const tabField = {
  tab: z.number().int().optional().describe('Tab id; defaults to the tab in front'),
}
const target = {
  ...tabField,
  ref: z.string().optional().describe('Element ref from browser_snapshot, like e12'),
  x: z.number().optional().describe('Viewport x in CSS pixels, when no ref is given'),
  y: z.number().optional().describe('Viewport y in CSS pixels, when no ref is given'),
}

function tool<Shape extends z.ZodRawShape>(definition: ToolDefinition<Shape>): Tool {
  return {
    description: definition.description,
    input: definition.input,
    invoke: (raw, browser) => {
      const args = definition.input.parse(raw)
      const id = z.object({ tab: z.number().optional() }).parse(args).tab
      const tab = async () =>
        id === undefined ? (await browser()).activeTab() : (await browser()).tab(id)
      return definition.run(args, tab)
    },
  }
}

async function point(
  tab: SharedTab,
  args: { ref?: string | undefined; x?: number | undefined; y?: number | undefined },
) {
  if (args.ref) return tab.locate(args.ref)
  if (args.x === undefined || args.y === undefined) throw new Error('Give a ref, or both x and y')
  return { x: args.x, y: args.y, role: 'point', name: `${args.x},${args.y}` }
}

const loadText = (result: { url: string; error?: string }) =>
  result.error ? `Loaded ${result.url} with error ${result.error}` : `Loaded ${result.url}`

/** The tools this server offers, keyed by name. */
export function sharedBrowserTools(browser: () => Promise<SharedBrowser>): Record<string, Tool> {
  // Recordings this server started, by id, so a later call can stop them.
  const recordings = new Map<string, SharedRecording>()
  return {
    browser_navigate: tool({
      description: 'Load a URL in a tab and wait for it to finish loading.',
      input: z.object({ ...tabField, url: z.string() }),
      run: async ({ url }, tab) => loadText(await (await tab()).goto(url)),
    }),
    browser_snapshot: tool({
      description:
        'Read the page as text: headings, text, and interactive elements with refs (e1, e2, ...) to use with browser_click and browser_type. Take a new one after the page changes.',
      input: z.object(tabField),
      run: async (_, tab) => (await tab()).snapshot(),
    }),
    browser_click: tool({
      description: 'Click an element by ref, or a viewport point, with real mouse input.',
      input: z.object(target),
      run: async (args, tab) => {
        const page = await tab()
        const at = await point(page, args)
        await page.click(at.x, at.y)
        return `Clicked ${at.role} ${JSON.stringify(at.name)}`
      },
    }),
    browser_hover: tool({
      description: 'Move the mouse over an element by ref, or a viewport point.',
      input: z.object(target),
      run: async (args, tab) => {
        const page = await tab()
        const at = await point(page, args)
        await page.mouseMove(at.x, at.y, 6)
        return `Hovering ${at.role} ${JSON.stringify(at.name)}`
      },
    }),
    browser_type: tool({
      description:
        'Click a field by ref and type into it with key presses. Clears it first unless clear is false.',
      input: z.object({
        ...tabField,
        ref: z.string(),
        text: z.string(),
        clear: z.boolean().optional(),
        submit: z.boolean().optional().describe('Press Enter afterwards'),
      }),
      run: async ({ ref, text, clear, submit }, tab) => {
        const page = await tab()
        const at = await page.fillRef(ref, text, { clear: clear ?? true })
        if (submit) await page.press('Enter')
        return `Typed into ${at.role} ${JSON.stringify(at.name)}`
      },
    }),
    browser_press: tool({
      description:
        'Press a key in the focused element: Enter, Tab, Escape, Backspace, ArrowDown, SelectAll, or one character.',
      input: z.object({ ...tabField, key: z.string() }),
      run: async ({ key }, tab) => {
        await (await tab()).press(key)
        return `Pressed ${key}`
      },
    }),
    browser_scroll: tool({
      description: 'Scroll with the mouse wheel; positive deltaY scrolls down.',
      input: z.object({ ...tabField, deltaY: z.number(), deltaX: z.number().optional() }),
      run: async ({ deltaX, deltaY }, tab) => {
        await (await tab()).wheel(deltaX ?? 0, deltaY)
        return `Scrolled ${deltaY} px`
      },
    }),
    browser_evaluate: tool({
      description:
        'Evaluate a JavaScript expression in the page and return its JSON value. Promises are awaited.',
      input: z.object({ ...tabField, expression: z.string() }),
      run: async ({ expression }, tab) =>
        JSON.stringify(
          (await (await tab()).evaluate(expression, { userGesture: true })) ?? null,
          null,
          2,
        ),
    }),
    browser_screenshot: tool({
      description: 'Capture the visible part of the tab in front as a PNG image.',
      input: z.object({}),
      run: async (_, tab) => [
        {
          type: 'image',
          data: (await (await tab()).screenshot()).toString('base64'),
          mimeType: 'image/png',
        },
      ],
    }),
    browser_wait_for: tool({
      description: 'Wait for text to appear in the page, or for a number of milliseconds.',
      input: z.object({
        ...tabField,
        text: z.string().optional(),
        ms: z.number().max(60_000).optional(),
      }),
      run: async ({ text, ms }, tab) => {
        if (text !== undefined) await (await tab()).waitForText(text, ms ?? 30_000)
        else await new Promise((resolve) => setTimeout(resolve, ms ?? 1_000))
        return text === undefined ? `Waited ${ms ?? 1_000} ms` : `Found ${JSON.stringify(text)}`
      },
    }),
    browser_back: tool({
      description: 'Go back in the tab history.',
      input: z.object(tabField),
      run: async (_, tab) => loadText(await (await tab()).back()),
    }),
    browser_reload: tool({
      description: 'Reload the tab.',
      input: z.object(tabField),
      run: async (_, tab) => loadText(await (await tab()).reload()),
    }),
    browser_tabs: tool({
      description: 'List open tabs.',
      input: z.object({}),
      run: async () => JSON.stringify(await (await browser()).tabs(), null, 2),
    }),
    browser_new_tab: tool({
      description: 'Open a new tab in front and wait for it to load.',
      input: z.object({ url: z.string().optional() }),
      run: async ({ url }) => {
        const opened = await (await browser()).newTab(url ?? 'about:blank')
        return `Opened tab ${opened.id}`
      },
    }),
    browser_select_tab: tool({
      description: 'Bring a tab to the front.',
      input: z.object({ tab: z.number().int() }),
      run: async (_, tab) => {
        const page = await tab()
        await page.select()
        return `Selected tab ${page.id}`
      },
    }),
    browser_close_tab: tool({
      description: 'Close a tab. The last tab stays open.',
      input: z.object({ tab: z.number().int() }),
      run: async (_, tab) => {
        const page = await tab()
        await page.close()
        return `Closed tab ${page.id}`
      },
    }),
    browser_dialog: tool({
      description:
        'Accept or dismiss the JavaScript dialog (alert, confirm, prompt) open in a tab.',
      input: z.object({ ...tabField, accept: z.boolean(), text: z.string().optional() }),
      run: async ({ accept, text }, tab) => {
        await (await tab()).answerDialog(accept, text ?? '')
        return accept ? 'Accepted the dialog' : 'Dismissed the dialog'
      },
    }),
    browser_permission: tool({
      description:
        'Allow or block the permission prompt shown in the toolbar (clipboard, notifications, camera, ...).',
      input: z.object({ allow: z.boolean() }),
      run: async ({ allow }) =>
        (await (await browser()).answerPermission(allow))
          ? `${allow ? 'Allowed' : 'Blocked'} the permission`
          : 'No permission prompt was waiting',
    }),
    browser_record_start: tool({
      description:
        "Start recording the browser window to a video file. Frames come from Chromium's own compositor inside the browser, not screen recording. Records the tab in front; switching tabs switches the picture. Returns a recording id for browser_record_stop.",
      input: z.object({
        path: z
          .string()
          .describe(
            'Output file, such as demo.mp4; relative to the server directory. Must not exist yet.',
          ),
        fps: z
          .number()
          .int()
          .min(1)
          .max(120)
          .optional()
          .describe('Frames per second; defaults to 60'),
        audio: z.boolean().optional().describe("Record the front tab's sound; defaults to false"),
      }),
      run: async ({ path, fps, audio }) => {
        const options: Parameters<SharedBrowser['startRecording']>[0] = {
          path,
          ...(fps === undefined ? {} : { fps }),
          ...(audio === undefined ? {} : { audio }),
        }
        const recording = await (await browser()).startRecording(options)
        recordings.set(recording.info.recording, recording)
        return `Recording ${recording.info.recording} to ${recording.info.outputPath} at ${recording.info.framesPerSecond} fps, ${recording.info.canvas}`
      },
    }),
    browser_record_stop: tool({
      description: 'Finish a recording and report the file it wrote.',
      input: z.object({
        recording: z
          .string()
          .optional()
          .describe('Id from browser_record_start; defaults to the latest'),
        discard: z.boolean().optional().describe('Stop without keeping the file'),
      }),
      run: async ({ recording, discard }) => {
        const id = recording ?? [...recordings.keys()].at(-1)
        const running = id === undefined ? undefined : recordings.get(id)
        if (!id || !running) throw new Error('No recording started by this server is running')
        recordings.delete(id)
        if (discard) {
          await running.cancel()
          return `Discarded ${id}`
        }
        const result = await running.stop()
        const seconds = (result.durationMs / 1000).toFixed(1)
        return [
          `Wrote ${result.outputPath}: ${seconds} s, ${result.canvas}, ${result.framesPerSecond} fps, ${result.outputBytes} bytes.`,
          `${result.outputFrames} video frames, ${result.outputFrames - result.heldFrames} of them new and ${result.heldFrames} repeated while nothing changed.`,
        ].join(' ')
      },
    }),
    browser_events: tool({
      description:
        'Recent browser events: popups, downloads, dialogs, permission prompts, load errors.',
      input: z.object({ limit: z.number().int().max(200).optional() }),
      run: async ({ limit }) => {
        const owner = await browser()
        const recent = owner
          .eventsSince(0)
          .filter((e) => e.event !== 'loading' && e.event !== 'page')
          .slice(-(limit ?? 30))
        return recent.map((e) => JSON.stringify(e)).join('\n') || 'No events'
      },
    }),
  }
}

const request = z.object({
  jsonrpc: z.literal('2.0'),
  id: z.union([z.string(), z.number()]).optional(),
  method: z.string(),
  params: z.record(z.string(), z.json()).optional(),
})

export interface SharedBrowserMcpOptions {
  readonly browser: SharedBrowserOptions
  readonly input: Readable
  readonly output: Writable
}

/** Serves MCP until the input ends, then closes the browser it started. */
export async function serveSharedBrowserMcp({
  browser: launchOptions,
  input,
  output,
}: SharedBrowserMcpOptions): Promise<void> {
  // Started on the first tool call, and again if the window was closed.
  let launched: Promise<SharedBrowser> | undefined
  const browser = async () => {
    const pending = launched
    const current = await pending?.catch(() => undefined)
    if (current?.running) return current
    let next = launched
    if (!next || next === pending) {
      next = launchSharedBrowser(launchOptions)
      launched = next
    }
    return next
  }
  const tools = sharedBrowserTools(browser)
  const send = (message: Record<string, JsonValue>) =>
    output.write(`${JSON.stringify({ jsonrpc: '2.0', ...message })}\n`)
  const respond = async (line: string) => {
    let parsed: z.infer<typeof request>
    try {
      parsed = request.parse(JSON.parse(line))
    } catch {
      return send({ id: null, error: { code: -32700, message: 'Parse error' } })
    }
    const { id, method, params } = parsed
    if (id === undefined) return // notifications need no answer
    try {
      if (method === 'initialize')
        return send({
          id,
          result: {
            protocolVersion:
              typeof params?.protocolVersion === 'string' ? params.protocolVersion : '2025-06-18',
            capabilities: { tools: {} },
            serverInfo: { name: 'suitecut-shared-browser', version: '0.1.0' },
            instructions:
              'Drives a visible native Chromium window with tabs. Start with browser_snapshot, act with refs, and take a new snapshot after the page changes.',
          },
        })
      if (method === 'ping') return send({ id, result: {} })
      if (method === 'tools/list')
        return send({
          id,
          result: {
            tools: Object.entries(tools).map(([name, definition]) => ({
              name,
              description: definition.description,
              inputSchema: z.json().parse(z.toJSONSchema(definition.input)),
            })),
          },
        })
      if (method === 'tools/call') {
        const name = z.string().parse(params?.name)
        const definition = tools[name]
        if (!definition)
          return send({ id, error: { code: -32602, message: `Unknown tool: ${name}` } })
        try {
          const result = await definition.invoke(params?.arguments ?? {}, browser)
          const content =
            typeof result === 'string' ? [{ type: 'text' as const, text: result }] : result
          return send({ id, result: { content } })
        } catch (error) {
          return send({
            id,
            result: {
              content: [{ type: 'text', text: describeError(error as UntrustedInput) }],
              isError: true,
            },
          })
        }
      }
      return send({ id, error: { code: -32601, message: `Unknown method: ${method}` } })
    } catch (error) {
      return send({ id, error: { code: -32602, message: describeError(error as UntrustedInput) } })
    }
  }
  const work = new Set<Promise<void>>()
  const lines = createInterface({ input })
  for await (const line of lines) {
    if (!line.trim()) continue
    const job = respond(line).then(() => undefined)
    work.add(job)
    void job.finally(() => work.delete(job))
  }
  await Promise.allSettled(work)
  const started = await launched?.catch(() => undefined)
  await started?.close()
}
