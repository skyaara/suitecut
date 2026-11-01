// SPDX-License-Identifier: MIT
import { type ChildProcess, spawn } from 'node:child_process'
import { mkdtemp, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { createInterface } from 'node:readline'

import * as z from 'zod'

import { toError, type UntrustedInput } from './untrusted.js'

/**
 * Client for the SuiteCut shared browser: one native CEF window with
 * SuiteCut's own tab strip and toolbar, where every tab is a full Chromium
 * page. It drives the process started with `--suitecut-prototype-control`,
 * which reads one JSON command per line and writes replies and events.
 */

export type JsonValue = z.infer<ReturnType<typeof z.json>>

const reply = z.object({ id: z.number(), ok: z.boolean(), result: z.json().optional() })
const event = z.looseObject({ event: z.string(), tab: z.number().optional() })

/** An event the browser reported, such as a load, popup, dialog or download. */
export type SharedBrowserEvent = z.infer<typeof event> & { readonly sequence: number }

export interface SharedTabInfo {
  readonly id: number
  readonly title: string
  readonly url: string
  readonly active: boolean
}

export interface SharedBrowserOptions {
  /** Path to the built `suitecut-browser` executable. */
  readonly executablePath: string
  /** First tab's address. Defaults to `about:blank`. */
  readonly url?: string
  /** Profile directory. Defaults to a new temporary directory removed on close. */
  readonly profileDirectory?: string
  /** How permission prompts are answered: our toolbar bar, or always allow or deny. */
  readonly permissions?: 'ask' | 'allow' | 'deny'
  /** JavaScript dialogs: held until `SharedTab.answerDialog`, accepted, or shown natively. */
  readonly dialogs?: 'control' | 'accept' | 'native'
  readonly width?: number
  readonly height?: number
  /** Whether to show SuiteCut's tab strip and toolbar. Defaults to true. */
  readonly toolbar?: boolean
  readonly launchTimeout?: number
  /** Further command-line switches for the browser. */
  readonly arguments?: readonly string[]
}

export interface SharedRecordingOptions {
  /** Output file. An `.mp4` is encoded with hardware H.264. Relative paths resolve from the working directory. */
  readonly path: string
  /** Frames per second of the video, 1 to 120; only frames due at this rate are captured. Defaults to 60. */
  readonly fps?: number
  /** `page` crops to the active tab's page; `window` keeps the tab strip and toolbar. Defaults to `page`. */
  readonly area?: 'page' | 'window'
  /** Video size. Defaults to the captured area in device pixels, scaled to fit 3840x2160. */
  readonly width?: number
  readonly height?: number
  /** FFmpeg used to write the file. Defaults to `SUITECUT_FFMPEG_PATH`, then `ffmpeg` on the PATH. */
  readonly ffmpegPath?: string
  readonly maxFileBytes?: number
}

const recordingInfo = z.looseObject({
  recording: z.string(),
  outputPath: z.string(),
  framesPerSecond: z.number(),
  /** Encoded video size. */
  canvas: z.string(),
  /** Size of the first captured frame. */
  source: z.string(),
  copiedFrames: z.number(),
  skippedFrames: z.number(),
})
const recordingResult = recordingInfo.extend({
  encoder: z.string(),
  container: z.string(),
  durationMs: z.number(),
  inputFrames: z.number(),
  outputFrames: z.number(),
  heldFrames: z.number(),
  coalescedFrames: z.number(),
  supersededFrames: z.number(),
  outputBytes: z.number(),
})
/** A recording that is running. */
export type SharedRecordingInfo = z.infer<typeof recordingInfo>
/** A finished recording: frames copied from the window, and frames written at the constant rate. */
export type SharedRecordingResult = z.infer<typeof recordingResult>

export interface GotoResult {
  readonly url: string
  readonly error?: string
}

/** A visible element an agent can act on, found by its position in a page snapshot. */
export interface ElementTarget {
  readonly x: number
  readonly y: number
  readonly role: string
  readonly name: string
}

const elementTarget = z.object({ x: z.number(), y: z.number(), role: z.string(), name: z.string() })

const keys: Record<string, { code: string; keyCode: number; text?: string }> = {
  Enter: { code: 'Enter', keyCode: 13, text: '\r' },
  Tab: { code: 'Tab', keyCode: 9 },
  Backspace: { code: 'Backspace', keyCode: 8 },
  Delete: { code: 'Delete', keyCode: 46 },
  Escape: { code: 'Escape', keyCode: 27 },
  Space: { code: 'Space', keyCode: 32, text: ' ' },
  ArrowUp: { code: 'ArrowUp', keyCode: 38 },
  ArrowDown: { code: 'ArrowDown', keyCode: 40 },
  ArrowLeft: { code: 'ArrowLeft', keyCode: 37 },
  ArrowRight: { code: 'ArrowRight', keyCode: 39 },
  Home: { code: 'Home', keyCode: 36 },
  End: { code: 'End', keyCode: 35 },
  PageUp: { code: 'PageUp', keyCode: 33 },
  PageDown: { code: 'PageDown', keyCode: 34 },
}

// Runs inside the page. It neither changes the DOM nor leaves globals: a ref is
// the element's index among visible interactive elements, found again on use.
const pageScript = String.raw`(mode, index) => {
  const selector = 'a[href],button,input:not([type=hidden]),select,textarea,summary,[contenteditable=""],[contenteditable=true],[role=button],[role=link],[role=checkbox],[role=radio],[role=tab],[role=menuitem],[role=option],[role=switch],[role=combobox],[role=textbox],[role=searchbox],[tabindex]:not([tabindex="-1"])'
  const shown = (el) => {
    const r = el.getBoundingClientRect()
    return r.width > 0 && r.height > 0 && el.checkVisibility({ opacityProperty: true, visibilityProperty: true })
  }
  const clean = (text, max = 120) => (text || '').replace(/\s+/g, ' ').trim().slice(0, max)
  const labelled = (el) => (el.getAttribute('aria-labelledby') || '').split(/\s+/).map((id) => document.getElementById(id)?.innerText || '').join(' ')
  const name = (el) => clean(el.getAttribute('aria-label') || labelled(el) || el.labels?.[0]?.innerText ||
    (el.tagName === 'INPUT' && ['button', 'submit', 'reset'].includes(el.type) ? el.value : '') ||
    el.innerText || el.getAttribute('placeholder') || el.getAttribute('title') || el.getAttribute('alt') ||
    el.querySelector?.('img[alt]')?.alt || '')
  const inputRoles = { checkbox: 'checkbox', radio: 'radio', button: 'button', submit: 'button', reset: 'button', range: 'slider', search: 'searchbox' }
  const role = (el) => el.getAttribute('role') || { A: 'link', BUTTON: 'button', SELECT: 'combobox', TEXTAREA: 'textbox', SUMMARY: 'button' }[el.tagName] ||
    (el.tagName === 'INPUT' ? inputRoles[el.type] || 'textbox' : el.isContentEditable ? 'textbox' : 'generic')
  const targets = [...document.querySelectorAll(selector)].filter(shown)
  if (mode === 'locate') {
    const el = targets[index]
    if (!el) return null
    el.scrollIntoView({ block: 'center', inline: 'center', behavior: 'instant' })
    const r = el.getBoundingClientRect()
    return { x: r.left + r.width / 2, y: r.top + r.height / 2, role: role(el), name: name(el) }
  }
  const refs = new Map(targets.map((el, i) => [el, i]))
  const lines = []
  let size = 0
  const add = (line) => { if (size < 24000) { lines.push(line); size += line.length } }
  const skip = new Set(['SCRIPT', 'STYLE', 'NOSCRIPT', 'TEMPLATE', 'SVG', 'HEAD'])
  const walk = (el, inside) => {
    if (skip.has(el.tagName.toUpperCase())) return
    const ref = refs.get(el)
    if (ref !== undefined) {
      const details = []
      if (el.disabled) details.push('disabled')
      if (el.type === 'checkbox' || el.type === 'radio') details.push(el.checked ? 'checked' : 'unchecked')
      else if ('value' in el && el.tagName !== 'BUTTON' && el.value) details.push('value=' + JSON.stringify(el.type === 'password' ? '••••' : clean(el.value, 80)))
      if (el.tagName === 'A') details.push('href=' + JSON.stringify(clean(el.getAttribute('href'), 100)))
      add('- ' + role(el) + ' ' + JSON.stringify(name(el)) + ' [ref=e' + ref + ']' + (details.length ? ' ' + details.join(' ') : ''))
      inside = true
    } else if (!inside && /^H[1-6]$/.test(el.tagName) && shown(el)) {
      add('- heading ' + JSON.stringify(clean(el.innerText, 200)) + ' [level=' + el.tagName[1] + ']')
      return
    } else if (!inside) {
      const own = clean([...el.childNodes].filter((n) => n.nodeType === 3).map((n) => n.textContent).join(' '), 300)
      if (own && shown(el)) add('- text ' + JSON.stringify(own))
    }
    for (const child of el.children) walk(child, inside)
  }
  if (document.body) walk(document.body, false)
  const scroll = document.scrollingElement
  return { title: document.title, url: location.href, scroll: scroll ? [Math.round(scroll.scrollTop), Math.max(0, scroll.scrollHeight - innerHeight)] : [0, 0], lines, truncated: size >= 24000 }
}`

const snapshotResult = z.object({
  title: z.string(),
  url: z.string(),
  scroll: z.tuple([z.number(), z.number()]),
  lines: z.array(z.string()),
  truncated: z.boolean(),
})

/** Launches the shared browser and resolves once its first tab exists. */
export async function launchSharedBrowser(options: SharedBrowserOptions): Promise<SharedBrowser> {
  const owned = options.profileDirectory === undefined
  const profile =
    options.profileDirectory ?? (await mkdtemp(join(tmpdir(), 'suitecut-shared-browser-')))
  const args = [
    '--suitecut-prototype=tabs',
    `--suitecut-prototype-root=${profile}`,
    '--suitecut-prototype-pages=alloy',
    '--suitecut-prototype-switch=hide',
    '--suitecut-prototype-control',
    `--suitecut-prototype-url=${options.url ?? 'about:blank'}`,
    `--suitecut-prototype-permissions=${options.permissions ?? 'ask'}`,
    `--suitecut-prototype-size=${options.width ?? 1280}x${options.height ?? 900}`,
  ]
  const dialogs = options.dialogs ?? 'control'
  if (dialogs === 'accept') args.push('--suitecut-prototype-autodialog')
  if (dialogs === 'control') args.push('--suitecut-prototype-dialogs=control')
  if (options.toolbar === false) args.push('--suitecut-prototype-toolbar=none')
  args.push(...(options.arguments ?? []))
  const child = spawn(options.executablePath, args, { stdio: ['pipe', 'pipe', 'pipe'] })
  const browser = new SharedBrowser(child, profile, owned)
  try {
    const timeout = options.launchTimeout ?? 30_000
    const ready = await browser.waitForEvent((e) => e.event === 'ready', { timeout, since: 0 })
    if (ready.tab === undefined) throw new Error('The shared browser reported no first tab')
    const first = ready.tab
    await browser
      .waitForEvent((e) => e.tab === first && e.event === 'loading' && e.loading === false, {
        timeout,
        since: 0,
      })
      .catch(() => undefined)
    return browser
  } catch (error) {
    await browser.close()
    throw error
  }
}

export class SharedBrowser {
  private readonly events: SharedBrowserEvent[] = []
  private readonly listeners = new Set<(event: SharedBrowserEvent) => void>()
  private readonly pending = new Map<number, (message: z.infer<typeof reply>) => void>()
  private readonly exited: Promise<void>
  private nextId = 1
  private stderr = ''
  private alive = true
  /** Dialogs shown and not yet answered, by tab. */
  readonly openDialogs = new Map<number, SharedBrowserEvent>()
  /** Tabs this client has driven, which keep rendering while hidden. */
  readonly driven = new Set<number>()

  constructor(
    private readonly child: ChildProcess,
    private readonly profile: string,
    private readonly ownsProfile: boolean,
  ) {
    this.exited = new Promise((resolve) => child.once('exit', () => resolve()))
    void this.exited.then(() => {
      this.alive = false
      for (const fail of this.pending.values()) fail({ id: 0, ok: false, result: 'browser exited' })
      this.pending.clear()
    })
    child.stderr?.on('data', (chunk: Buffer) => {
      this.stderr = (this.stderr + chunk.toString()).slice(-16_384)
    })
    if (child.stdout)
      createInterface({ input: child.stdout }).on('line', (line) => this.receive(line))
  }

  /** The last part of the browser's error output, for diagnostics. */
  get errorOutput(): string {
    return this.stderr
  }

  get running(): boolean {
    return this.alive
  }

  /** Sequence number of the next event, for `waitForEvent({ since })`. */
  cursor(): number {
    return this.events.length
  }

  /** Events reported since a cursor. */
  eventsSince(since: number): readonly SharedBrowserEvent[] {
    return this.events.slice(since)
  }

  on(listener: (event: SharedBrowserEvent) => void): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  /** Resolves with the first event at or after `since` that matches, in order. */
  waitForEvent(
    match: (event: SharedBrowserEvent) => boolean,
    { timeout = 30_000, since = this.cursor() }: { timeout?: number; since?: number } = {},
  ): Promise<SharedBrowserEvent> {
    for (const seen of this.events.slice(since)) if (match(seen)) return Promise.resolve(seen)
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        stop()
        reject(new Error(`Timed out after ${timeout} ms waiting for a browser event`))
      }, timeout)
      const stop = this.on((seen) => {
        if (!match(seen)) return
        clearTimeout(timer)
        stop()
        resolve(seen)
      })
      void this.exited.then(() => {
        clearTimeout(timer)
        stop()
        reject(new Error('The shared browser exited'))
      })
    })
  }

  /** Sends one control command and returns its result; rejects when the browser refuses it. */
  async command(
    cmd: string,
    args: Record<string, JsonValue> = {},
    timeout = 30_000,
  ): Promise<JsonValue | undefined> {
    if (!this.alive) throw new Error('The shared browser is not running')
    const id = this.nextId++
    const answer = await new Promise<z.infer<typeof reply>>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id)
        reject(new Error(`Timed out after ${timeout} ms waiting for ${cmd}`))
      }, timeout)
      this.pending.set(id, (message) => {
        clearTimeout(timer)
        resolve(message)
      })
      this.child.stdin?.write(`${JSON.stringify({ id, cmd, ...args })}\n`)
    })
    if (!answer.ok) throw new Error(`${cmd} failed: ${JSON.stringify(answer.result ?? null)}`)
    return answer.result
  }

  async tabs(): Promise<SharedTabInfo[]> {
    const list = z
      .array(z.object({ tab: z.number(), title: z.string(), url: z.string(), active: z.boolean() }))
      .parse(await this.command('tabs'))
    return list.map(({ tab, ...rest }) => ({ id: tab, ...rest }))
  }

  tab(id: number): SharedTab {
    return new SharedTab(this, id)
  }

  async activeTab(): Promise<SharedTab> {
    const active = (await this.tabs()).find((tab) => tab.active)
    if (!active) throw new Error('The shared browser has no active tab')
    return this.tab(active.id)
  }

  /** Opens a tab in the foreground and waits for its first load. */
  async newTab(url = 'about:blank', timeout = 30_000): Promise<SharedTab> {
    const since = this.cursor()
    const id = z.number().parse(await this.command('open', { url }))
    if (id === 0) throw new Error('The shared browser is closing')
    await this.waitForEvent((e) => e.tab === id && e.event === 'loading' && e.loading === false, {
      since,
      timeout,
    }).catch(() => undefined)
    return this.tab(id)
  }

  /** Answers the oldest permission prompt; resolves false when none is waiting. */
  async answerPermission(allow: boolean): Promise<boolean> {
    return this.command('permission', { allow }).then(
      () => true,
      () => false,
    )
  }

  /**
   * Records the window to a video file. Frames come from Chromium's compositor
   * inside the browser process as it draws them, not from screen recording or
   * DevTools screencast, and go through SuiteCut's encoder. Resolves once
   * frames are being encoded. Records whichever tab is in front.
   */
  async startRecording(options: SharedRecordingOptions): Promise<SharedRecording> {
    const args: Record<string, JsonValue> = {
      path: resolve(options.path),
      fps: options.fps ?? 60,
      area: options.area ?? 'page',
      ffmpegPath: options.ffmpegPath ?? process.env.SUITECUT_FFMPEG_PATH ?? 'ffmpeg',
    }
    if (options.width !== undefined) args.width = options.width
    if (options.height !== undefined) args.height = options.height
    if (options.maxFileBytes !== undefined) args.maxFileBytes = options.maxFileBytes
    return new SharedRecording(
      this,
      recordingInfo.parse(await this.command('record-start', args, 30_000)),
    )
  }

  /** Closes the window, waits for the process, and removes a temporary profile. */
  async close(): Promise<void> {
    if (this.alive) {
      await this.command('quit', {}, 5_000).catch(() => undefined)
      const timer = setTimeout(() => this.child.kill('SIGKILL'), 15_000)
      await this.exited
      clearTimeout(timer)
    }
    if (this.ownsProfile) await rm(this.profile, { recursive: true, force: true })
  }

  private receive(line: string): void {
    let parsed: JsonValue
    try {
      parsed = z.json().parse(JSON.parse(line))
    } catch {
      return
    }
    const answer = reply.safeParse(parsed)
    if (answer.success && !('event' in answer.data)) {
      this.pending.get(answer.data.id)?.(answer.data)
      this.pending.delete(answer.data.id)
      return
    }
    const seen = event.safeParse(parsed)
    if (!seen.success) return
    const recorded: SharedBrowserEvent = { ...seen.data, sequence: this.events.length }
    this.events.push(recorded)
    if (recorded.tab !== undefined) {
      if (recorded.event === 'dialog') this.openDialogs.set(recorded.tab, recorded)
      if (
        recorded.event === 'closed' ||
        (recorded.event === 'loading' && recorded.loading === true)
      )
        this.openDialogs.delete(recorded.tab)
    }
    for (const listener of [...this.listeners]) listener(recorded)
  }
}

export class SharedRecording {
  constructor(
    private readonly browser: SharedBrowser,
    readonly info: SharedRecordingInfo,
  ) {}

  /** Finishes the file and returns what was written. */
  async stop(timeout = 60_000): Promise<SharedRecordingResult> {
    return recordingResult.parse(
      await this.browser.command('record-stop', { recording: this.info.recording }, timeout),
    )
  }

  /** Stops without keeping the file. */
  async cancel(timeout = 30_000): Promise<void> {
    await this.browser.command(
      'record-stop',
      { recording: this.info.recording, cancel: true },
      timeout,
    )
  }
}

export class SharedTab {
  private pointer = { x: 0, y: 0 }

  constructor(
    readonly browser: SharedBrowser,
    readonly id: number,
  ) {}

  /** Sends a Chrome DevTools Protocol method to this tab's page. */
  async cdp(
    method: string,
    params: Record<string, JsonValue> = {},
    timeout?: number,
  ): Promise<JsonValue | undefined> {
    return this.browser.command('cdp', { tab: this.id, method, params }, timeout)
  }

  // A hidden page, in a background tab or a covered window, draws no frames:
  // screenshots then hang and input waits for acknowledgements. Focus
  // emulation keeps a driven page rendering and focused, as Playwright does.
  private async drive(): Promise<void> {
    if (this.browser.driven.has(this.id)) return
    await this.cdp('Emulation.setFocusEmulationEnabled', { enabled: true })
    this.browser.driven.add(this.id)
  }

  // An input event that opens a dialog is acknowledged only once the dialog
  // closes, so stop waiting as soon as one opens.
  private async input(method: string, params: Record<string, JsonValue>): Promise<void> {
    if (this.browser.openDialogs.has(this.id)) throw new Error('A dialog is open. Answer it first.')
    await this.drive()
    let stop: () => void = () => undefined
    const opened = new Promise<void>((resolve) => {
      stop = this.browser.on((e) => {
        if (e.tab === this.id && e.event === 'dialog') resolve()
      })
    })
    const sent = this.cdp(method, params)
    try {
      await Promise.race([sent, opened])
    } finally {
      stop()
      sent.catch(() => undefined)
    }
  }

  /** Brings this tab to the front. */
  async select(): Promise<void> {
    await this.browser.command('select', { tab: this.id })
  }

  async close(): Promise<void> {
    await this.browser.command('close', { tab: this.id })
  }

  /** Navigates and waits until the page finishes loading. */
  async goto(url: string, timeout = 30_000): Promise<GotoResult> {
    const since = this.browser.cursor()
    await this.browser.command('navigate', { tab: this.id, url })
    return this.loaded(since, timeout)
  }

  async back(timeout = 30_000): Promise<GotoResult> {
    return this.history('back', timeout)
  }

  async forward(timeout = 30_000): Promise<GotoResult> {
    return this.history('forward', timeout)
  }

  async reload(timeout = 30_000): Promise<GotoResult> {
    return this.history('reload', timeout)
  }

  /** Evaluates an expression in the page and returns its JSON value. */
  async evaluate(
    expression: string,
    { userGesture = false, timeout = 30_000 }: { userGesture?: boolean; timeout?: number } = {},
  ): Promise<JsonValue | undefined> {
    const dialog = this.browser.openDialogs.get(this.id)
    if (dialog)
      throw new Error(
        `A ${String(dialog.type === 0 ? 'alert' : dialog.type === 1 ? 'confirm' : 'prompt')} dialog is open: ${JSON.stringify(dialog.message ?? '')}. Answer it first.`,
      )
    const response = z
      .object({
        result: z.object({ value: z.json().optional() }).loose(),
        exceptionDetails: z
          .object({
            text: z.string(),
            exception: z.object({ description: z.string().optional() }).loose().optional(),
          })
          .loose()
          .optional(),
      })
      .parse(
        await this.cdp(
          'Runtime.evaluate',
          { expression, returnByValue: true, awaitPromise: true, userGesture },
          timeout,
        ),
      )
    if (response.exceptionDetails)
      throw new Error(
        response.exceptionDetails.exception?.description ?? response.exceptionDetails.text,
      )
    return response.result.value
  }

  async title(): Promise<string> {
    return z.string().parse(await this.evaluate('document.title'))
  }

  async url(): Promise<string> {
    return z.string().parse(await this.evaluate('location.href'))
  }

  async content(): Promise<string> {
    return z.string().parse(await this.evaluate('document.documentElement.outerHTML'))
  }

  /** Captures the page as PNG, also when its tab is in the background. */
  async screenshot(): Promise<Buffer> {
    await this.drive()
    const { data } = z
      .object({ data: z.string() })
      .parse(await this.cdp('Page.captureScreenshot', { format: 'png' }))
    return Buffer.from(data, 'base64')
  }

  /** A text outline of the page for agents: headings, text, and interactive elements with refs. */
  async snapshot(): Promise<string> {
    // The page is paused while a dialog is open, so report only the dialog.
    const open = this.browser.openDialogs.get(this.id)
    if (open)
      return `Open dialog: ${JSON.stringify(open.message ?? '')}\nThe page is paused until the dialog is answered.`
    const page = snapshotResult.parse(await this.evaluate(`(${pageScript})('snapshot', 0)`))
    const header = [
      `Page: ${page.title || '(untitled)'}`,
      `URL: ${page.url}`,
      `Scroll: ${page.scroll[0]} of ${page.scroll[1]} px`,
    ]
    const dialog = this.browser.openDialogs.get(this.id)
    if (dialog) header.push(`Open dialog: ${JSON.stringify(dialog.message ?? '')}`)
    return [...header, '', ...page.lines, ...(page.truncated ? ['(truncated)'] : [])].join('\n')
  }

  /** Scrolls a snapshot ref into view and returns its centre in page coordinates. */
  async locate(ref: string): Promise<ElementTarget> {
    const index = /^e(\d+)$/.exec(ref)?.[1]
    if (index === undefined) throw new Error(`Not a snapshot ref: ${ref}`)
    const found = elementTarget
      .nullable()
      .parse(await this.evaluate(`(${pageScript})('locate', ${Number(index)})`))
    if (!found) throw new Error(`No element ${ref}; take a new snapshot`)
    return found
  }

  /** Moves the mouse in straight steps from where it last was. */
  async mouseMove(x: number, y: number, steps = 1): Promise<void> {
    const from = this.pointer
    for (let step = 1; step <= steps; step++) {
      const at = {
        x: from.x + ((x - from.x) * step) / steps,
        y: from.y + ((y - from.y) * step) / steps,
      }
      await this.input('Input.dispatchMouseEvent', { type: 'mouseMoved', ...at })
    }
    this.pointer = { x, y }
  }

  async click(
    x: number,
    y: number,
    {
      button = 'left',
      clickCount = 1,
    }: { button?: 'left' | 'right' | 'middle'; clickCount?: number } = {},
  ): Promise<void> {
    await this.mouseMove(x, y, 4)
    const buttons = button === 'left' ? 1 : button === 'right' ? 2 : 4
    await this.input('Input.dispatchMouseEvent', {
      type: 'mousePressed',
      x,
      y,
      button,
      buttons,
      clickCount,
    })
    await delay(40)
    await this.input('Input.dispatchMouseEvent', {
      type: 'mouseReleased',
      x,
      y,
      button,
      buttons: 0,
      clickCount,
    })
  }

  async wheel(deltaX: number, deltaY: number): Promise<void> {
    await this.input('Input.dispatchMouseEvent', {
      type: 'mouseWheel',
      ...this.pointer,
      deltaX,
      deltaY,
    })
  }

  /** Types text as key presses; a newline presses Enter. */
  async type(text: string): Promise<void> {
    for (const character of text) {
      if (character === '\n') {
        await this.press('Enter')
        continue
      }
      await this.input('Input.dispatchKeyEvent', {
        type: 'keyDown',
        key: character,
        text: character,
        unmodifiedText: character,
      })
      await this.input('Input.dispatchKeyEvent', { type: 'keyUp', key: character })
    }
  }

  /** Presses a named key such as Enter, Tab, Escape or ArrowDown, or selects all with "SelectAll". */
  async press(key: string): Promise<void> {
    if (key === 'SelectAll') {
      await this.input('Input.dispatchKeyEvent', {
        type: 'rawKeyDown',
        key: 'a',
        code: 'KeyA',
        windowsVirtualKeyCode: 65,
        modifiers: 4,
        commands: ['selectAll'],
      })
      await this.input('Input.dispatchKeyEvent', {
        type: 'keyUp',
        key: 'a',
        code: 'KeyA',
        windowsVirtualKeyCode: 65,
        modifiers: 4,
      })
      return
    }
    const known = keys[key]
    if (!known) {
      if ([...key].length !== 1) throw new Error(`Unknown key: ${key}`)
      return this.type(key)
    }
    const common = {
      key: key === 'Space' ? ' ' : key,
      code: known.code,
      windowsVirtualKeyCode: known.keyCode,
    }
    await this.input('Input.dispatchKeyEvent', {
      type: known.text ? 'keyDown' : 'rawKeyDown',
      ...common,
      ...(known.text ? { text: known.text, unmodifiedText: known.text } : {}),
    })
    await this.input('Input.dispatchKeyEvent', { type: 'keyUp', ...common })
  }

  /** Clicks a snapshot ref like a user would. */
  async clickRef(ref: string): Promise<ElementTarget> {
    const target = await this.locate(ref)
    await this.click(target.x, target.y)
    return target
  }

  /** Clicks a field, optionally clears it, and types into it. */
  async fillRef(
    ref: string,
    text: string,
    { clear = true }: { clear?: boolean } = {},
  ): Promise<ElementTarget> {
    const target = await this.clickRef(ref)
    if (clear) {
      await this.press('SelectAll')
      await this.press('Backspace')
    }
    await this.type(text)
    return target
  }

  /** Accepts or dismisses this tab's open JavaScript dialog. */
  async answerDialog(accept: boolean, text = ''): Promise<void> {
    await this.browser.command('dialog', { tab: this.id, accept, text })
    this.browser.openDialogs.delete(this.id)
  }

  /** Waits until text appears in the page. */
  async waitForText(text: string, timeout = 30_000): Promise<void> {
    const deadline = Date.now() + timeout
    const probe = `document.body?.innerText.includes(${JSON.stringify(text)}) ?? false`
    while ((await this.evaluate(probe)) !== true) {
      if (Date.now() > deadline)
        throw new Error(`Timed out after ${timeout} ms waiting for ${JSON.stringify(text)}`)
      await delay(200)
    }
  }

  private async history(cmd: 'back' | 'forward' | 'reload', timeout: number): Promise<GotoResult> {
    const since = this.browser.cursor()
    await this.browser.command(cmd, { tab: this.id })
    return this.loaded(since, timeout)
  }

  private async loaded(since: number, timeout: number): Promise<GotoResult> {
    let started = false
    const done = await this.browser.waitForEvent(
      (e) => {
        if (e.tab !== this.id || e.event !== 'loading') return false
        if (e.loading === true) started = true
        return started && e.loading === false
      },
      { since, timeout },
    )
    const failure = this.browser
      .eventsSince(since)
      .find((e) => e.tab === this.id && e.event === 'load-error')
    const url = typeof done.url === 'string' ? done.url : ''
    return failure
      ? { url, error: typeof failure.error === 'string' ? failure.error : 'load failed' }
      : { url }
  }
}

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms))
}

/** Normalizes a thrown value for tool replies. */
export function describeError(error: UntrustedInput): string {
  return toError(error).message
}
