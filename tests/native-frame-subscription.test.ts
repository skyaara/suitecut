// SPDX-License-Identifier: MIT
import { ChildProcess, spawn } from 'node:child_process'
import { PassThrough } from 'node:stream'

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { NativeBrowserCapabilitiesSchema } from '../src/native-browser-capabilities.js'
import { launchBrowser } from '../src/native-browser-controller.js'
import { launchNativeBrowser, type NativeBrowserOptions } from '../src/native-browser.js'
import { NativeViewerGate } from '../src/native-viewer-gate.js'

vi.mock('node:child_process', async (original) => ({
  ...(await original<{ ChildProcess: typeof ChildProcess }>()),
  spawn: vi.fn(),
}))

const capabilities = {
  schemaVersion: 1,
  backend: 'cef',
  directoryUpload: false,
  nativeFileSelectionReset: false,
  webSocketRouting: false,
  fullClockControl: false,
  coverageAcrossNavigation: false,
  offscreenOopifFullPageCapture: false,
}

describe('native raw-frame demand', () => {
  let child: ChildProcess
  let input: PassThrough
  let output: PassThrough
  let commands: { id: number; pageId: number; command: string; enabled?: boolean }[]
  const options = { executablePath: '/native/suitecut-browser', width: 2, height: 2 }
  const emit = (header: object, data = Buffer.alloc(0)): void => {
    const json = Buffer.from(JSON.stringify({ version: 1, bytes: data.length, ...header }))
    const prefix = Buffer.alloc(4)
    prefix.writeUInt32LE(json.length)
    output.write(Buffer.concat([prefix, json, data]))
  }
  const frame = (value: number, pageId = 1): void =>
    emit(
      {
        type: 'frame',
        pageId,
        timestampMs: value,
        width: 2,
        height: 2,
        dropped: 0,
      },
      Buffer.alloc(16, value),
    )
  const ready = async (frameSubscription?: boolean): Promise<void> => {
    await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
    emit({
      type: 'ready',
      timestampMs: 1,
      cefVersion: 'test',
      capabilities: {
        ...capabilities,
        ...(frameSubscription === undefined ? {} : { frameSubscription }),
      },
    })
  }
  const launch = async (extra: Partial<NativeBrowserOptions> = {}, supported = true) => {
    const launched = launchNativeBrowser({ ...options, frameTransport: 'on-demand', ...extra })
    await ready(supported)
    return launched
  }
  const command = async (index: number, enabled: boolean, pageId = 1) => {
    await vi.waitFor(() =>
      expect(commands[index]).toMatchObject({ command: 'frameSubscription', enabled, pageId }),
    )
    return commands[index]!
  }
  const ack = (request: { id: number }): void =>
    emit({ type: 'response', id: request.id, ok: true })
  const settle = async (): Promise<void> => {
    for (let i = 0; i < 8; i++) await Promise.resolve()
  }

  beforeEach(() => {
    vi.mocked(spawn).mockReset()
    child = new ChildProcess()
    child.stdin = input = new PassThrough()
    child.stdout = output = new PassThrough()
    child.stderr = new PassThrough()
    commands = []
    let remainder = ''
    input.on('data', (chunk: Buffer) => {
      remainder += chunk.toString()
      for (let end = remainder.indexOf('\n'); end !== -1; end = remainder.indexOf('\n')) {
        commands.push(JSON.parse(remainder.slice(0, end)) as (typeof commands)[number])
        remainder = remainder.slice(end + 1)
      }
    })
    input.once('finish', () => queueMicrotask(() => child.emit('close', 0)))
    vi.spyOn(child, 'kill').mockImplementation(() => {
      queueMicrotask(() => child.emit('close', 1))
      return true
    })
    vi.mocked(spawn).mockReturnValue(child)
  })
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('defaults low-level capture to always, retaining synchronous cached replay', async () => {
    const launched = launchNativeBrowser(options)
    await ready(true)
    const browser = await launched
    frame(2)
    const listener = vi.fn()
    const off = browser.onFrame(listener)
    expect(listener).toHaveBeenCalledExactlyOnceWith(
      expect.objectContaining({ data: Buffer.alloc(16, 2) }),
    )
    off()
    await settle()
    expect(commands).toEqual([])
    expect(spawn).toHaveBeenCalledWith(
      options.executablePath,
      expect.arrayContaining(['--suitecut-frame-transport=always']),
      expect.anything(),
    )
    await browser.close()
  })

  it.each([undefined, 'always'] as const)(
    'defaults high-level launch to demand, respecting override %s',
    async (override) => {
      const launched = launchBrowser({
        ...options,
        ...(override === undefined ? {} : { frameTransport: override }),
      })
      await ready(true)
      const browser = await launched
      expect(spawn).toHaveBeenCalledWith(
        options.executablePath,
        expect.arrayContaining([`--suitecut-frame-transport=${override ?? 'on-demand'}`]),
        expect.anything(),
      )
      await browser.close()
    },
  )

  it.each([undefined, false])(
    'uses legacy cached delivery when capability is %s',
    async (supported) => {
      const launched = launchNativeBrowser({ ...options, frameTransport: 'on-demand' })
      await ready(supported)
      const browser = await launched
      expect(browser.backendCapabilities?.()?.frameSubscription).toBe(false)
      frame(2)
      const listener = vi.fn()
      const off = browser.onFrame(listener)
      expect(listener).toHaveBeenCalledOnce()
      off()
      frame(3)
      browser.onFrame(listener)
      expect(listener).toHaveBeenLastCalledWith(
        expect.objectContaining({ data: Buffer.alloc(16, 3) }),
      )
      await settle()
      expect(commands).toEqual([])
      await browser.close()
    },
  )

  it('defaults an absent capability to false without granting backend authority', () => {
    expect(NativeBrowserCapabilitiesSchema.parse(capabilities).frameSubscription).toBe(false)
  })

  it('keeps pre-capability helpers compatible when the ready packet has no capability object', async () => {
    const launched = launchNativeBrowser({ ...options, frameTransport: 'on-demand' })
    await vi.waitFor(() => expect(spawn).toHaveBeenCalled())
    emit({ type: 'ready', timestampMs: 1, cefVersion: 'old-helper' })
    const browser = await launched
    frame(2)
    const listener = vi.fn()
    browser.onFrame(listener)
    expect(listener).toHaveBeenCalledOnce()
    await settle()
    expect(commands).toEqual([])
    await browser.close()
  })

  it('keeps passive frame subscription available while a human viewer owns browser actions', async () => {
    const browser = await launch()
    const gate = new NativeViewerGate(browser.processSignal!)
    gate.transition('human')
    const listener = vi.fn()
    const off = browser.onFrame(listener)
    ack(await command(0, true))
    frame(2)
    expect(listener).toHaveBeenCalledOnce()
    off()
    ack(await command(1, false))
    await browser.close()
  })

  it('enables only for the first subscriber and clears cached/in-flight frames at the last unsubscribe', async () => {
    const browser = await launch()
    const first = vi.fn(),
      second = vi.fn()
    frame(2)
    const offFirst = browser.onFrame(first)
    const offSecond = browser.onFrame(second)
    const enable = await command(0, true)
    frame(3)
    expect(first).not.toHaveBeenCalled()
    ack(enable)
    // ACK and cached view may be coalesced into one pipe read: acceptance must
    // become active in the response parser, before promise continuations run.
    frame(4)
    expect(first).toHaveBeenCalledOnce()
    expect(second).toHaveBeenCalledOnce()
    offFirst()
    await settle()
    expect(commands).toHaveLength(1)
    offSecond()
    const disable = await command(1, false)
    frame(5)
    ack(disable)
    await settle()
    const resumed = vi.fn()
    const offResumed = browser.onFrame(resumed)
    expect(resumed).not.toHaveBeenCalled()
    const reenable = await command(2, true)
    frame(6)
    expect(resumed).not.toHaveBeenCalled()
    ack(reenable)
    frame(7)
    expect(resumed).toHaveBeenCalledExactlyOnceWith(
      expect.objectContaining({ data: Buffer.alloc(16, 7) }),
    )
    offResumed()
    ack(await command(3, false))
    await browser.close()
  })

  it('coalesces an add/remove before dispatch without waking native transport', async () => {
    const browser = await launch()
    browser.onFrame(vi.fn())()
    await settle()
    expect(commands).toEqual([])
    await browser.close()
  })

  it('does not accept a previous listener generation when enable is still pending', async () => {
    const browser = await launch()
    const off = browser.onFrame(vi.fn())
    const previous = await command(0, true)
    off()
    const listener = vi.fn()
    browser.onFrame(listener)
    ack(previous)
    frame(2)
    expect(listener).not.toHaveBeenCalled()
    const current = await command(1, true)
    ack(current)
    frame(3)
    expect(listener).toHaveBeenCalledOnce()
    expect(commands).toHaveLength(2)
    await browser.close()
  })

  it('keeps pending per-page subscriptions independent and tolerates shutdown before ACK', async () => {
    const browser = await launch()
    emit(
      { type: 'context' },
      Buffer.from(JSON.stringify({ event: 'page-created', contextId: 2, pageId: 2, sequence: 1 })),
    )
    const second = browser.pageSource!(2)
    const firstListener = vi.fn(),
      secondListener = vi.fn()
    browser.onFrame(firstListener)
    second.onFrame(secondListener)
    const firstEnable = await command(0, true)
    expect(commands).toHaveLength(1)
    emit({ type: 'ready', pageId: 2, timestampMs: 2, cefVersion: 'test' })
    const secondEnable = await command(1, true, 2)
    ack(secondEnable)
    frame(3, 2)
    expect(firstListener).not.toHaveBeenCalled()
    expect(secondListener).toHaveBeenCalledOnce()
    expect(firstEnable.id).not.toBe(secondEnable.id)
    await browser.close()
    expect(browser.state).toBe('closed')
  })

  it('surfaces native subscription rejection through failure and closes the helper', async () => {
    const browser = await launch()
    const failed = expect(browser.failure).rejects.toThrow('Native frame subscription failed')
    browser.onFrame(vi.fn())
    const enable = await command(0, true)
    emit({ type: 'response', id: enable.id, ok: false, error: 'Actual transport failure' })
    await failed
    await browser.closed
  })

  it.each([false, true])(
    'reconciles a pending enable after renderer crash and late ACK (immediate recovery %s)',
    async (immediate) => {
      const browser = await launch()
      emit(
        { type: 'context' },
        Buffer.from(
          JSON.stringify({ event: 'page-created', contextId: 2, pageId: 2, sequence: 1 }),
        ),
      )
      const second = browser.pageSource!(2)
      const mainFrame = { id: 'child-main', parentId: '', name: '', url: 'about:blank' }
      emit({ type: 'ready', pageId: 2, timestampMs: 2, cefVersion: 'test', mainFrame })
      const oldListener = vi.fn()
      second.onFrame(oldListener)
      const beforeCrash = await command(0, true, 2)
      const crashed = expect(second.failure).rejects.toThrow('renderer terminated')
      emit({ type: 'pagecrash', pageId: 2, error: 'Browser renderer terminated' })
      // JS has rejected the pending request, but native can still have admitted
      // the enable. Its late response must not grant the next renderer delivery.
      ack(beforeCrash)
      if (!immediate) await settle()
      emit({ type: 'pagerecovered', pageId: 2, rendererGeneration: 2, mainFrame })
      frame(3, 2)
      expect(oldListener).not.toHaveBeenCalled()
      const reconcile = await command(1, false, 2)
      ack(reconcile)
      await crashed
      await settle()
      expect(browser.state).toBe('running')
      expect(second.state).toBe('running')
      const current = vi.fn()
      second.onFrame(current)
      ack(await command(2, true, 2))
      frame(4, 2)
      expect(current).toHaveBeenCalledOnce()
      expect(oldListener).not.toHaveBeenCalled()
      await browser.close()
    },
  )
})
