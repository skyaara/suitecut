// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { z } from 'zod'

export const nativeRepeatCount = z.number().int().min(0).max(Number.MAX_SAFE_INTEGER)
export interface NativeRepeatContext {
  readonly repeatEachIndex: number
  readonly declaredRepeatEach: number
  readonly effectiveRepeatEach: number
  readonly iterations: number
  readonly logicalTestId: string
}
const localId = z.string().regex(/^[1-9][0-9]{0,3}$/u)
const ownerId = z.string().regex(/^[1-9][0-9]{0,19}$/u)
const integer = z.coerce.number().int().nonnegative().max(Number.MAX_SAFE_INTEGER)

/** Transport only. Native selected admissions own identity, repetition and counts. */
export class NativeRepeatCallbacks {
  private readonly contexts = new Map<string, NativeRepeatContext>()
  private projectContext: { owner: number; declared: number } | undefined
  private actor:
    | {
        file: string
        project: string
        index: number
        declared: number
        effective: number
        iterations: number
      }
    | undefined
  project(fields: readonly string[], actualProject: number): number {
    const [, owner, declared] = z
      .tuple([z.literal('PROJECT_REPEAT_CONTEXT'), integer, integer])
      .parse(fields)
    if (
      !owner ||
      owner !== actualProject ||
      this.projectContext ||
      this.actor ||
      this.contexts.size
    )
      throw new Error('Native project repeat context lacks exact original project admission')
    this.projectContext = Object.freeze({ owner, declared })
    return declared
  }
  begin(fields: readonly string[], actualProject: number): number {
    const [, file, project, localProject, index, declared, effective, iterations] = z
      .tuple([
        z.literal('REPEAT_ACTOR_CONTEXT'),
        ownerId,
        ownerId,
        integer,
        integer,
        integer,
        integer,
        integer,
      ])
      .parse(fields)
    if (
      this.actor ||
      this.contexts.size ||
      localProject !== actualProject ||
      (this.projectContext !== undefined &&
        (this.projectContext.owner !== localProject ||
          this.projectContext.declared !== declared)) ||
      iterations !== Math.max(1, effective) ||
      index >= iterations
    )
      throw new Error('Native repeat actor differs from original project/context admission')
    this.actor = Object.freeze({ file, project, index, declared, effective, iterations })
    return declared
  }
  packet(fields: readonly string[], testCount: number): void {
    const [, id, hexId] = z
      .tuple([
        z.literal('TEST_REPEAT_CONTEXT'),
        localId,
        z
          .string()
          .max(512)
          .regex(/^(?:[a-f\d]{2})+$/u),
      ])
      .parse(fields)
    const actor = this.actor
    if (!actor || Number(id) > testCount || this.contexts.has(id))
      throw new Error('Native repeat test context lacks exact actor/declaration admission')
    const bytes = Buffer.from(hexId, 'hex'),
      logicalTestId = bytes.toString('utf8')
    const expected = `native-repeat:${actor.file}:${actor.project}:${id}:${actor.index}`
    if (!Buffer.from(logicalTestId).equals(bytes) || logicalTestId !== expected)
      throw new Error('Native repeat public identity differs from admitted actor/declaration')
    this.contexts.set(
      id,
      Object.freeze({
        repeatEachIndex: actor.index,
        declaredRepeatEach: actor.declared,
        effectiveRepeatEach: actor.effective,
        iterations: actor.iterations,
        logicalTestId,
      }),
    )
  }
  snapshot(id: string): NativeRepeatContext {
    const context = this.contexts.get(id)
    if (context) return context
    if (this.actor) throw new Error('Native repeat actor has no required selected test mapping')
    // Versioned legacy BODY/CALL ABI: an ordinary native-admitted callback is
    // repeat zero with its original local public ID. This is never a body/retry
    // counter, and the explicit actor marker forbids fallback for repeat actors.
    return Object.freeze({
      repeatEachIndex: 0,
      declaredRepeatEach: 1,
      effectiveRepeatEach: 1,
      iterations: 1,
      logicalTestId: id,
    })
  }
}
