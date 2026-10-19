// SPDX-License-Identifier: MIT
// Private setup-context/receipt marshalling prerequisites. Native admission is
// authoritative; this module has no scheduler, process verdict or reuse grant.
import { AsyncLocalStorage } from 'node:async_hooks'

import { type NativeSourceWorkConnection } from './native-source-work.js'
import { type NativeWorkWatermark } from './native-work-watermark.js'

export interface NativeSourceSetupSelection {
  readonly catalog: string
  readonly declaration: string
  readonly fixtureKey: string
  readonly token: string
  readonly owner: string
  readonly test: string
  readonly generation: string
  readonly scope: 'worker'
  readonly origin: 'callback'
  readonly callbackScope?: 'test' | 'beforeAll' | 'afterAll'
}
export interface NativeSourceLaunchReceipt {
  readonly kind: 'registered'
  readonly ticket: string
  readonly descriptor: 4
  readonly cookie: string
}
export interface NativeSourceSetupTransport {
  request(
    selection: NativeSourceSetupSelection,
    executable: string,
  ): Promise<NativeSourceLaunchReceipt | { readonly kind: 'freshOnly' }>
  startup(selection: NativeSourceSetupSelection, ticket: string): Promise<void>
  /** Actual cancellation-admitted ACK; native physical ticket stays retained. */
  cancel(selection: NativeSourceSetupSelection, ticket: string): Promise<void>
  bindWork?(
    selection: NativeSourceSetupSelection,
    ticket: string,
    connection: NativeSourceWorkConnection,
  ): () => void
  observeWork?(
    selection: NativeSourceSetupSelection,
    ticket: string,
    watermark: NativeWorkWatermark,
  ): Promise<void>
}
interface SetupContext {
  readonly selection: NativeSourceSetupSelection
  readonly callbacks: NativeSourceSetupCallbacks
  open: boolean
}
const context = new AsyncLocalStorage<SetupContext>()
export interface NativeSourceLaunchAdmission extends NativeSourceLaunchReceipt {
  /** Waits an actual native authenticated startup receipt; no local timer. */
  startup(): Promise<void>
  /** Recheck synchronously immediately before spawn, with no intervening await. */
  launchAllowed(): boolean
  /** Native cancellation-admitted ACK only; not physical child retirement. */
  cancel(): Promise<void>
  /** Private actual NativeBrowser transport binding; no launch/reuse grant. */
  bindWork?(connection: NativeSourceWorkConnection): () => void
  workChanged?(): void
  observeWork?(watermark: NativeWorkWatermark): Promise<void>
}
/** Returns no endpoint outside actual selected authored setup before use(). */
export async function prepareNativeFixtureSource(
  executable: string,
): Promise<NativeSourceLaunchAdmission | undefined> {
  const current = context.getStore()
  if (!current?.open) return undefined
  return current.callbacks.request(current.selection, executable)
}
export class NativeSourceSetupCallbacks {
  private pending = 0
  private readonly active = new Map<string, SetupContext>()
  constructor(
    private readonly transport: NativeSourceSetupTransport,
    private readonly changed: () => void = () => undefined,
  ) {}
  /** The host must supply the native selected version, never an author alias. */
  invoke<T>(selection: NativeSourceSetupSelection, callback: (beforeUse: () => void) => T): T {
    if (
      selection.origin !== 'callback' ||
      selection.scope !== 'worker' ||
      this.active.has(selection.token)
    )
      throw new Error('Native Source setup is not a selected worker callback')
    const current: SetupContext = {
      selection: Object.freeze({ ...selection }),
      callbacks: this,
      open: true,
    }
    const close = (): void => {
      current.open = false
      if (this.active.get(current.selection.token) === current)
        this.active.delete(current.selection.token)
      this.changed()
    }
    this.active.set(current.selection.token, current)
    try {
      const result = context.run(current, () => callback(close))
      // Observe callback settlement without replacing its raw return/throw.
      void Promise.resolve(result).then(close, close)
      return result
    } catch (error) {
      close()
      throw error
    }
  }
  close(token: string, generation: string): boolean {
    const current = this.active.get(token)
    if (current?.selection.generation !== generation) return false
    current.open = false
    this.active.delete(token)
    this.changed()
    return true
  }
  // Queue readiness only: a living registered Source remains native-owned.
  get idleReady(): boolean {
    return this.pending === 0 && this.active.size === 0
  }
  async request(
    selection: NativeSourceSetupSelection,
    executable: string,
  ): Promise<NativeSourceLaunchAdmission | undefined> {
    // This cannot authorize a launch: only the real native transport response
    // may provide the private descriptor/ticket. Cancellation never fakes ACKs.
    const current = this.active.get(selection.token)
    if (!current?.open || current.selection !== selection) return undefined
    ++this.pending
    try {
      const receipt = await this.transport.request(selection, executable)
      if (receipt.kind === 'freshOnly') return undefined
      if (!current.open || this.active.get(selection.token) !== current) {
        await this.transport.cancel(selection, receipt.ticket)
        return undefined
      }
      ++this.pending // Accepted registration stays charged even before caller awaits startup.
      let startup: Promise<void> | undefined, cancellation: Promise<void> | undefined
      const bindWork = this.transport.bindWork?.bind(this.transport)
      const observeWork = this.transport.observeWork?.bind(this.transport)
      return Object.freeze({
        ...receipt,
        workChanged: this.changed,
        ...(bindWork === undefined
          ? {}
          : {
              bindWork: (connection: NativeSourceWorkConnection): (() => void) =>
                bindWork(selection, receipt.ticket, connection),
            }),
        ...(observeWork === undefined
          ? {}
          : {
              observeWork: (watermark: NativeWorkWatermark): Promise<void> =>
                observeWork(selection, receipt.ticket, watermark),
            }),
        launchAllowed: (): boolean =>
          !cancellation && current.open && this.active.get(selection.token) === current,
        cancel: (): Promise<void> => {
          if (cancellation) return cancellation
          ++this.pending
          cancellation = Promise.resolve()
            .then(() => this.transport.cancel(selection, receipt.ticket))
            .finally(() => {
              --this.pending
              if (!startup) --this.pending
              this.changed()
            })
          return cancellation
        },
        startup: (): Promise<void> => {
          if (cancellation)
            return Promise.reject(new Error('Native Source startup admission was cancelled'))
          if (startup) return startup
          startup = Promise.resolve()
            .then(() => this.transport.startup(selection, receipt.ticket))
            .finally(() => {
              --this.pending
              this.changed()
            })
          return startup
        },
      })
    } finally {
      --this.pending
      this.changed()
    }
  }
}
