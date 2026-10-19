// SPDX-License-Identifier: MIT
/* eslint import-x/consistent-type-specifier-style: off -- Whole type imports erase lifecycle module edges. */
import * as z from 'zod'

import { type NativeBrowser } from './native-browser.js'
import { NativeDebuggerSession, type NativeDebuggerOptions } from './native-debugger.js'
import { waitNativeEventPredicate } from './native-event-predicate.js'
import { NativeConsoleInfoSchema, type NativeConsoleInfo } from './native-protocol.js'
import { createNativeValueCodec } from './native-values.js'
import {
  NativeWorkerControlSchema,
  NativeWorkerEventSchema,
  NativeWorkerInfoSchema,
  type NativeWorkerControl,
  type NativeWorkerEvent,
  type NativeWorkerInfo,
} from './native-worker-protocol.js'
import {
  NativeWorkerRetirements,
  type NativeWorkerRetirementSnapshot,
} from './native-worker-retirements.js'
import { type UntrustedInput } from './untrusted.js'

import type { NativeConsoleMessage } from './native-page.js'

export interface NativeWorkerBrowser extends Pick<
  NativeBrowser,
  | 'signal'
  | 'pageSignal'
  | 'processSignal'
  | 'processClosed'
  | 'processExit'
  | 'runAutomation'
  | 'onEventPredicate'
  | 'eventSequence'
  | 'onConsoleMessage'
> {
  workerInfos?(): NativeWorkerInfo[]
  workerRetirements?(): NativeWorkerRetirementSnapshot
  onWorkerMessage?(listener: (event: NativeWorkerEvent) => void): () => void
}
export type NativeWorkerRun = (
  params: Record<string, UntrustedInput>,
  signal?: AbortSignal,
) => Promise<UntrustedInput>
export interface NativeWorkerOwner {
  source: NativeWorkerBrowser
  runWorker: NativeWorkerRun
  /** Only an actual Source page supplies this private transport; brokers do not. */
  runDebugger?: NativeWorkerRun
  defaultTimeout(): number
  isPageHandle(value: UntrustedInput): boolean
  createConsole(info: NativeConsoleInfo): NativeConsoleMessage
}
const EvaluationSchema = z.object({
  result: z.object({ value: z.unknown().optional() }).optional(),
  exceptionDetails: z
    .object({
      text: z.string(),
      exception: z
        .object({ description: z.string().optional(), value: z.unknown().optional() })
        .optional(),
    })
    .optional(),
})
const HandleSchema = z.strictObject({
  handle: z.number().int().positive(),
  guid: z.string().min(1).max(256),
  preview: z.string().max(8192),
  node: z.literal(false),
  worker: z.string().min(1).max(128),
  workerControl: NativeWorkerControlSchema.optional(),
})
function functionBody(fn: { toString(): string } | string): string {
  let body = typeof fn === 'string' ? fn : fn.toString()
  if (typeof fn === 'string') return body
  if (!/^async\s+function\b/u.test(body) && /^async\s+[\p{L}\p{N}_$]+\s*\(/u.test(body))
    body = body.replace(/^async\s+/u, 'async function ')
  else if (
    /^[\p{L}\p{N}_$]+\s*\(/u.test(body) &&
    !body.startsWith('function') &&
    !/^async\s*\(/u.test(body)
  )
    body = `function ${body}`
  return body
}
function workerArgument(worker: NativeWorker, value: UntrustedInput) {
  const handles: NativeWorkerHandle[] = []
  const encoded = createNativeValueCodec().encode(value, 'argument', (item) => {
    if (worker.isPageHandle(item))
      throw new Error('Page and frame handles cannot be passed into a worker execution context')
    if (!(item instanceof NativeWorkerHandle)) return undefined
    item.assertUsable(worker)
    let index = handles.indexOf(item)
    if (index < 0) {
      index = handles.length
      handles.push(item)
    }
    return index
  })
  return { encoded, handles: handles.map((handle) => handle.id) }
}
function decodeEvaluation<R>(value: UntrustedInput): R {
  const result = EvaluationSchema.parse(value)
  if (result.exceptionDetails) {
    const details = result.exceptionDetails
    const exception = details.exception
    throw new Error(
      exception?.description ??
        (exception && Object.hasOwn(exception, 'value') ? String(exception.value) : details.text),
    )
  }
  return createNativeValueCodec().decode(result.result?.value as UntrustedInput) as R
}

/** A real native worker target, with its own execution context and lifecycle. */
export class NativeWorker {
  private closed = false
  private channelError: string | undefined
  private consoleLifetime = new AbortController()
  private channelEpoch = 0
  private channelClosedSequence: number | undefined
  private lifecycleSequence: number | undefined
  private nativeControl: NativeWorkerControl | undefined
  private readonly consoleListeners: ((message: NativeConsoleMessage) => void)[] = []
  private readonly closeListeners: ((worker: NativeWorker) => void)[] = []
  constructor(
    private readonly owner: NativeWorkerOwner,
    readonly info: NativeWorkerInfo,
  ) {
    this.nativeControl = info.control ? { ...info.control } : undefined
  }
  isPageHandle(value: UntrustedInput): boolean {
    return this.owner.isPageHandle(value)
  }
  get source(): NativeWorkerBrowser {
    return this.owner.source
  }
  url(): string {
    return this.info.url
  }
  isClosed(): boolean {
    return this.closed
  }
  on(event: 'close', listener: (worker: NativeWorker) => void): this
  on(event: 'console', listener: (message: NativeConsoleMessage) => void): this
  on(
    event: 'close' | 'console',
    listener: ((worker: NativeWorker) => void) | ((message: NativeConsoleMessage) => void),
  ): this {
    z.enum(['close', 'console']).parse(event)
    if (event === 'console')
      this.consoleListeners.push(listener as (message: NativeConsoleMessage) => void)
    else this.closeListeners.push(listener as (worker: NativeWorker) => void)
    return this
  }
  once(event: 'close', listener: (worker: NativeWorker) => void): this
  once(event: 'console', listener: (message: NativeConsoleMessage) => void): this
  once(
    event: 'close' | 'console',
    listener: ((worker: NativeWorker) => void) | ((message: NativeConsoleMessage) => void),
  ): this {
    z.enum(['close', 'console']).parse(event)
    if (event === 'console') {
      const once = (message: NativeConsoleMessage) => {
        this.off('console', once)
        ;(listener as (message: NativeConsoleMessage) => void)(message)
      }
      return this.on('console', once)
    }
    const once = (worker: NativeWorker) => {
      this.off('close', once)
      ;(listener as (worker: NativeWorker) => void)(worker)
    }
    return this.on('close', once)
  }
  off(event: 'close', listener: (worker: NativeWorker) => void): this
  off(event: 'console', listener: (message: NativeConsoleMessage) => void): this
  off(
    event: 'close' | 'console',
    listener: ((worker: NativeWorker) => void) | ((message: NativeConsoleMessage) => void),
  ): this {
    z.enum(['close', 'console']).parse(event)
    const listeners = event === 'console' ? this.consoleListeners : this.closeListeners
    const index = listeners.lastIndexOf(
      listener as ((worker: NativeWorker) => void) & ((message: NativeConsoleMessage) => void),
    )
    if (index >= 0) listeners.splice(index, 1)
    return this
  }
  readonly addListener = this.on.bind(this)
  readonly removeListener = this.off.bind(this)
  consoleFromNative(message: NativeConsoleMessage): void {
    for (const listener of [...this.consoleListeners]) listener(message)
  }
  acceptsLifecycleReceipt(sequence: number): boolean {
    return this.lifecycleSequence === undefined || sequence >= this.lifecycleSequence
  }
  observeLifecycleReceipt(sequence: number): void {
    this.lifecycleSequence = Math.max(this.lifecycleSequence ?? sequence, sequence)
  }
  closeFromNative(sequence?: number): void {
    if (sequence !== undefined) this.observeLifecycleReceipt(sequence)
    if (this.closed) return
    this.closed = true
    this.consoleLifetime.abort(new Error('Target page, context or browser has been closed'))
    this.consoleListeners.length = 0
    for (const listener of [...this.closeListeners]) listener(this)
    this.closeListeners.length = 0
  }
  channelClosedFromNative(reason: string, sequence?: number): void {
    if (this.closed || (sequence !== undefined && sequence === this.channelClosedSequence)) return
    this.channelClosedSequence = sequence
    if (sequence !== undefined) this.observeLifecycleReceipt(sequence)
    this.channelEpoch++
    this.channelError = reason
    this.consoleLifetime.abort(new Error(reason))
    this.consoleListeners.length = 0
  }
  readyFromNative(sequence: number, control?: NativeWorkerControl): void {
    if (!this.acceptsLifecycleReceipt(sequence)) return
    this.observeLifecycleReceipt(sequence)
    if (this.closed) return
    if (control) {
      this.nativeControl = { ...control }
      this.info.control = { ...control }
    }
    if (!this.channelError) return
    this.channelError = undefined
    this.consoleLifetime = new AbortController()
  }
  captureNativeControl(): NativeWorkerControl | undefined {
    return this.nativeControl ? { ...this.nativeControl } : undefined
  }
  assertNativeControl(control: NativeWorkerControl | undefined): void {
    if (!this.nativeControl && !control) return
    if (!this.nativeControl || !control)
      throw new Error('Native worker control identity is unavailable')
    if (
      this.nativeControl.owner !== control.owner ||
      this.nativeControl.generation !== control.generation
    )
      throw new Error(
        'Native worker handle belongs to another Inspector owner or attachment generation',
      )
  }
  captureChannelEpoch(): number {
    return this.channelEpoch
  }
  assertChannelEpoch(epoch: number, control?: NativeWorkerControl): void {
    if (epoch === this.channelEpoch) return
    const replacedControl =
      control &&
      this.nativeControl &&
      (control.owner !== this.nativeControl.owner ||
        control.generation !== this.nativeControl.generation)
    throw new Error(
      replacedControl
        ? 'Native worker handle belongs to another Inspector owner or attachment generation (earlier Inspector control channel)'
        : 'Native worker handle belongs to an earlier Inspector control channel',
    )
  }
  assertUsable(): void {
    this.source.signal.throwIfAborted()
    if (this.closed)
      throw new Error('Target page, context or browser has been closed (native worker closed)')
    if (this.channelError) throw new Error(this.channelError)
  }
  private async run(
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput> {
    this.assertUsable()
    const epoch = this.channelEpoch
    const control = this.captureNativeControl()
    const result = await this.owner.runWorker(
      {
        worker: this.info.id,
        timeoutMs: this.owner.defaultTimeout(),
        ...params,
        ...(control ? { workerControl: control } : {}),
      },
      signal,
    )
    // A physically completed reply can be queued before a later attach event.
    // Its old opaque handles must not be adopted into the replacement channel.
    if (params.action !== 'waitClose') {
      this.assertChannelEpoch(epoch, control)
      this.assertNativeControl(control)
    }
    return result
  }
  /** Source-owned dedicated-worker debugger. Observed pauses/resume only. */
  createDebuggerSession(options: NativeDebuggerOptions = {}): Promise<NativeDebuggerSession> {
    this.assertUsable()
    const send = this.owner.runDebugger
    const control = this.captureNativeControl()
    if (this.info.type !== 'worker' || !send || !control)
      throw new Error(
        'Native debugger supports only Source-owned dedicated workers with actual attachment identity',
      )
    const epoch = this.captureChannelEpoch()
    const target = { worker: this.info.id, workerControl: control }
    const lifetime = AbortSignal.any([this.source.signal, this.consoleLifetime.signal])
    return NativeDebuggerSession.create(
      {
        signal: lifetime,
        cleanupOwner: this,
        agentControls: false,
        defaultTimeout: () => this.owner.defaultTimeout(),
        runDebugger: (params, signal) => {
          this.assertUsable()
          this.assertChannelEpoch(epoch, control)
          this.assertNativeControl(control)
          return send({ ...params, ...target }, signal)
        },
        // Cleanup retains the exact old attachment. Never adopt a reattached
        // worker's newest control or fall back to a root/page debugger.
        cleanupDebugger: (session) =>
          send(
            { action: 'close', session, timeoutMs: 0, ...target },
            this.source.pageSignal ?? this.source.signal,
          ),
      },
      options,
    )
  }
  waitForEvent(
    event: 'close',
    options?: { timeout?: number; signal?: AbortSignal },
  ): Promise<NativeWorker>
  waitForEvent(
    event: 'console',
    options?: {
      timeout?: number
      signal?: AbortSignal
      predicate?: (message: NativeConsoleMessage) => boolean | Promise<boolean>
    },
  ): Promise<NativeConsoleMessage>
  async waitForEvent(
    event: 'close' | 'console',
    options: {
      timeout?: number
      signal?: AbortSignal
      predicate?: (message: NativeConsoleMessage) => boolean | Promise<boolean>
    } = {},
  ): Promise<NativeWorker | NativeConsoleMessage> {
    z.enum(['close', 'console']).parse(event)
    const config = z
      .strictObject({
        timeout: z.number().int().min(0).max(120_000).optional(),
        signal: z.instanceof(AbortSignal).optional(),
        predicate: z
          .custom<(message: NativeConsoleMessage) => boolean | Promise<boolean>>(
            (value) => typeof value === 'function',
          )
          .optional(),
      })
      .parse(options)
    if (event === 'console') {
      this.assertUsable()
      return waitNativeEventPredicate(
        this.source,
        'console',
        (value) => {
          const info = NativeConsoleInfoSchema.parse(value)
          return { info, message: this.owner.createConsole(info) }
        },
        async ({ info, message }) =>
          info.worker === this.info.id && (!config.predicate || (await config.predicate(message))),
        {
          timeout: config.timeout ?? this.owner.defaultTimeout(),
          signal: AbortSignal.any([
            this.consoleLifetime.signal,
            ...(config.signal ? [config.signal] : []),
          ]),
        },
      ).then(({ message }) => message)
    }
    await this.run(
      { action: 'waitClose', timeoutMs: config.timeout ?? this.owner.defaultTimeout() },
      config.signal,
    )
    return this
  }
  async evaluate<R>(fn: (() => R | Promise<R>) | string): Promise<R>
  async evaluate<R, A>(fn: (arg: A) => R | Promise<R>, arg: A): Promise<R>
  async evaluate<R, A>(fn: ((arg: A) => R | Promise<R>) | string, arg?: A): Promise<R> {
    if (arguments.length > 2)
      throw new Error('Too many arguments; pass arguments as a single object')
    const { encoded, handles } = workerArgument(this, arg as UntrustedInput)
    const invocation =
      typeof fn === 'string'
        ? `(0,eval)(${JSON.stringify(fn)})`
        : `(${functionBody(fn)}).call(globalThis,codec.decode(${JSON.stringify(encoded)},values))`
    const declaration = `async function(...handles){const codec=(${createNativeValueCodec.toString()})();const values=handles.map(h=>h.value);return codec.encode(await (${invocation}));}\n//# sourceURL=suitecut://native/worker.evaluate\n`
    return decodeEvaluation<R>(
      await this.run(
        handles.length
          ? { action: 'evaluate', function: declaration, handles }
          : { action: 'evaluate', expression: `(${declaration})()` },
      ),
    )
  }
  async evaluateHandle(
    fn: (() => UntrustedInput | Promise<UntrustedInput>) | string,
  ): Promise<NativeWorkerHandle>
  async evaluateHandle<A>(
    fn: (arg: A) => UntrustedInput | Promise<UntrustedInput>,
    arg: A,
  ): Promise<NativeWorkerHandle>
  async evaluateHandle<A>(
    fn: ((arg: A) => UntrustedInput | Promise<UntrustedInput>) | string,
    arg?: A,
  ): Promise<NativeWorkerHandle> {
    if (arguments.length > 2)
      throw new Error('Too many arguments; pass arguments as a single object')
    const { encoded, handles } = workerArgument(this, arg as UntrustedInput)
    const invocation =
      typeof fn === 'string'
        ? `(0,eval)(${JSON.stringify(fn)})`
        : `(${functionBody(fn)}).call(globalThis,codec.decode(${JSON.stringify(encoded)},values))`
    const declaration = `async function(...handles){const codec=(${createNativeValueCodec.toString()})();const values=handles.map(h=>h.value);return {value:await (${invocation})};}`
    return this.adoptHandle(
      await this.run(
        handles.length
          ? { action: 'create', function: declaration, handles }
          : {
              action: 'create',
              expression: `(async()=>{const codec=(${createNativeValueCodec.toString()})();const values=[];return await (${invocation});})()`,
            },
      ),
    )
  }
  adoptHandle(value: UntrustedInput): NativeWorkerHandle {
    const info = HandleSchema.parse(value)
    if (info.worker !== this.info.id) throw new Error('Native handle belongs to another worker')
    this.assertNativeControl(info.workerControl)
    return new NativeWorkerHandle(this, info.handle, info.guid, info.preview, info.workerControl)
  }
  runHandle(params: Record<string, UntrustedInput>): Promise<UntrustedInput> {
    return this.run(params)
  }
  disposeHandle(id: number): Promise<UntrustedInput> {
    if (this.closed || this.source.signal.aborted) return Promise.resolve(undefined)
    return this.owner.runWorker({
      action: 'dispose',
      worker: this.info.id,
      handle: id,
      timeoutMs: this.owner.defaultTimeout(),
      ...(this.nativeControl ? { workerControl: { ...this.nativeControl } } : {}),
    })
  }
}

/** An actual worker remote reference, never a page/frame wrapper. */
export class NativeWorkerHandle {
  private disposed = false
  private readonly channelEpoch: number
  constructor(
    private readonly worker: NativeWorker,
    readonly id: number,
    readonly guid: string,
    private readonly preview: string,
    private readonly nativeControl?: NativeWorkerControl,
  ) {
    this.channelEpoch = worker.captureChannelEpoch()
  }
  assertUsable(worker: NativeWorker): void {
    if (this.disposed)
      throw new Error(`no object with guid ${this.guid} (native worker handle is disposed)`)
    if (worker !== this.worker)
      throw new Error('Native handle belongs to a different worker execution context')
    worker.assertChannelEpoch(this.channelEpoch, this.nativeControl)
    worker.assertNativeControl(this.nativeControl)
    worker.assertUsable()
  }
  asElement(): null {
    return null
  }
  toString(): string {
    return this.preview
  }
  async evaluate<R, A>(
    fn: ((value: UntrustedInput, arg: A) => R | Promise<R>) | string,
    arg?: A,
  ): Promise<R> {
    if (arguments.length > 2)
      throw new Error('Too many arguments; pass arguments as a single object')
    this.assertUsable(this.worker)
    const { encoded, handles } = workerArgument(this.worker, arg as UntrustedInput)
    const invocation =
      typeof fn === 'string'
        ? `(0,eval)(${JSON.stringify(fn)})`
        : `(${functionBody(fn)}).call(globalThis,this.value,codec.decode(${JSON.stringify(encoded)},values))`
    const declaration = `async function(...handles){const codec=(${createNativeValueCodec.toString()})();const values=handles.map(h=>h.value);return codec.encode(await (${invocation}));}`
    return decodeEvaluation<R>(
      await this.worker.runHandle({
        action: 'evaluate',
        handle: this.id,
        function: declaration,
        handles,
      }),
    )
  }
  async evaluateHandle<A>(
    fn: ((value: UntrustedInput, arg: A) => UntrustedInput | Promise<UntrustedInput>) | string,
    arg?: A,
  ): Promise<NativeWorkerHandle> {
    if (arguments.length > 2)
      throw new Error('Too many arguments; pass arguments as a single object')
    this.assertUsable(this.worker)
    const { encoded, handles } = workerArgument(this.worker, arg as UntrustedInput)
    const invocation =
      typeof fn === 'string'
        ? `(0,eval)(${JSON.stringify(fn)})`
        : `(${functionBody(fn)}).call(globalThis,this.value,codec.decode(${JSON.stringify(encoded)},values))`
    const declaration = `async function(...handles){const codec=(${createNativeValueCodec.toString()})();const values=handles.map(h=>h.value);return {value:await (${invocation})};}`
    return this.worker.adoptHandle(
      await this.worker.runHandle({
        action: 'create',
        handle: this.id,
        function: declaration,
        handles,
      }),
    )
  }
  jsonValue(): Promise<UntrustedInput> {
    return this.evaluate((value) => value)
  }
  async getProperty(name: string): Promise<NativeWorkerHandle> {
    try {
      this.assertUsable(this.worker)
      return this.worker.adoptHandle(
        await this.worker.runHandle({ action: 'property', handle: this.id, name }),
      )
    } catch (error) {
      throw new Error(
        `jsHandle.getProperty: ${error instanceof Error ? error.message : String(error)}`,
        { cause: error },
      )
    }
  }
  async getProperties(): Promise<Map<string, NativeWorkerHandle>> {
    const names = await this.evaluate((value) => {
      if (value === null || (typeof value !== 'object' && typeof value !== 'function')) return []
      const names = Object.keys(value)
      if (names.length > 1024) throw new Error('Native worker property capacity exceeded')
      return names
    })
    const properties = new Map<string, NativeWorkerHandle>()
    try {
      for (const name of names) properties.set(name, await this.getProperty(name))
      return properties
    } catch (error) {
      await Promise.allSettled([...properties.values()].map((handle) => handle.dispose()))
      throw error
    }
  }
  async dispose(): Promise<void> {
    if (this.disposed) return
    if (this.worker.captureChannelEpoch() === this.channelEpoch) {
      this.worker.assertNativeControl(this.nativeControl)
      await this.worker.disposeHandle(this.id)
    }
    this.disposed = true
  }
}

const controllers = new WeakMap<NativeWorkerBrowser, NativeWorkerEvents>()
/** Discovery metadata is retained by the native transport even before listeners. */
export class NativeWorkerEvents {
  private readonly workers = new Map<string, NativeWorker>()
  private readonly closedWorkers = new Map<string, WeakRef<NativeWorker>>()
  private readonly observedWorkers = new Map<string, WeakRef<NativeWorker>>()
  private readonly listeners: ((worker: NativeWorker) => void)[] = []
  private readonly retirements = new NativeWorkerRetirements()
  private physicallyClosed = false
  private logicallyAborted = false
  private detachWorker: (() => void) | undefined
  private receiveWorker: ((event: NativeWorkerEvent) => void) | undefined
  private constructor(private readonly owner: NativeWorkerOwner) {
    const source = owner.source
    if (!source.onWorkerMessage || !source.workerInfos)
      throw new Error('Native worker events are unavailable on this source')
    this.retirements.restore(source.workerRetirements?.())
    this.physicallyClosed = source.processExit?.() !== undefined
    if (this.physicallyClosed) return
    for (const info of source.workerInfos()) this.adopt(info)
    const receive = (value: NativeWorkerEvent) => {
      const event = NativeWorkerEventSchema.parse(value)
      const retained = this.known(event.worker.id)
      if (
        retained &&
        (!retained.acceptsLifecycleReceipt(event.sequence) ||
          (retained.isClosed() && event.event !== 'close'))
      )
        return
      if (!this.retirements.observe('', event)) return
      if (event.event === 'channelclosed') {
        const worker =
          this.workers.get(event.worker.id) ?? this.observedWorkers.get(event.worker.id)?.deref()
        if (worker) this.retainObserved(worker)
        this.workers.delete(event.worker.id)
        worker?.channelClosedFromNative(
          event.reason ?? 'Native worker control channel closed',
          event.sequence,
        )
        if (worker) this.rememberClosed(worker)
        this.detachSettledLifecycle()
        return
      }
      if (event.event === 'updated') {
        const worker =
          this.workers.get(event.worker.id) ?? this.observedWorkers.get(event.worker.id)?.deref()
        if (worker) {
          worker.info.url = event.worker.url
          worker.observeLifecycleReceipt(event.sequence)
        }
        return
      }
      if (event.event === 'close') {
        const worker =
          this.workers.get(event.worker.id) ??
          this.observedWorkers.get(event.worker.id)?.deref() ??
          this.closedWorkers.get(event.worker.id)?.deref()
        this.workers.delete(event.worker.id)
        this.observedWorkers.delete(event.worker.id)
        worker?.closeFromNative(event.sequence)
        if (worker) this.rememberClosed(worker)
        this.detachSettledLifecycle()
        return
      }
      const worker = this.adopt(event.worker, event.sequence)
      if (worker.isClosed()) return
      if (worker.info.type === 'worker')
        for (const listener of [...this.listeners]) listener(worker)
    }
    this.receiveWorker = receive
    this.attachLifecycle()
    const detachConsole = source.processSignal?.aborted
      ? undefined
      : source.onConsoleMessage?.((info) => {
          if (!info.workerInfo || info.workerInfo.id !== info.worker) return
          this.fromConsole(info.workerInfo).consoleFromNative(this.owner.createConsole(info))
        })
    const logicalAbort = () => {
      detachConsole?.()
      this.listeners.length = 0
      this.logicallyAborted = true
      this.detachSettledLifecycle()
    }
    const lifetime = source.pageSignal ?? source.signal
    lifetime.addEventListener('abort', logicalAbort, { once: true })
    if (lifetime.aborted) logicalAbort()
    // Promise callbacks must not keep an otherwise unreachable context alive.
    const weak = new WeakRef(this)
    void source.processClosed?.then(() => weak.deref()?.processClosedFromNative())
  }
  private attachLifecycle(): void {
    if (this.detachWorker || this.physicallyClosed || this.owner.source.processSignal?.aborted)
      return
    if (this.receiveWorker)
      this.detachWorker = this.owner.source.onWorkerMessage?.(this.receiveWorker)
  }
  private retainObserved(worker: NativeWorker): void {
    for (const [id, reference] of this.observedWorkers)
      if (!reference.deref()) this.observedWorkers.delete(id)
    if (!this.observedWorkers.has(worker.info.id) && this.observedWorkers.size >= 1024)
      throw new Error('Native observed worker identity capacity exceeded')
    this.attachLifecycle()
    this.observedWorkers.set(worker.info.id, new WeakRef(worker))
  }
  private rememberClosed(worker: NativeWorker): void {
    this.closedWorkers.set(worker.info.id, new WeakRef(worker))
    while (this.closedWorkers.size > 1024) {
      const oldest = this.closedWorkers.keys().next().value
      if (oldest === undefined) break
      this.closedWorkers.delete(oldest)
    }
  }
  private detachSettledLifecycle(): void {
    if (!this.logicallyAborted) return
    for (const [id, reference] of this.observedWorkers)
      if (!reference.deref()) this.observedWorkers.delete(id)
    if (this.workers.size || this.observedWorkers.size) return
    for (const reference of this.closedWorkers.values())
      if (reference.deref()?.isClosed() === false) return
    this.detachWorker?.()
    this.detachWorker = undefined
  }
  private processClosedFromNative(): void {
    // This receipt is actual process+stdio closure, never logical signal abort.
    this.physicallyClosed = true
    this.retirements.clear()
    let callbackError: Error | undefined
    const close = (worker: NativeWorker | undefined) => {
      if (!worker) return
      try {
        worker.closeFromNative()
      } catch (error) {
        callbackError ??=
          error instanceof Error
            ? error
            : new Error('Native worker close listener failed', { cause: error })
      }
      this.rememberClosed(worker)
    }
    for (const worker of this.workers.values()) close(worker)
    for (const reference of this.observedWorkers.values()) close(reference.deref())
    for (const reference of this.closedWorkers.values()) close(reference.deref())
    this.workers.clear()
    this.observedWorkers.clear()
    this.listeners.length = 0
    this.detachWorker?.()
    this.detachWorker = undefined
    // Preserve the ordinary uncaught user-listener exception, after settling
    // every physical target instead of leaving later identities live.
    if (callbackError) {
      const error = callbackError
      queueMicrotask(() => {
        throw error
      })
    }
  }
  static forOwner(owner: NativeWorkerOwner): NativeWorkerEvents {
    let events = controllers.get(owner.source)
    if (!events) {
      events = new NativeWorkerEvents(owner)
      controllers.set(owner.source, events)
    }
    return events
  }
  private known(id: string): NativeWorker | undefined {
    return (
      this.workers.get(id) ??
      this.observedWorkers.get(id)?.deref() ??
      this.closedWorkers.get(id)?.deref()
    )
  }
  private adopt(value: UntrustedInput, actualReadySequence?: number): NativeWorker {
    const info = NativeWorkerInfoSchema.parse(value)
    let worker = this.workers.get(info.id)
    if (!worker) {
      worker =
        this.observedWorkers.get(info.id)?.deref() ??
        this.closedWorkers.get(info.id)?.deref() ??
        new NativeWorker(this.owner, info)
      if (worker.isClosed()) return worker
      worker.info.url = info.url
      this.observedWorkers.delete(info.id)
      this.workers.set(info.id, worker)
      this.closedWorkers.delete(info.id)
    }
    worker.info.url = info.url
    const sequence = actualReadySequence ?? this.retirements.readySequence(info.id)
    if (sequence !== undefined) worker.readyFromNative(sequence, info.control)
    return worker
  }
  fromRecord(value: UntrustedInput): NativeWorker {
    const record = NativeWorkerEventSchema.parse(value)
    const info = record.worker
    const retained = this.known(info.id)
    if (
      retained &&
      (!retained.acceptsLifecycleReceipt(record.sequence) ||
        (retained.isClosed() && record.event !== 'close'))
    )
      return retained
    if (!this.detachWorker && !this.physicallyClosed)
      this.retirements.restore(this.owner.source.workerRetirements?.())
    if (record.event === 'close' || record.event === 'channelclosed') {
      if (
        !retained &&
        this.retirements.truncated &&
        this.retirements.latestSequence(info.id) === undefined
      )
        throw new Error(
          'Native worker retirement history capacity exceeded; late target identity is unavailable',
        )
      this.receiveWorker?.(record)
      return this.fromObservedInfo(info)
    }
    if (this.physicallyClosed) return this.fromObservedInfo(info)
    if (record.event === 'worker') {
      if (
        !retained &&
        this.retirements.truncated &&
        this.retirements.latestSequence(info.id) === undefined
      )
        throw new Error(
          'Native worker retirement history capacity exceeded; late target identity is unavailable',
        )
      if (!this.retirements.observe('', record)) return this.fromObservedInfo(info)
      return this.adopt(info, record.sequence)
    }
    if (this.retirements.get(info.id)) return this.fromObservedInfo(info)
    const active = this.workers.get(info.id)
    if (active) return active
    const closed = this.closedWorkers.get(info.id)?.deref()
    if (closed) return closed
    const current = this.owner.source.workerInfos?.().find((target) => target.id === info.id)
    if (current) return this.adopt(current)
    return this.fromObservedInfo(info)
  }
  /** Native request metadata is observation, never a discovery or close event. */
  fromObservedInfo(value: UntrustedInput): NativeWorker {
    const info = NativeWorkerInfoSchema.parse(value)
    if (!this.detachWorker && !this.physicallyClosed)
      this.retirements.restore(this.owner.source.workerRetirements?.())
    const active = this.workers.get(info.id)
    if (active) return active
    const closed = this.closedWorkers.get(info.id)?.deref()
    if (closed) return closed
    const current = this.physicallyClosed
      ? undefined
      : this.owner.source.workerInfos?.().find((target) => target.id === info.id)
    if (current) return this.adopt(current)
    const retained = this.observedWorkers.get(info.id)?.deref()
    if (retained) {
      retained.info.url = info.url
      return retained
    }
    const receipt = this.retirements.get(info.id)
    if (this.physicallyClosed || receipt) {
      if (receipt && receipt.type !== info.type)
        throw new Error('Native worker retirement identity is inconsistent')
      const worker = new NativeWorker(this.owner, info)
      if (this.physicallyClosed || receipt?.event === 'close')
        worker.closeFromNative(receipt?.sequence)
      else {
        worker.channelClosedFromNative(
          receipt?.reason ?? 'Native worker control channel closed',
          receipt?.sequence,
        )
        this.retainObserved(worker)
      }
      this.rememberClosed(worker)
      return worker
    }
    if (this.retirements.truncated)
      throw new Error(
        'Native worker retirement history capacity exceeded; late target identity is unavailable',
      )
    for (const [id, reference] of this.observedWorkers)
      if (!reference.deref()) this.observedWorkers.delete(id)
    // A retained observation must keep receiving genuine lifecycle events.
    // Do not evict a live object merely to admit a new target.
    if (this.observedWorkers.size >= 1024)
      throw new Error('Native observed worker identity capacity exceeded')
    const worker = new NativeWorker(this.owner, info)
    this.retainObserved(worker)
    return worker
  }
  fromConsole(info: UntrustedInput): NativeWorker {
    return this.fromObservedInfo(info)
  }
  list(): NativeWorker[] {
    const published = new Set(this.owner.source.workerInfos?.().map((worker) => worker.id))
    return [...this.workers.values()].filter(
      (worker) => worker.info.type === 'worker' && published.has(worker.info.id),
    )
  }
  allTargets(): NativeWorker[] {
    return [...this.workers.values()]
  }
  on(listener: (worker: NativeWorker) => void, prepend = false): void {
    this.owner.source.signal.throwIfAborted()
    if (prepend) this.listeners.unshift(listener)
    else this.listeners.push(listener)
  }
  off(listener: (worker: NativeWorker) => void): void {
    const index = this.listeners.lastIndexOf(listener)
    if (index >= 0) this.listeners.splice(index, 1)
  }
}
