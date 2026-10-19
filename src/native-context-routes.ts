import { randomUUID } from 'node:crypto'

import * as z from 'zod'

import {
  NativeContextRouteInvocations,
  type NativeRouteInvocationWait,
} from './native-context-route-invocations.js'
import {
  NativeContextRouteMutationSchema,
  type NativeContextRouteMutation,
} from './native-context-route-protocol.js'
import { type NativeRouteHandler } from './native-network.js'
import {
  NativeRouteURLMatcherPolicy,
  type NativeRouteURLMatcher,
  type NativeRouteMatcherRegistration,
} from './native-route-url-matcher.js'
import { nativeTraceStack } from './native-trace-metadata.js'
import { type UntrustedInput } from './untrusted.js'

const UnrouteOptions = z.strictObject({
  behavior: z.enum(['default', 'wait', 'ignoreErrors']).optional(),
})
let pendingMutations = 0
interface Mutation {
  admitted: boolean
  applied: boolean
  closed: boolean
  selected: Set<string>
  behavior: 'wait' | 'ignoreErrors'
  outcome: Promise<boolean>
  settle(applied: boolean): void
  authorizeIgnore?: ((ids: ReadonlySet<string>) => void) | undefined
  hostReceipts?: NativeRouteInvocationWait | undefined
}
const Options = z.strictObject({ times: z.number().int().positive().max(1_000_000).optional() })
/** A handler is registered once per actual context. Native admission owns uses. */
export class NativeContextRoutes {
  private readonly handlers = new Map<
    string,
    { policy: NativeRouteURLMatcherPolicy; handler: NativeRouteHandler }
  >()
  private readonly invocations = new NativeContextRouteInvocations()
  private readonly mutations = new Map<string, Mutation>()
  private readonly detachMutations: (() => void) | undefined
  constructor(
    private readonly command: (params: Record<string, UntrustedInput>) => Promise<UntrustedInput>,
    subscribeMutation?: (listener: (record: NativeContextRouteMutation) => void) => () => void,
  ) {
    this.detachMutations = subscribeMutation?.((record) => this.receiveMutation(record))
  }
  async route(
    url: NativeRouteURLMatcher,
    handler: NativeRouteHandler,
    options: { times?: number } = {},
  ): Promise<void> {
    const parsed = Options.parse(options)
    if (typeof handler !== 'function') throw new Error('Native context route requires a handler')
    if (this.handlers.size >= 128) throw new Error('Native context route handler capacity exceeded')
    const registrationId = randomUUID()
    const policy = new NativeRouteURLMatcherPolicy(url)
    this.handlers.set(registrationId, { policy, handler })
    try {
      await this.command({
        action: 'register',
        registrationId,
        url: policy.nativePattern,
        ...(policy.host ? { hostMatcher: true } : {}),
        times: parsed.times ?? 0,
        traceStack: nativeTraceStack(),
      })
    } catch (error) {
      this.drop(registrationId)
      throw error
    }
  }
  async unroute(url: NativeRouteURLMatcher, handler?: NativeRouteHandler): Promise<void> {
    if (handler !== undefined && typeof handler !== 'function')
      throw new Error('Native context unroute requires a handler')
    const ids = [...this.handlers]
      .filter(
        ([, entry]) =>
          entry.policy.matchesRegistration(url) &&
          (handler === undefined || entry.handler === handler),
      )
      .map(([id]) => id)
    if (!ids.length) return
    await this.command({ action: 'remove', registrationIds: ids })
    for (const id of ids) this.drop(id)
  }
  async unrouteAll(
    options: { behavior?: 'default' | 'wait' | 'ignoreErrors' } = {},
  ): Promise<void> {
    const { behavior = 'default' } = UnrouteOptions.parse(options)
    if (behavior === 'default') {
      // A later registration can be admitted while this removal's actual ACK
      // is pending. Remove exactly this call's selection on both sides.
      const ids = [...this.handlers.keys()]
      if (!ids.length) return
      await this.command({ action: 'remove', registrationIds: ids })
      for (const id of ids) this.drop(id)
      return
    }
    if (!this.detachMutations) throw new Error('Native route mutation receipts are unavailable')
    if (pendingMutations >= 64) throw new Error('Native route mutation observer capacity exceeded')
    const mutationId = randomUUID()
    let settle: Mutation['settle'] = () => undefined
    const outcome = new Promise<boolean>((resolve) => {
      settle = resolve
    })
    const mutation: Mutation = {
      admitted: false,
      applied: false,
      closed: false,
      selected: new Set(),
      behavior,
      outcome,
      settle,
    }
    if (behavior === 'ignoreErrors') mutation.authorizeIgnore = this.invocations.armIgnore(outcome)
    this.mutations.set(mutationId, mutation)
    ++pendingMutations
    try {
      await this.command({ action: 'remove', registrationIds: [], behavior, mutationId })
      if (mutation.closed) throw new Error('Native context closed before removal observer delivery')
      if (!mutation.admitted || !mutation.applied)
        throw new Error('Native unrouteAll completed without its actual policy mutation receipts')
      if (behavior === 'wait') await mutation.hostReceipts?.settled
      if (mutation.closed) throw new Error('Native context closed before removal observer delivery')
      mutation.settle(true)
    } catch (error) {
      mutation.settle(false)
      throw error
    } finally {
      mutation.hostReceipts?.cancel()
      if (this.mutations.get(mutationId) === mutation) {
        this.mutations.delete(mutationId)
        --pendingMutations
      }
    }
  }
  private receiveMutation(value: NativeContextRouteMutation): void {
    const record = NativeContextRouteMutationSchema.parse(value),
      mutation = this.mutations.get(record.mutationId)
    if (!mutation) return
    const selected = new Set(record.registrationIds)
    if (selected.size !== record.registrationIds.length)
      throw new Error('Native route mutation contains duplicate registration identities')
    if (record.phase === 'admitted') {
      if (mutation.admitted) throw new Error('Native route mutation admission was published twice')
      mutation.admitted = true
      mutation.selected = selected
      mutation.authorizeIgnore?.(selected)
      if (mutation.behavior === 'wait') mutation.hostReceipts = this.invocations.snapshot(selected)
      for (const id of selected) this.drop(id)
    } else {
      if (
        !mutation.admitted ||
        mutation.applied ||
        selected.size !== mutation.selected.size ||
        [...selected].some((id) => !mutation.selected.has(id))
      )
        throw new Error('Native route mutation ACK does not match its actual admission')
      mutation.applied = true
    }
  }
  private drop(id: string): void {
    this.handlers.get(id)?.policy.dispose()
    this.handlers.delete(id)
  }
  private clearHandlers(): void {
    for (const entry of this.handlers.values()) entry.policy.dispose()
    this.handlers.clear()
  }
  matcher(id: string): NativeRouteMatcherRegistration | undefined {
    const policy = this.handlers.get(id)?.policy
    if (!policy?.host) return undefined
    return {
      callback: policy.callback(),
      observe: (callback) => this.invocations.resolveCallback(id, callback),
    }
  }
  resolve(id: string, exhausted: boolean): NativeRouteHandler | undefined {
    const entry = this.handlers.get(id)
    if (exhausted) this.drop(id)
    return entry?.handler
  }
  /** One wrapper is pinned to one genuine invocation, independent of handler reuse. */
  resolveInvocation(id: string, exhausted: boolean): NativeRouteHandler | undefined {
    const handler = this.resolve(id, exhausted)
    return handler ? this.invocations.resolve(id, handler) : undefined
  }
  close(): void {
    this.clearHandlers()
    this.detachMutations?.()
    for (const mutation of this.mutations.values()) {
      mutation.closed = true
      mutation.hostReceipts?.cancel()
      mutation.settle(false)
      --pendingMutations
    }
    this.mutations.clear()
  }
}
