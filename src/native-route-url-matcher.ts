import { isPromise } from 'node:util/types'

import { toError, type UntrustedInput } from './untrusted.js'

export type NativeRouteURLMatcher = string | RegExp | ((url: URL) => boolean)
export type NativeRouteMatcherEvaluation =
  | { matched: boolean; error?: never; continuation?: never }
  | { error: Error; matched?: never; continuation?: Promise<UntrustedInput> }

export interface NativeRouteMatcherRegistration {
  callback(this: void, url: string): NativeRouteMatcherEvaluation
  observe(callback: () => Promise<void>): () => Promise<void | undefined>
}

const IntrinsicPromise = Promise,
  intrinsicPromiseThen = Reflect.get(Promise.prototype, 'then')

let retained = 0,
  bytes = 0

/** Immutable registration policy, executed only for a genuine native candidate. */
export class NativeRouteURLMatcherPolicy {
  readonly host: boolean
  readonly nativePattern: string
  private original: NativeRouteURLMatcher | undefined
  private source: string
  private flags: string
  private cost: number
  private expression: RegExp | undefined
  constructor(url: NativeRouteURLMatcher) {
    if (typeof url !== 'string' && typeof url !== 'function' && !(url instanceof RegExp))
      throw new Error('Native route requires a URL glob, RegExp or synchronous URL predicate')
    const source = url instanceof RegExp ? url.source : typeof url === 'string' ? url : ''
    const flags = url instanceof RegExp ? url.flags : ''
    if (source.length > 8192 || source.includes('\0') || (typeof url === 'string' && !source))
      throw new Error('Native route URL matcher exceeds its metadata bounds')
    const host = typeof url !== 'string'
    const cost = host ? Buffer.byteLength(source) + Buffer.byteLength(flags) : 0
    if (host && (retained >= 1024 || cost > 4 * 1024 * 1024 - bytes))
      throw new Error('Native route URL matcher registration capacity exceeded')
    this.host = host
    this.nativePattern = this.host ? '**/*' : source
    this.original = url
    this.source = source
    this.flags = flags
    this.expression = url instanceof RegExp ? new RegExp(source, flags) : undefined
    this.cost = cost
    if (host) ++retained
    bytes += cost
  }

  matchesRegistration(url: NativeRouteURLMatcher): boolean {
    if (this.original === undefined) return false
    if (typeof this.original === 'function') return this.original === url
    if (this.original instanceof RegExp)
      return url instanceof RegExp && this.source === url.source && this.flags === url.flags
    return this.original === url
  }

  /** Capture the exact registration before native start/removal can interleave. */
  callback(): (url: string) => NativeRouteMatcherEvaluation {
    const original = this.original
    if (!this.host || original === undefined)
      throw new Error('Native host URL matcher registration is unavailable')
    // The policy owns its clone. Evaluation is synchronous, so
    // resetting lastIndex preserves g/y isolation without allocating a new
    // RegExp for every intercepted request.
    const expression = this.expression
    return (url) => {
      try {
        // g/y begin at zero for each request. The private clone never reads or
        // mutates the author's lastIndex, nor calls an overridden test().
        if (expression) expression.lastIndex = 0
        const value: UntrustedInput = expression
          ? expression.test(url)
          : typeof original === 'function'
            ? original(new URL(url))
            : undefined
        if (typeof value === 'boolean') return { matched: value }
        const error = new Error('Native route URL predicate must return a synchronous boolean')
        if (isPromise(value)) {
          let observationFailure: Error | undefined
          const continuation = new IntrinsicPromise<UntrustedInput>((resolve, reject) => {
            try {
              // A real Promise's own then can lie or throw. Only the captured
              // intrinsic can observe its actual internal settlement.
              Reflect.apply(intrinsicPromiseThen, value, [resolve, reject])
            } catch (failure) {
              // Author constructor/species hooks can prevent attaching even an
              // intrinsic observer. Leave this continuation pending: no finish
              // ACK may claim that the unobservable real work has settled.
              observationFailure = toError(failure as UntrustedInput)
            }
          })
          void continuation.catch(() => undefined)
          return { error: observationFailure ?? error, continuation }
        }
        if (value !== null && (typeof value === 'object' || typeof value === 'function')) {
          // Read then only once: a getter itself may have author side effects.
          const then: UntrustedInput = Reflect.get(value, 'then')
          if (typeof then === 'function') {
            const continuation = Promise.resolve().then(
              () =>
                new Promise<UntrustedInput>((resolve, reject) => {
                  Reflect.apply(then, value, [resolve, reject])
                }),
            )
            // Logical rejection is immediate. The adapter must still retain
            // this actual continuation and finish its native physical lease.
            void continuation.catch(() => undefined)
            return { error, continuation }
          }
        }
        return { error }
      } catch (error) {
        return { error: toError(error as UntrustedInput) }
      }
    }
  }

  dispose(): void {
    if (this.original === undefined) return
    this.original = undefined
    this.source = ''
    this.flags = ''
    this.expression = undefined
    if (this.host) --retained
    bytes -= this.cost
    this.cost = 0
  }
}
