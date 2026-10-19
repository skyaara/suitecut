import { type NativeRouteHandler } from './native-network.js'
import {
  isNativeRouteCommandFailure,
  observeNativeRouteHandler,
} from './native-route-handler-observations.js'
import { type UntrustedInput } from './untrusted.js'

interface Invocation {
  registration: string
  suppressed: boolean
  pending: Set<IgnoreScope>
  decision?: ((suppressed: boolean) => void) | undefined
  receiptObserved: boolean
  waits: Set<WaitScope>
}
export interface NativeRouteInvocationWait {
  readonly settled: Promise<void>
  cancel(): void
}
export interface NativeRouteInvocationWaitReservation extends NativeRouteInvocationWait {
  capture(registrations: ReadonlySet<string>): void
}
interface WaitScope {
  captured: boolean
  members: Set<Invocation>
  resolve(): void
}
interface IgnoreScope {
  selected: ReadonlySet<string> | undefined
  members: Set<Invocation>
  completed: boolean
}

/** Host closures survive registration exhaustion/removal until their real receipt. */
export class NativeContextRouteInvocations {
  private readonly active = new Set<Invocation>()
  private readonly ignores = new Set<IgnoreScope>()
  private readonly waits = new Set<WaitScope>()

  resolve(registration: string, handler: NativeRouteHandler): NativeRouteHandler {
    const state = this.begin(registration)
    const wrapped: NativeRouteHandler = async (route, request) => {
      try {
        await handler(route, request)
      } catch (error) {
        if (!(await this.suppressed(state, error as UntrustedInput))) throw error
        // Only the selected user exception is suppressed. The actual pause
        // still requires a terminal command, whose failure stays observable.
        if (!route.isHandled()) await route.resolveAutomatically('abort')
      }
    }
    this.observe(state, wrapped)
    return wrapped
  }

  /** Match callbacks share actual removal receipts without fabricating a Route. */
  resolveCallback<T>(
    registration: string,
    callback: () => Promise<T>,
  ): () => Promise<T | undefined> {
    const state = this.begin(registration)
    const wrapped = async () => {
      try {
        return await callback()
      } catch (error) {
        if (!(await this.suppressed(state, error as UntrustedInput))) throw error
        return undefined
      }
    }
    this.observe(state, wrapped)
    return wrapped
  }

  private begin(registration: string): Invocation {
    if (this.active.size >= 128)
      throw new Error('Native context route invocation observation capacity exceeded')
    const state: Invocation = {
      registration,
      suppressed: false,
      pending: new Set(),
      receiptObserved: false,
      waits: new Set(),
    }
    this.active.add(state)
    for (const scope of this.ignores) if (!scope.selected) this.join(state, scope)
    return state
  }

  private async suppressed(state: Invocation, error: UntrustedInput): Promise<boolean> {
    if (error instanceof Error && isNativeRouteCommandFailure(error)) throw error
    if (!state.suppressed && state.pending.size)
      return await new Promise<boolean>((resolve) => {
        state.decision = resolve
      })
    return state.suppressed
  }

  private observe(state: Invocation, wrapped: object): void {
    observeNativeRouteHandler(wrapped, (settlement) => {
      state.receiptObserved = true
      const settled = () => {
        this.active.delete(state)
        for (const scope of state.pending) scope.members.delete(state)
        state.pending.clear()
        for (const scope of [...state.waits]) {
          scope.members.delete(state)
          state.waits.delete(scope)
          if (!scope.members.size) this.finishWait(scope)
        }
      }
      void settlement.then(settled, settled)
    })
  }

  /** Pre-admission errors await the real outcome; only admitted IDs authorize it. */
  armIgnore(outcome: Promise<boolean>): (registrations: ReadonlySet<string>) => void {
    const scope = this.createScope(undefined, outcome)
    return (registrations) => {
      if (registrations.size > 256)
        throw new Error('Native route exception selection capacity exceeded')
      if (scope.completed) return
      scope.selected = new Set(registrations)
      for (const state of scope.members) {
        if (scope.selected.has(state.registration)) continue
        scope.members.delete(state)
        state.pending.delete(scope)
        if (state.decision && !state.pending.size) {
          const decision = state.decision
          state.decision = undefined
          decision(state.suppressed)
        }
      }
    }
  }

  /** A known selection never delays unrelated registration exceptions. */
  ignore(registrations: ReadonlySet<string>, outcome: Promise<boolean>): void {
    this.createScope(registrations, outcome)
  }

  private createScope(
    selected: ReadonlySet<string> | undefined,
    outcome: Promise<boolean>,
  ): IgnoreScope {
    if (this.ignores.size >= 64)
      throw new Error('Native context route exception-policy observation capacity exceeded')
    if (selected && selected.size > 256)
      throw new Error('Native route exception selection capacity exceeded')
    const scope: IgnoreScope = {
      selected: selected ? new Set(selected) : undefined,
      members: new Set(),
      completed: false,
    }
    this.ignores.add(scope)
    for (const state of this.active)
      if (!selected || selected.has(state.registration)) this.join(state, scope)
    const settled = (applied: boolean) => {
      scope.completed = true
      this.ignores.delete(scope)
      for (const state of scope.members) {
        state.pending.delete(scope)
        state.suppressed ||= applied && scope.selected?.has(state.registration) === true
        if (state.decision && (state.suppressed || !state.pending.size)) {
          const decision = state.decision
          state.decision = undefined
          decision(state.suppressed)
        }
      }
      scope.members.clear()
    }
    // One observer per bounded mutation, not one Promise continuation for each
    // completed request. Real receipt cleanup removes obsolete member edges.
    void outcome.then(settled, () => settled(false))
    return scope
  }

  private join(state: Invocation, scope: IgnoreScope): void {
    if (state.suppressed) return
    state.pending.add(scope)
    scope.members.add(state)
  }

  /** Reserve observer quota before any actual native policy mutation. */
  reserveWait(): NativeRouteInvocationWaitReservation {
    if (this.waits.size >= 64) throw new Error('Native context route host wait capacity exceeded')
    let resolve = () => undefined as void
    const settled = new Promise<void>((done) => {
      resolve = done
    })
    const scope: WaitScope = { members: new Set(), resolve, captured: false }
    this.waits.add(scope)
    return {
      settled,
      cancel: () => this.finishWait(scope),
      capture: (registrations) => {
        if (!this.waits.has(scope) || scope.captured)
          throw new Error('Native route host wait reservation is unavailable or already captured')
        const members = new Set(
          [...this.active].filter((entry) => registrations.has(entry.registration)),
        )
        for (const entry of members)
          if (!entry.receiptObserved)
            throw new Error('Actual route invocation settlement receipt is unavailable')
        scope.captured = true
        scope.members = members
        for (const entry of members) entry.waits.add(scope)
        if (!members.size) this.finishWait(scope)
      },
    }
  }

  /** Capture exact host receipts without adding Promise reactions per wait. */
  snapshot(registrations: ReadonlySet<string>): NativeRouteInvocationWait {
    const reservation = this.reserveWait()
    try {
      reservation.capture(registrations)
      return reservation
    } catch (error) {
      reservation.cancel()
      throw error
    }
  }

  private finishWait(scope: WaitScope): void {
    if (!this.waits.delete(scope)) return
    for (const entry of scope.members) entry.waits.delete(scope)
    scope.members.clear()
    scope.resolve()
  }

  waitObservationEdges(): number {
    return [...this.waits].reduce((count, scope) => count + scope.members.size, 0)
  }

  pending(): number {
    return this.active.size
  }
  observationEdges(): number {
    return [...this.ignores].reduce((count, scope) => count + scope.members.size, 0)
  }
}
