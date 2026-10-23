// SPDX-License-Identifier: MIT
import { randomUUID } from 'node:crypto'

import {
  type NativeContextRouteInvocations,
  type NativeRouteInvocationWaitReservation,
} from './native-context-route-invocations.js'
import {
  NativePageRouteMutationSchema,
  nativePageRouteCallbackIdentity,
  type NativePageRouteMutation,
} from './native-page-route-removal-protocol.js'
import { type UntrustedInput } from './untrusted.js'

interface Mutation {
  admitted: boolean
  applied: boolean
  closed: boolean
  behavior: 'wait' | 'ignoreErrors'
  scope: string
  registrations: Set<string>
  tokens: Set<string>
  authorizeIgnore?: ((identities: ReadonlySet<string>) => void) | undefined
  wait?: NativeRouteInvocationWaitReservation | undefined
  settle(applied: boolean): void
}
let pending = 0
/** Page policy removal shares no Context-policy counter or callback selection. */
export class NativePageRouteRemovals {
  private readonly mutations = new Map<string, Mutation>()
  private closed = false
  constructor(
    private readonly invocations: NativeContextRouteInvocations,
    private readonly command: (params: Record<string, UntrustedInput>) => Promise<UntrustedInput>,
    private readonly removed: (ids: ReadonlySet<string>) => void,
  ) {}
  async remove(behavior: 'wait' | 'ignoreErrors'): Promise<void> {
    if (this.closed) throw new Error('Native Page route removal owner is closed')
    if (pending >= 64) throw new Error('Native Page route removal observation capacity exceeded')
    const id = randomUUID()
    let settle: Mutation['settle'] = () => undefined
    const outcome = new Promise<boolean>((done) => {
      settle = done
    })
    const state: Mutation = {
      admitted: false,
      applied: false,
      closed: false,
      behavior,
      scope: '',
      registrations: new Set(),
      tokens: new Set(),
      settle,
    }
    if (behavior === 'wait') state.wait = this.invocations.reserveWait()
    if (behavior === 'ignoreErrors') state.authorizeIgnore = this.invocations.armIgnore(outcome)
    this.mutations.set(id, state)
    ++pending
    try {
      await this.command({ action: 'removeRoute', registrationId: '', behavior, mutationId: id })
      if (state.closed) throw new Error('Native Page closed before removal observer delivery')
      if (!state.admitted || !state.applied)
        throw new Error(
          'Native Page removal completed without its actual admission and control ACK',
        )
      if (behavior === 'wait') await state.wait?.settled
      if (state.closed) throw new Error('Native Page closed before removal observer delivery')
      state.settle(true)
    } catch (error) {
      state.settle(false)
      throw error
    } finally {
      state.wait?.cancel()
      this.mutations.delete(id)
      --pending
    }
  }
  receive(value: NativePageRouteMutation): void {
    const record = NativePageRouteMutationSchema.parse(value),
      state = this.mutations.get(record.mutationId)
    if (!state || state.closed) return
    const registrations = new Set(record.registrationIds),
      tokens = new Set(record.callbackTokens)
    if (
      registrations.size !== record.registrationIds.length ||
      tokens.size !== record.callbackTokens.length
    )
      throw new Error('Native Page removal contains duplicate policy or callback identities')
    if (record.phase === 'admitted') {
      if (state.admitted) throw new Error('Native Page removal admission was published twice')
      state.admitted = true
      state.registrations = registrations
      state.scope = record.callbackScope
      state.tokens = tokens
      const identities = new Set(
        [...tokens].map((token) => nativePageRouteCallbackIdentity(record.callbackScope, token)),
      )
      this.removed(registrations)
      state.authorizeIgnore?.(identities)
      if (state.behavior === 'wait') state.wait?.capture(identities)
    } else {
      if (
        !state.admitted ||
        state.applied ||
        record.callbackScope !== state.scope ||
        registrations.size !== state.registrations.size ||
        tokens.size !== state.tokens.size ||
        [...registrations].some((id) => !state.registrations.has(id)) ||
        [...tokens].some((token) => !state.tokens.has(token))
      )
        throw new Error('Native Page removal ACK does not match its actual admission')
      state.applied = true
    }
  }
  close(): void {
    this.closed = true
    for (const state of this.mutations.values()) {
      state.closed = true
      state.wait?.cancel()
      state.settle(false)
    }
  }
}
