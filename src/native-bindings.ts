// SPDX-License-Identifier: MIT
/* eslint import-x/consistent-type-specifier-style: off -- Whole type imports erase lifecycle module edges. */
import * as z from 'zod'

import { NativeFrameInfoSchema, type NativeFrameInfo } from './native-protocol.js'
import { decodeNativeBindingName } from './native-string-wire.js'
import { createNativeValueCodec } from './native-values.js'

import type { NativeBrowser } from './native-browser.js'
import type { NativeBrowserContext } from './native-context.js'
import type { NativeFrame, NativeJSHandle, NativePage } from './native-page.js'
import type { UntrustedInput } from './untrusted.js'

// One physical ledger for all native helpers and module copies in this SDK
// process. Closing a page removes logical ownership, never pending host work.
const hostOwnershipKey = Symbol.for('suitecut.nativeBindingPhysicalOwnership.v1')
const existingHostOwnership = Reflect.get(globalThis, hostOwnershipKey) as UntrustedInput
if (existingHostOwnership !== undefined && !(existingHostOwnership instanceof Map))
  throw new Error('Native binding physical ownership registry is invalid')
const hostPhysicalSlots =
  existingHostOwnership === undefined
    ? new Map<object, number>()
    : (existingHostOwnership as Map<object, number>)
if (existingHostOwnership === undefined)
  Object.defineProperty(globalThis, hostOwnershipKey, { value: hostPhysicalSlots })
function hostPhysicalBytes(): number {
  let bytes = 0
  for (const slot of hostPhysicalSlots.values()) {
    if (!Number.isSafeInteger(slot) || slot < 0)
      throw new Error('Native binding physical ownership entry is invalid')
    bytes += slot
  }
  return bytes
}

const NativeBindingWireSchema = z.discriminatedUnion('event', [
  z.object({
    event: z.literal('call'),
    id: z.string().min(1).max(96),
    name: z.string().min(1).max(256).optional(),
    nameJSON: z.string().max(1538).optional(),
    frame: NativeFrameInfoSchema,
    args: z.unknown(),
    handle: z.boolean().optional(),
  }),
  z.object({
    event: z.literal('canceled'),
    id: z.string().min(1).max(96),
    error: z.string().max(4096),
  }),
])
export const NativeBindingEventSchema = NativeBindingWireSchema.transform((event, context) => {
  if (event.event !== 'call') return event
  try {
    return { ...event, name: decodeNativeBindingName(event) }
  } catch {
    context.addIssue({ code: 'custom', message: 'Invalid native binding name wire identity' })
    return z.NEVER
  }
})
export type NativeBindingEvent = z.infer<typeof NativeBindingEventSchema>
export interface NativeBindingSource {
  context: NativeBrowserContext
  page: NativePage
  frame: NativeFrame
}
export interface NativeExposedBinding {
  dispose(): Promise<void>
}
export type NativeBindingCallback = (...args: never[]) => UntrustedInput | Promise<UntrustedInput>
export interface NativeBindingTransport {
  readonly signal: AbortSignal
  readonly pageSignal?: AbortSignal
  subscribeCrash?: ((listener: () => void) => () => void) | undefined
  run(params: Record<string, UntrustedInput>, signal?: AbortSignal): Promise<UntrustedInput>
  subscribe(listener: (event: NativeBindingEvent) => void): () => void
}

/** Serialized renderer installer. All call admission and deadlines remain native-owned. */
function installNativeBinding(
  name: string,
  bridge: string,
  stateKey: string,
  codecFactory: typeof createNativeValueCodec,
  handleArgument = false,
  allowDeferred = false,
): void {
  const existing = Reflect.get(globalThis, stateKey) as UntrustedInput
  if (existing !== undefined) {
    if (!existing || typeof existing !== 'object')
      throw new Error('Native binding renderer state identity changed')
    const connect = Reflect.get(existing, 'connect') as UntrustedInput
    if (
      Reflect.get(existing, 'name') !== name ||
      Reflect.get(existing, 'bridge') !== bridge ||
      Reflect.get(existing, 'handleArgument') !== handleArgument ||
      typeof connect !== 'function'
    )
      throw new Error('Native binding renderer state identity changed')
    const candidate = Reflect.get(globalThis, bridge) as UntrustedInput
    if (!allowDeferred) {
      if (typeof candidate !== 'function') throw new Error('Native binding bridge is unavailable')
      Reflect.apply(connect, existing, [candidate])
    }
    return
  }
  if (Reflect.get(globalThis, name) !== undefined)
    throw new Error(`Function "${name}" already exists in the browser document`)
  let raw = Reflect.get(globalThis, bridge) as UntrustedInput
  if (typeof raw !== 'function' && !allowDeferred)
    throw new Error('Native binding bridge is unavailable')
  // Context snapshots install before native default-context bookkeeping. Only
  // the later native installer may connect their early author calls.
  if (allowDeferred) raw = undefined
  const queueKey = Symbol.for('suitecut.nativeBindingDeferredOwnership.v1')
  const previousQueue = Reflect.get(globalThis, queueKey) as UntrustedInput
  if (previousQueue !== undefined && !(previousQueue instanceof Map))
    throw new Error('Native binding deferred ownership registry is invalid')
  const queued =
    previousQueue === undefined ? new Map<object, number>() : (previousQueue as Map<object, number>)
  if (previousQueue === undefined) Object.defineProperty(globalThis, queueKey, { value: queued })
  const queuedBytes = () => {
    let bytes = 0
    for (const value of queued.values()) {
      if (!Number.isSafeInteger(value) || value < 0)
        throw new Error('Native binding deferred ownership entry is invalid')
      bytes += value
    }
    return bytes
  }
  const codec = codecFactory()
  const nonceBytes = new Uint8Array(16)
  crypto.getRandomValues(nonceBytes)
  let contextNonce = ''
  for (const byte of nonceBytes) contextNonce += byte.toString(16).padStart(2, '0')
  const pending = new Map<
    number,
    {
      resolve(value: UntrustedInput): void
      reject(value: UntrustedInput): void
      argument: UntrustedInput
      payload: string
      deadline: number
      timer: ReturnType<typeof setTimeout> | undefined
      queueToken: object
    }
  >()
  let sequence = 0
  let disposed = false
  const now = performance.now.bind(performance)
  const clearCall = (id: number) => {
    const call = pending.get(id)
    if (!call) return undefined
    pending.delete(id)
    queued.delete(call.queueToken)
    if (call.timer !== undefined) clearTimeout(call.timer)
    call.payload = ''
    return call
  }
  const timeout = () => {
    const error = new Error('Native binding callback timed out')
    error.name = 'TimeoutError'
    return error
  }
  const state = {
    name,
    bridge,
    handleArgument,
    connect(candidate: UntrustedInput) {
      if (disposed) throw new Error('Native binding was disposed')
      if (typeof candidate !== 'function') throw new Error('Native binding bridge is unavailable')
      raw = candidate
      for (const [id, call] of pending) {
        if (!call.payload) continue
        const remaining = Math.ceil(call.deadline - now())
        queued.delete(call.queueToken)
        if (remaining <= 0) {
          clearCall(id)?.reject(timeout())
          continue
        }
        try {
          const data = JSON.parse(call.payload) as {
            contextNonce: string
            id: number
            args: UntrustedInput
          }
          const payload = JSON.stringify({ ...data, timeoutMillis: Math.min(30000, remaining) })
          call.payload = ''
          Reflect.apply(candidate, globalThis, [payload])
        } catch (error) {
          clearCall(id)?.reject(error as UntrustedInput)
        }
      }
    },
    argument(id: number, expectedNonce: string): UntrustedInput {
      if (expectedNonce !== contextNonce) throw new Error('Native binding argument context changed')
      const call = pending.get(id)
      if (!call) throw new Error('Native binding argument call is no longer active')
      return call.argument
    },
    settle(
      id: number,
      ok: boolean,
      encoded: UntrustedInput,
      handles: UntrustedInput[] = [],
      expectedNonce?: string,
    ) {
      if (expectedNonce !== contextNonce) return
      const call = clearCall(id)
      if (!call) return
      try {
        const value = codec.decode(encoded, handles)
        if (ok) call.resolve(value)
        else call.reject(value)
      } catch (error) {
        call.reject(error as UntrustedInput)
      }
    },
    dispose() {
      disposed = true
      // Old native cleanup can arrive after another registration uses this
      // public name. Only this exact state's wrapper and state are disposable.
      if ((Object.getOwnPropertyDescriptor(globalThis, name)?.value as UntrustedInput) === wrapper)
        Reflect.deleteProperty(globalThis, name)
      if (
        (Object.getOwnPropertyDescriptor(globalThis, stateKey)?.value as UntrustedInput) === state
      )
        Reflect.deleteProperty(globalThis, stateKey)
      for (const id of pending.keys())
        clearCall(id)?.reject(new Error('Native binding was disposed'))
    },
  }
  Object.defineProperty(globalThis, stateKey, { configurable: true, value: state })
  const wrapper = (...args: UntrustedInput[]) =>
    new Promise<UntrustedInput>((resolve, reject) => {
      if (disposed) {
        reject(new Error('Native binding was disposed'))
        return
      }
      if (pending.size >= 128) {
        reject(new Error('Native binding renderer call capacity exceeded'))
        return
      }
      if (++sequence > Number.MAX_SAFE_INTEGER) {
        reject(new Error('Native binding call sequence exhausted'))
        return
      }
      try {
        if (handleArgument && args.length > 1)
          throw new Error('Native binding handle mode supports only one argument')
        const encoded = codec.encode(handleArgument ? [] : args)
        const payload = JSON.stringify({
          contextNonce,
          id: sequence,
          args: encoded,
          ...(allowDeferred ? { timeoutMillis: 30000 } : {}),
        })
        const parsed = JSON.parse(payload) as { args?: UntrustedInput }
        if (!Array.isArray(parsed.args))
          throw new Error(
            'serializedArgs is not an array. This can happen when Array.prototype.toJSON is defined incorrectly',
          )
        const bytes = new TextEncoder().encode(payload).byteLength
        if (bytes > 1024 * 1024) throw new Error('Native binding arguments exceed 1 MiB')
        const deferred = typeof raw !== 'function'
        if (deferred && (queued.size >= 128 || queuedBytes() + bytes > 8 * 1024 * 1024))
          throw new Error('Native binding deferred call capacity exceeded')
        const id = sequence,
          queueToken = {}
        const deadline = now() + 30000
        const timer = allowDeferred
          ? setTimeout(() => clearCall(id)?.reject(timeout()), 30000)
          : undefined
        pending.set(id, {
          resolve,
          reject,
          argument: handleArgument ? args[0] : undefined,
          payload: deferred ? payload : '',
          deadline,
          timer,
          queueToken,
        })
        if (deferred) queued.set(queueToken, bytes)
        else Reflect.apply(raw as (payload: string) => void, globalThis, [payload])
      } catch (error) {
        clearCall(sequence)
        // Host code may throw null or another non-Error value; preserve it.
        // eslint-disable-next-line @typescript-eslint/prefer-promise-reject-errors
        reject(error)
      }
    })
  Object.defineProperty(globalThis, name, { configurable: true, value: wrapper })
}

/** Native context snapshots use the same authored installer and codec as pages. */
export function nativeBindingBootstrapSource(): { installer: string; codec: string } {
  return { installer: installNativeBinding.toString(), codec: createNativeValueCodec.toString() }
}

/** Page-scoped host callbacks with separate logical and physical ownership bounds. */
export class NativeBindings {
  private static readonly controllers = new WeakMap<NativeBrowser, NativeBindings>()
  /** Page signal projections share the canonical physical source and subscriber. */
  static forSource(source: NativeBrowser, factory: () => NativeBindings): NativeBindings {
    source.signal.throwIfAborted()
    const existing = this.controllers.get(source)
    if (existing) return existing
    const controller = factory()
    this.controllers.set(source, controller)
    return controller
  }
  private readonly registrations = new Map<
    string,
    { callback: NativeBindingCallback; binding: boolean; handle: boolean; inherited: boolean }
  >()
  private readonly logical = new Map<string, AbortController>()
  private readonly physical = new Map<string, number>()
  private physicalBytes = 0
  private replyActive = 0
  private readonly replyQueue: (() => void)[] = []
  private readonly unsubscribe: () => void
  constructor(
    private readonly transport: NativeBindingTransport,
    private readonly source: (frame: NativeFrameInfo) => NativeBindingSource,
    private readonly handleIndex: (value: UntrustedInput) => number | undefined,
    private readonly retainedArgument?: (
      frame: NativeFrameInfo,
      value: UntrustedInput,
    ) => NativeJSHandle,
  ) {
    this.unsubscribe = transport.subscribe((event) => {
      if (event.event === 'canceled') {
        this.logical.get(event.id)?.abort(new Error(event.error))
        this.logical.delete(event.id)
      } else this.dispatch(event)
    })
    const cancelCalls = (): void => {
      for (const token of this.logical.values()) token.abort(transport.signal.reason)
      this.logical.clear()
    }
    const detachCrash = transport.subscribeCrash?.(cancelCalls)
    const lifetime = transport.pageSignal ?? transport.signal
    lifetime.addEventListener(
      'abort',
      () => {
        cancelCalls()
        this.registrations.clear()
        this.unsubscribe()
        detachCrash?.()
        // Unresolved user promises retain their physical admission slots until
        // settling. Logical cancellation cannot admit an unbounded callback heap.
      },
      { once: true },
    )
  }
  async expose(
    name: string,
    callback: NativeBindingCallback,
    binding = false,
    handle = false,
  ): Promise<NativeExposedBinding> {
    this.transport.signal.throwIfAborted()
    z.string().min(1).max(256).parse(name)
    if (typeof callback !== 'function')
      throw new Error('Native binding callback must be a function')
    if (this.registrations.has(name))
      throw new Error(`page.exposeFunction: Function "${name}" has been already registered`)
    if (this.registrations.size >= 64)
      throw new Error('Native binding registration capacity exceeded')
    if (handle && (!binding || !this.retainedArgument))
      throw new Error('Native binding handle argument creator is unavailable')
    const registration = { callback, binding, handle, inherited: false }
    this.registrations.set(name, registration)
    try {
      await this.transport.run({
        action: 'register',
        name,
        installer: installNativeBinding.toString(),
        codec: createNativeValueCodec.toString(),
        handle,
      })
    } catch (error) {
      if (this.registrations.get(name) === registration) this.registrations.delete(name)
      throw error
    }
    let disposed = false
    return {
      dispose: async () => {
        if (disposed) return
        await this.transport.run({ action: 'remove', name })
        if (this.registrations.get(name) === registration) this.registrations.delete(name)
        disposed = true
      },
    }
  }
  /** Host adoption precedes inherited native bridge resume; it sends no duplicate registration. */
  adoptInherited(
    name: string,
    callback: NativeBindingCallback,
    binding = false,
    handle = false,
  ): void {
    this.transport.signal.throwIfAborted()
    z.string().min(1).max(256).parse(name)
    z.boolean().parse(binding)
    z.boolean().parse(handle)
    if (typeof callback !== 'function')
      throw new Error('Native binding callback must be a function')
    if (handle && (!binding || !this.retainedArgument))
      throw new Error('Native binding handle argument creator is unavailable')
    const existing = this.registrations.get(name)
    if (existing) {
      if (
        existing.inherited &&
        existing.callback === callback &&
        existing.binding === binding &&
        existing.handle === handle
      )
        return
      throw new Error(`Function "${name}" has been already registered`)
    }
    if (this.registrations.size >= 64)
      throw new Error('Native binding registration capacity exceeded')
    this.registrations.set(name, { callback, binding, handle, inherited: true })
  }
  /** Native context removal owns browser disposal; physical user work remains admitted until settlement. */
  dropInherited(name: string): void {
    if (this.registrations.get(name)?.inherited) this.registrations.delete(name)
  }
  diagnostics(): {
    logical: number
    physical: number
    physicalBytes: number
    registrations: number
  } {
    return {
      logical: this.logical.size,
      physical: this.physical.size,
      physicalBytes: this.physicalBytes,
      registrations: this.registrations.size,
    }
  }
  static hostDiagnostics(): { physical: number; physicalBytes: number } {
    return { physical: hostPhysicalSlots.size, physicalBytes: hostPhysicalBytes() }
  }
  private dispatch(event: Extract<NativeBindingEvent, { event: 'call' }>): void {
    const registration = this.registrations.get(event.name)
    const bytes = Buffer.byteLength(JSON.stringify(event.args), 'utf8')
    if (
      !registration ||
      this.physical.has(event.id) ||
      hostPhysicalSlots.size >= 128 ||
      hostPhysicalBytes() + bytes > 8 * 1024 * 1024
    ) {
      void this.reply(event.id, false, new Error('Native host binding callback capacity exceeded'))
      return
    }
    const token = new AbortController()
    this.logical.set(event.id, token)
    this.physical.set(event.id, bytes)
    hostPhysicalSlots.set(token, bytes)
    this.physicalBytes += bytes
    const run = async () => {
      try {
        if ((event.handle ?? false) !== registration.handle)
          throw new Error('Native binding argument mode does not match its registration')
        const args = z
          .array(z.unknown())
          .max(100000)
          .parse(createNativeValueCodec().decode(event.args as UntrustedInput))
        if (registration.handle) {
          const value = await this.sendReplyControl(
            { action: 'argument', bindingCallId: event.id },
            token.signal,
          )
          if (!this.retainedArgument)
            throw new Error('Native binding handle argument creator is unavailable')
          const handle = this.retainedArgument(event.frame, value)
          if (this.logical.get(event.id) !== token || this.transport.signal.aborted) {
            await handle.dispose().catch(() => undefined)
            return
          }
          args[0] = handle
        }
        if (registration.binding) args.unshift(this.source(event.frame))
        let value: UntrustedInput
        let success: boolean
        try {
          value = (await Reflect.apply(registration.callback, undefined, args)) as UntrustedInput
          success = true
        } catch (error) {
          value = error as UntrustedInput
          success = false
        }
        if (this.logical.get(event.id) === token && !this.transport.signal.aborted)
          await this.reply(event.id, success, value)
      } catch (error) {
        if (this.logical.get(event.id) === token && !this.transport.signal.aborted)
          await this.reply(event.id, false, error as UntrustedInput)
      } finally {
        if (this.logical.get(event.id) === token) this.logical.delete(event.id)
        this.physicalBytes -= this.physical.get(event.id) ?? 0
        this.physical.delete(event.id)
        hostPhysicalSlots.delete(token)
      }
    }
    void run()
  }
  private async sendReplyControl(
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput> {
    const ownerSignal = signal
      ? AbortSignal.any([this.transport.signal, signal])
      : this.transport.signal
    ownerSignal.throwIfAborted()
    if (this.replyActive >= 64) {
      if (this.replyQueue.length >= 128)
        throw new Error('Native binding acknowledgment queue capacity exceeded')
      await new Promise<void>((resolve, reject) => {
        const admit = () => {
          cleanup()
          resolve()
        }
        const abort = () => {
          const index = this.replyQueue.indexOf(admit)
          if (index >= 0) this.replyQueue.splice(index, 1)
          cleanup()
          reject(
            ownerSignal.reason instanceof Error
              ? ownerSignal.reason
              : new Error('Native binding argument operation was canceled'),
          )
        }
        const cleanup = () => ownerSignal.removeEventListener('abort', abort)
        this.replyQueue.push(admit)
        ownerSignal.addEventListener('abort', abort, { once: true })
        if (ownerSignal.aborted) abort()
      })
    } else this.replyActive++
    try {
      ownerSignal.throwIfAborted()
      return await this.transport.run(params, ownerSignal)
    } finally {
      const next = this.replyQueue.shift()
      if (next) next()
      else this.replyActive--
    }
  }
  private async reply(id: string, success: boolean, value: UntrustedInput): Promise<void> {
    const handles: number[] = []
    let encoded: object
    try {
      encoded = createNativeValueCodec().encode(value, 'result', (item) => {
        const native = this.handleIndex(item)
        if (native === undefined) return undefined
        handles[handles.length] = native
        return handles.length - 1
      })
    } catch (error) {
      encoded = createNativeValueCodec().encode(error as UntrustedInput)
      handles.length = 0
      success = false
    }
    let serialized = Buffer.from(JSON.stringify(encoded), 'utf8')
    if (serialized.length > 1024 * 1024) {
      encoded = createNativeValueCodec().encode(
        new Error('Native binding callback result exceeds 1 MiB'),
      )
      serialized = Buffer.from(JSON.stringify(encoded), 'utf8')
      success = false
      handles.length = 0
    }
    try {
      if (serialized.length <= 24 * 1024) {
        await this.sendReplyControl({
          action: 'resolve',
          bindingCallId: id,
          success,
          value: encoded,
          handles,
        })
      } else {
        for (let offset = 0, index = 0; offset < serialized.length; offset += 24 * 1024, index++) {
          await this.sendReplyControl({
            action: 'stage',
            bindingCallId: id,
            index,
            data: serialized.subarray(offset, offset + 24 * 1024).toString('base64'),
            final: offset + 24 * 1024 >= serialized.length,
          })
        }
        await this.sendReplyControl({
          action: 'resolve',
          bindingCallId: id,
          success,
          staged: true,
          handles,
        })
      }
    } catch (error) {
      if (!this.transport.signal.aborted) {
        try {
          await this.sendReplyControl({
            action: 'resolve',
            bindingCallId: id,
            success: false,
            value: createNativeValueCodec().encode(error as UntrustedInput),
            handles: [],
          })
        } catch {
          /* The native call deadline/context cancellation owns settlement. */
        }
      }
    }
  }
}
