import { describe, expect, it } from 'vitest'

import { type NativeBrowser } from '../src/native-browser.js'
import { NativeElementHandle, NativePage } from '../src/native-page.js'
import { toError, type UntrustedInput } from '../src/untrusted.js'

import { InputFixture, png } from './native-screenshot-input-fixture.js'

const turn = () => new Promise<void>((resolve) => setImmediate(resolve))
const style = 'div{color:red}/*' + 'x'.repeat(100000) + '*/'
type Kind = 'page' | 'locator' | 'handle' | 'pdf'

class LifetimeFixture {
  readonly input = new InputFixture()
  readonly controller = new AbortController()
  readonly sourceLifetime = new AbortController()
  readonly calls: Record<string, UntrustedInput>[] = []
  readonly discardSignals: (AbortSignal | undefined)[] = []
  readonly appendReady: Promise<void>
  private admitAppend!: () => void
  releaseCleanup: (() => void) | undefined
  readonly source: NativeBrowser
  readonly page: NativePage

  constructor() {
    this.appendReady = new Promise((resolve) => {
      this.admitAppend = resolve
    })
    this.source = {
      pageId: 7,
      state: 'running',
      url: 'about:blank',
      signal: this.sourceLifetime.signal,
      width: 320,
      height: 180,
      pixelFormat: 'bgra',
      framesPerSecond: 60,
      cefVersion: 'unit-test',
      failure: new Promise<never>(() => undefined),
      closed: new Promise<void>(() => undefined),
      close: () => Promise.resolve(),
      navigate: () => Promise.resolve(),
      sendDevToolsCommand: () => Promise.resolve({}),
      onFrame: () => () => undefined,
      onAudio: () => () => undefined,
      runAutomation: (command, params = {}, signal) => {
        signal?.throwIfAborted()
        expect(command).toBe('artifact')
        this.calls.push(params)
        if (params.action === 'capturePrepare')
          return Promise.resolve({ token: 'nativeScreenshotInput:1', pageId: 7, contextId: 3 })
        if (params.action === 'captureCommit')
          return Promise.resolve({ data: png.toString('base64') })
        throw new Error('Unexpected unit artifact operation')
      },
      runContext: (action, params = {}, signal) => {
        if (params.action === 'append') {
          expect(signal).toBeInstanceOf(AbortSignal)
          return new Promise((_resolve, reject) => {
            signal!.addEventListener(
              'abort',
              () => reject(toError(signal!.reason as UntrustedInput)),
              { once: true },
            )
            this.admitAppend()
          })
        }
        if (params.action === 'discard') {
          this.discardSignals.push(signal)
          return new Promise((resolve) => {
            this.releaseCleanup = () => resolve({})
          })
        }
        return this.input.source.runContext(action, params, signal)
      },
    }
    this.page = new NativePage(this.source, this.controller.signal)
  }
  capture(kind: Kind): Promise<Buffer> {
    if (kind === 'pdf')
      return this.page.pdf({
        headerTemplate: '<div>actual header</div><!--' + 'x'.repeat(100000) + '-->',
        timeout: 0,
      })
    if (kind === 'locator') return this.page.locator('div').screenshot({ style, timeout: 0 })
    if (kind === 'handle')
      return new NativeElementHandle(this.page, 19).screenshot({ style, timeout: 0 })
    return this.page.screenshot({ style, timeout: 0 })
  }
}

describe('effective native page lifetime throughout prepared artifact upload', () => {
  it.each<Kind>(['page', 'locator', 'handle', 'pdf'])(
    'cancels %s mid-control upload using the constructor signal and retains unsignaled canonical discard',
    async (kind) => {
      const fixture = new LifetimeFixture(),
        reason = new Error(`constructor ${kind} canceled during append`)
      const pending = fixture.capture(kind).catch((error: UntrustedInput) => error)
      await fixture.appendReady
      fixture.controller.abort(reason)
      expect(await pending).toBe(reason)
      expect(fixture.sourceLifetime.signal.aborted).toBe(false)
      await turn()
      expect(fixture.calls[0]?.action).toBe('capturePrepare')
      expect(fixture.calls.some((call) => call.action === 'captureCommit')).toBe(false)
      expect(fixture.discardSignals).toEqual([undefined])
      expect(fixture.releaseCleanup).toBeTypeOf('function')
      fixture.releaseCleanup!()
      await turn()
    },
  )

  it('retains all16 constructor-aborted upload slots until actual canonical cleanup receipts', async () => {
    const fixtures = Array.from({ length: 16 }, () => new LifetimeFixture())
    const reasons = fixtures.map((_, index) => new Error(`projection ${index} canceled`))
    const pending = fixtures.map((fixture) =>
      fixture.capture('page').catch((error: UntrustedInput) => error),
    )
    await Promise.all(fixtures.map((fixture) => fixture.appendReady))
    fixtures.forEach((fixture, index) => fixture.controller.abort(reasons[index]))
    expect(await Promise.all(pending)).toEqual(reasons)
    await turn()
    const blocked = new LifetimeFixture()
    await expect(blocked.capture('pdf')).rejects.toThrow('staging capacity')
    expect(blocked.calls).toEqual([])
    for (const fixture of fixtures) {
      expect(fixture.discardSignals).toEqual([undefined])
      fixture.releaseCleanup!()
    }
    await turn()
    const admitted = blocked.capture('pdf').catch((error: UntrustedInput) => error)
    await blocked.appendReady
    const reason = new Error('clean final admitted projection')
    blocked.controller.abort(reason)
    expect(await admitted).toBe(reason)
    await turn()
    blocked.releaseCleanup!()
    await turn()
  })

  it.each<Kind>(['page', 'locator', 'handle', 'pdf'])(
    'rejects pre-aborted constructor lifetime before any %s preparation or chunk admission',
    async (kind) => {
      const fixture = new LifetimeFixture(),
        reason = new Error('already closed projection')
      fixture.controller.abort(reason)
      await expect(fixture.capture(kind)).rejects.toBe(reason)
      expect(fixture.calls).toEqual([])
      expect(fixture.input.actions).toEqual([])
      expect(fixture.releaseCleanup).toBeUndefined()
    },
  )
})
