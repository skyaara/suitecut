import * as z from 'zod'

import {
  NativeContextNetworkInfoSchema,
  type NativeContextNetworkInfo,
} from './native-context-network-protocol.js'
import {
  type NativeResponse,
  type NativeNetworkEventName,
  type NativeRequest,
} from './native-network.js'
import { getNativeDefaultTimeout } from './native-timeouts.js'
import { type NativeWorkerInfo } from './native-worker-protocol.js'
import { type UntrustedInput } from './untrusted.js'

export interface NativeContextNetworkTransport {
  readonly signal: AbortSignal
  contextNetworkSequence(): number
  onContextNetwork(listener: (event: NativeContextNetworkInfo) => void): () => void
  runContextNetwork(
    params: Record<string, UntrustedInput>,
    signal?: AbortSignal,
  ): Promise<UntrustedInput>
}
interface ContextOwner {
  readonly id: number
  projectWorkerNetworkEvent(
    worker: NativeWorkerInfo,
    event: NativeNetworkEventName,
    value: UntrustedInput,
  ): NativeRequest | NativeResponse
  projectNetworkEvent(
    pageId: number,
    event: NativeNetworkEventName,
    value: UntrustedInput,
  ): NativeRequest | NativeResponse
}
type Listener = (value: NativeRequest | NativeResponse) => void
const Options = z.strictObject({
  timeout: z.number().int().min(0).max(120000).optional(),
  signal: z.instanceof(AbortSignal).optional(),
})
/** Projects actual native context journal records through canonical page caches. */
export class NativeContextNetwork {
  private readonly listeners = new Map<NativeNetworkEventName, Listener[]>()
  private readonly detach: () => void
  private readonly aborted = () => this.close()
  private closed = false
  constructor(
    private readonly owner: ContextOwner,
    private readonly transport: NativeContextNetworkTransport,
  ) {
    this.detach = transport.onContextNetwork((event) => {
      if (event.contextId !== owner.id) return
      const listeners = this.listeners.get(event.network.event as NativeNetworkEventName)
      if (!listeners?.length && !('worker' in event)) return
      const value = this.project(event.network.event as NativeNetworkEventName, event)
      for (const listener of [...(listeners ?? [])]) listener(value)
    })
    transport.signal.addEventListener('abort', this.aborted, { once: true })
    if (transport.signal.aborted) this.close()
  }
  on(event: NativeNetworkEventName, listener: Listener, prepend = false): void {
    if (this.closed) throw new Error('Native browser context network is closed')
    const callbacks = this.listeners.get(event) ?? []
    if (prepend) callbacks.unshift(listener)
    else callbacks.push(listener)
    this.listeners.set(event, callbacks)
  }
  off(event: NativeNetworkEventName, listener: Listener): void {
    const callbacks = this.listeners.get(event)
    if (!callbacks) return
    const index = callbacks.indexOf(listener)
    if (index >= 0) callbacks.splice(index, 1)
    if (!callbacks.length) this.listeners.delete(event)
  }
  waitForEvent(event: 'response', options?: z.input<typeof Options>): Promise<NativeResponse>
  waitForEvent(
    event: Exclude<NativeNetworkEventName, 'response'>,
    options?: z.input<typeof Options>,
  ): Promise<NativeRequest>
  async waitForEvent(
    event: NativeNetworkEventName,
    options: z.input<typeof Options> = {},
  ): Promise<NativeRequest | NativeResponse> {
    if (this.closed) throw new Error('Native browser context network is closed')
    const name = z.enum(['request', 'response', 'requestfinished', 'requestfailed']).parse(event)
    const config = Options.parse(options)
    const value = await this.transport.runContextNetwork(
      {
        action: 'waitEvent',
        contextId: this.owner.id,
        event: name,
        afterSequence: this.transport.contextNetworkSequence(),
        timeoutMs: config.timeout ?? getNativeDefaultTimeout(this.owner),
      },
      config.signal,
    )
    return this.project(name, value)
  }
  close(): void {
    if (this.closed) return
    this.closed = true
    this.transport.signal.removeEventListener('abort', this.aborted)
    this.detach()
    this.listeners.clear()
  }
  private project(
    event: NativeNetworkEventName,
    value: UntrustedInput,
  ): NativeRequest | NativeResponse {
    const record = NativeContextNetworkInfoSchema.parse(value)
    if (record.contextId !== this.owner.id)
      throw new Error('Native context network record belongs to a different browser context')
    return 'worker' in record
      ? this.owner.projectWorkerNetworkEvent(record.worker, event, record.network)
      : this.owner.projectNetworkEvent(record.pageId, event, record.network)
  }
}
