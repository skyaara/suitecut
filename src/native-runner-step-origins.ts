// SPDX-License-Identifier: MIT
// Object identity and author async-scope bookkeeping only. Native C++ validates
// every claimed source against actual callback ownership and step ancestry.
import * as z from 'zod'

import { type NativeStepOwner } from './native-runner-steps.js'
import { type UntrustedInput } from './untrusted.js'

const UInt = z
  .string()
  .regex(/^[1-9]\d{0,19}$/u)
  .refine((value) => BigInt(value) <= 18446744073709551615n)
const Owner = z.strictObject({
  token: z.union([z.literal('0'), UInt]),
  scope: UInt,
  generation: UInt,
})
const ownerKey = (owner: NativeStepOwner): string =>
  `${owner.token}:${owner.scope}:${owner.generation}`
interface ActualStep {
  readonly owner: string
  readonly ancestors: readonly string[]
}
export class NativeStepFailureOrigins {
  private readonly actual = new Map<string, ActualStep>()
  private readonly observed = new Set<string>()
  private readonly errors = new WeakMap<object, Map<string, Set<string>>>()
  private reserved = 0
  admit(owner: NativeStepOwner, id: string, parent: string): void {
    const identity = ownerKey(Owner.parse(owner)),
      actualId = UInt.parse(id)
    if (this.actual.has(actualId) || this.actual.size >= 8192)
      throw new Error('Native step-origin admission capacity/identity rejected')
    const prior = parent === '0' ? undefined : this.actual.get(UInt.parse(parent))
    if (parent !== '0' && prior?.owner !== identity)
      throw new Error('Native step-origin parent belongs to a different callback')
    const ancestors = prior ? [...prior.ancestors, parent] : []
    const reservation =
      identity.length * 2 +
      actualId.length * 2 +
      ancestors.reduce((sum, item) => sum + item.length * 2 + 32, 0) +
      1024
    if (ancestors.length >= 64 || reservation > 64 * 1024 * 1024 - this.reserved)
      throw new Error('Native step-origin metadata capacity exceeded')
    this.actual.set(actualId, { owner: identity, ancestors: Object.freeze(ancestors) })
    this.reserved += reservation
  }
  // Called only after the actual native STEP_RESULT confirms this raw failure.
  observe(owner: NativeStepOwner, id: string, error: UntrustedInput): void {
    const identity = ownerKey(Owner.parse(owner)),
      actualId = UInt.parse(id)
    if (this.actual.get(actualId)?.owner !== identity || this.observed.has(actualId))
      throw new Error('Native step-origin failure has stale/duplicate actual identity')
    this.observed.add(actualId)
    if ((typeof error !== 'object' || error === null) && typeof error !== 'function') return
    let owners = this.errors.get(error)
    if (!owners) {
      owners = new Map()
      this.errors.set(error, owners)
    }
    let steps = owners.get(identity)
    if (!steps) {
      steps = new Set()
      owners.set(identity, steps)
    }
    steps.add(actualId)
  }
  source(owner: NativeStepOwner, error: UntrustedInput, within?: string): string | undefined {
    const identity = ownerKey(Owner.parse(owner))
    if (within !== undefined && this.actual.get(UInt.parse(within))?.owner !== identity)
      throw new Error('Native step-origin source query has stale actual callback')
    if ((typeof error !== 'object' || error === null) && typeof error !== 'function')
      return undefined
    const candidates = [...(this.errors.get(error)?.get(identity) ?? [])].filter(
      (id) => within === undefined || this.actual.get(id)?.ancestors.includes(within),
    )
    const ids = new Set(candidates)
    const roots = candidates.filter(
      (id) => !this.actual.get(id)?.ancestors.some((ancestor) => ids.has(ancestor)),
    )
    // Equal object identity is insufficient to select between sibling failures.
    return roots.length === 1 ? roots[0] : undefined
  }
}
