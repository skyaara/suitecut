import { randomUUID } from 'node:crypto'
import { isIP } from 'node:net'

import * as z from 'zod'

import {
  bindNativeRetryContinuation,
  withoutNativeRetryOperation,
} from './native-callback-operations.js'
import {
  NativeHTTPLookupEventSchema,
  type NativeHTTPLookupEvent,
} from './native-http-lookup-protocol.js'
import { runNativeMatcherUserPromise } from './native-soft-assertions.js'
import { type UntrustedInput } from './untrusted.js'

export interface NativeHTTPLookupAddress {
  address: string
  family: 4 | 6
}
export type NativeHTTPLookup = (
  hostname: string,
  options: { signal: AbortSignal },
) => readonly NativeHTTPLookupAddress[] | Promise<readonly NativeHTTPLookupAddress[]>
export interface NativeHTTPLookupTransport {
  readonly processSignal?: AbortSignal | undefined
  readonly signal: AbortSignal
  onHTTPLookupMessage?:
    ((listener: (event: NativeHTTPLookupEvent) => void) => () => void) | undefined
  resolveLookup(
    event: NativeHTTPLookupEvent,
    addresses: readonly NativeHTTPLookupAddress[],
    error: string,
  ): Promise<UntrustedInput>
}
const Addresses = z
  .array(
    z
      .strictObject({
        address: z.string().min(1).max(64),
        family: z.union([z.literal(4), z.literal(6)]),
      })
      .refine(
        (value) => isIP(value.address) === value.family,
        'Invalid native HTTP lookup IP address',
      ),
  )
  .min(1)
  .max(16)
interface PhysicalCall {
  active: boolean
  readonly abort: AbortController
}
// One process-wide physical callback ledger. Logical native cancellation does
// not free a slot occupied by a host promise that has not actually settled.
const physicalCallbacks = new Set<PhysicalCall>()
const controllers = new WeakMap<AbortSignal, NativeHTTPLookups>()
export class NativeHTTPLookups {
  private readonly registered = new Map<
    string,
    {
      readonly callback: NativeHTTPLookup
      readonly run: (task: () => Promise<void>) => Promise<void>
    }
  >()
  private readonly physical = new Map<string, PhysicalCall>()
  private closed = false
  private unsubscribe: (() => void) | undefined
  static forTransport(source: NativeHTTPLookupTransport): NativeHTTPLookups {
    const signal = source.processSignal ?? source.signal
    let controller = controllers.get(signal)
    if (!controller) {
      controller = new NativeHTTPLookups(source)
      controllers.set(signal, controller)
    }
    return controller
  }
  private constructor(private readonly source: NativeHTTPLookupTransport) {
    if (!source.onHTTPLookupMessage)
      throw new Error('Native HTTP custom lookup transport is unavailable')
    const signal = source.processSignal ?? source.signal
    this.unsubscribe = source.onHTTPLookupMessage((event) => this.receive(event))
    const close = () => {
      if (this.closed) return
      this.closed = true
      this.unsubscribe?.()
      this.unsubscribe = undefined
      this.registered.clear()
      for (const call of this.physical.values()) {
        call.active = false
        call.abort.abort(signal.reason)
      }
    }
    signal.addEventListener('abort', close, { once: true })
    if (signal.aborted) close()
  }
  async withLookup<T>(callback: NativeHTTPLookup, run: (key: string) => Promise<T>): Promise<T> {
    if (this.closed) throw new Error('Native HTTP lookup process has closed')
    if (this.registered.size >= 128)
      throw new Error('Native HTTP lookup registration capacity exceeded')
    const key = randomUUID()
    this.registered.set(key, {
      callback,
      run: bindNativeRetryContinuation((task: () => Promise<void>) => task()),
    })
    try {
      return await run(key)
    } finally {
      this.registered.delete(key)
    }
  }
  private receive(input: NativeHTTPLookupEvent): void {
    const event = NativeHTTPLookupEventSchema.parse(input)
    const id = `${event.owner}:${event.generation}`
    if (event.event === 'canceled') {
      const call = this.physical.get(id)
      if (call) {
        call.active = false
        call.abort.abort(new Error('Native HTTP original lookup operation canceled'))
      }
      return
    }
    if (this.closed || this.physical.has(id)) return
    const registration = this.registered.get(event.lookupKey),
      callback = registration?.callback
    if (!callback || physicalCallbacks.size >= 128) {
      void this.source
        .resolveLookup(
          event,
          [],
          callback
            ? 'Native HTTP physical lookup callback capacity exceeded'
            : 'Native HTTP lookup callback is unavailable',
        )
        .catch(() => undefined)
      return
    }
    const call: PhysicalCall = { active: true, abort: new AbortController() }
    this.physical.set(id, call)
    physicalCallbacks.add(call)
    if (!registration) throw new Error('Native lookup callback lost its actual registration')
    void registration.run(async () => {
      let addresses: NativeHTTPLookupAddress[] = [],
        error = ''
      try {
        const value = await runNativeMatcherUserPromise(() =>
          callback(event.hostname, { signal: call.abort.signal }),
        )
        addresses = withoutNativeRetryOperation(() => {
          if (!Array.isArray(value) || value.length > 16)
            throw new Error('Native HTTP lookup requires at most 16 IP addresses')
          return Addresses.parse(value)
        })
      } catch (failure) {
        try {
          error = withoutNativeRetryOperation(() => String(failure).slice(0, 4096))
        } catch {
          error = 'Native HTTP lookup callback failed with an unprintable value'
        }
      }
      try {
        if (call.active && !this.closed) await this.source.resolveLookup(event, addresses, error)
      } catch {
        /* Native cancellation or channel closure owns the operation. */
      } finally {
        this.physical.delete(id)
        physicalCallbacks.delete(call)
      }
    })
  }
  /** Host lookup tasks still unsettled, including native answer acknowledgement. */
  physicalCount(): number {
    return this.physical.size
  }
}
