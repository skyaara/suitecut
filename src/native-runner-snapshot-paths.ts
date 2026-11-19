// SPDX-License-Identifier: MIT
import { extname, isAbsolute, join, resolve, sep } from 'node:path'

import { z } from 'zod'

import { type UntrustedInput } from './untrusted.js'

export interface NativeSnapshotPath {
  (...name: readonly string[]): string
  (name: string, options: { readonly kind: 'snapshot' | 'screenshot' | 'aria' }): string
}
export interface NativeTestSnapshots {
  readonly snapshotDir: string
  readonly snapshotPath: NativeSnapshotPath
  snapshotSuffix: string
}
export interface NativeSnapshotConfiguration {
  /** Paths resolve against the actual canonical suite module's directory. */
  readonly testDir?: string
  readonly snapshotDir?: string
  readonly snapshotPathTemplate?: string
  readonly expect?: {
    readonly toHaveScreenshot?: { readonly pathTemplate?: string }
    readonly toMatchAriaSnapshot?: { readonly pathTemplate?: string }
  }
}
const text = () =>
  z
    .string()
    .refine((value) => !value.includes('\0') && Buffer.from(value).toString('utf8') === value)
export const NativeSnapshotConfigurationSchema = z.strictObject({
  testDir: text().optional(),
  snapshotDir: text().optional(),
  snapshotPathTemplate: text().optional(),
  expect: z
    .strictObject({
      toHaveScreenshot: z.strictObject({ pathTemplate: text().optional() }).optional(),
      toMatchAriaSnapshot: z.strictObject({ pathTemplate: text().optional() }).optional(),
    })
    .optional(),
})
const part = z.strictObject({ kind: z.enum(['literal', 'arg', 'ext']), value: text() })
const parts = z.array(part).min(1)
const path = text().refine((value) => isAbsolute(value) && resolve(value) === value)
const plan = z.strictObject({
  version: z.literal(1),
  baseDirectory: path,
  snapshotDir: path,
  snapshot: parts,
  screenshot: parts,
  aria: parts,
})
const planID = z.number().int().min(1).max(3)
const sharedPlan = z
  .strictObject({
    version: z.literal(2),
    baseDirectory: path,
    snapshotDir: path,
    plans: z
      .array(z.strictObject({ id: planID, parts }))
      .min(1)
      .max(3),
    kinds: z.strictObject({ snapshot: planID, screenshot: planID, aria: planID }),
  })
  .refine((value) => {
    const ids = new Set(value.plans.map((plan) => plan.id))
    return ids.size === value.plans.length && Object.values(value.kinds).every((id) => ids.has(id))
  }, 'Invalid native snapshot plan references')
const unavailable = z.strictObject({
  version: z.literal(1),
  unsupported: text(),
  snapshotDir: path.optional(),
})
const suffixText = text().refine(
  (value) => value !== '.' && value !== '..' && !value.includes('/') && !value.includes('\\'),
  'Native snapshotSuffix supports only a filename component',
)
const suffixParts = z
  .array(part.extend({ kind: z.enum(['literal', 'arg', 'ext', 'suffix']) }))
  .min(1)
const suffixContext = z.strictObject({
  version: z.literal(3),
  suffix: z.strictObject({
    initial: suffixText,
    // Accepted for older native runners; the host enforces no suffix length.
    maxBytes: z.number().int().nonnegative().optional(),
    policy: z.literal('filenameComponent'),
  }),
  paths: z.union([
    plan.extend({ snapshot: suffixParts, screenshot: suffixParts, aria: suffixParts }),
    z
      .strictObject({
        ...sharedPlan.shape,
        plans: z
          .array(z.strictObject({ id: planID, parts: suffixParts }))
          .min(1)
          .max(3),
      })
      .refine((value) => {
        const ids = new Set(value.plans.map((plan) => plan.id))
        return (
          ids.size === value.plans.length && Object.values(value.kinds).every((id) => ids.has(id))
        )
      }, 'Invalid native snapshot plan references'),
    unavailable,
  ]),
})

/** The host installs only the native-selected test's frozen token plan. No IO or native identity inference. */
export function nativeTestSnapshots(input: UntrustedInput): NativeTestSnapshots {
  const issued =
    input !== null && typeof input === 'object' && 'version' in input && input.version === 3
      ? suffixContext.parse(input)
      : undefined
  let suffix = issued?.suffix.initial
  const suffixAccess = {
    get snapshotSuffix(): string {
      if (suffix === undefined) throw new Error('Native snapshotSuffix has no issued policy')
      return suffix
    },
    set snapshotSuffix(value: string) {
      if (!issued) throw new Error('Native snapshotSuffix has no issued policy')
      suffix = suffixText.parse(value)
    },
  }
  const pooled =
    input !== null && typeof input === 'object' && 'version' in input && input.version === 2
  const parsed =
    issued?.paths ?? (pooled ? sharedPlan.parse(input) : z.union([plan, unavailable]).parse(input))
  if ('unsupported' in parsed) {
    const fail = (): never => {
      throw new Error(parsed.unsupported)
    }
    return Object.freeze({
      get snapshotDir() {
        return parsed.snapshotDir ?? fail()
      },
      snapshotPath: fail,
      get snapshotSuffix() {
        return suffixAccess.snapshotSuffix
      },
      set snapshotSuffix(value: string) {
        suffixAccess.snapshotSuffix = value
      },
    })
  }
  const definitions =
    parsed.version === 2 ? new Map(parsed.plans.map((plan) => [plan.id, plan.parts])) : undefined
  const select = (kind: 'snapshot' | 'screenshot' | 'aria') => {
    if (parsed.version === 1) return parsed[kind]
    const selected = definitions?.get(parsed.kinds[kind])
    if (!selected) throw new Error('Native snapshot plan reference lacks its local definition')
    return selected
  }
  const snapshotPath = (...input: readonly UntrustedInput[]): string => {
    const names = [...input]
    let kind: 'snapshot' | 'screenshot' | 'aria' = 'snapshot'
    const last = names.at(-1)
    if (last !== null && typeof last === 'object') {
      const option = z.strictObject({ kind: z.string() }).parse(last)
      if (option.kind !== 'snapshot' && option.kind !== 'screenshot' && option.kind !== 'aria')
        throw new Error('testInfo.snapshotPath: unknown kind ' + JSON.stringify(option.kind))
      kind = option.kind
      names.pop()
      if (names.length !== 1) throw new Error('Native snapshotPath kind requires exactly one name')
    }
    const segments = z
      .array(text().refine((value) => value.length > 0 && !isAbsolute(value)))
      .min(1)
      .parse(names)
    const name = join(...segments)
    if (name === '..' || name.startsWith('..' + sep) || isAbsolute(name))
      throw new Error('Native snapshotPath name escapes its relative argument')
    const extension = extname(name),
      argument = extension ? name.slice(0, -extension.length) : name
    let result = ''
    for (const item of select(kind)) {
      const value =
        item.kind === 'literal'
          ? item.value
          : item.kind === 'arg'
            ? argument
            : item.kind === 'ext'
              ? extension
              : suffixAccess.snapshotSuffix
      result += item.kind !== 'literal' && value ? item.value + value : value
    }
    return resolve(parsed.baseDirectory, result)
  }
  return Object.freeze({
    snapshotDir: parsed.snapshotDir,
    snapshotPath,
    get snapshotSuffix() {
      return suffixAccess.snapshotSuffix
    },
    set snapshotSuffix(value: string) {
      suffixAccess.snapshotSuffix = value
    },
  })
}

export class NativeSnapshotCallbacks {
  private readonly contexts = new Map<
    string,
    { current: NativeTestSnapshots; fresh: () => NativeTestSnapshots; retry?: number }
  >()
  packet(fields: readonly string[], testCount: number): void {
    const [kind, id, wire] = z
      .tuple([
        z.literal('TEST_SNAPSHOT_CONTEXT'),
        z.string().regex(/^[1-9][0-9]*$/u),
        z.string().regex(/^(?:[a-f\d]{2})+$/u),
      ])
      .parse(fields)
    if (kind !== 'TEST_SNAPSHOT_CONTEXT' || Number(id) > testCount || this.contexts.has(id))
      throw new Error('Native snapshot plan lacks its actual test declaration')
    const raw = Buffer.from(wire, 'hex'),
      encoded = raw.toString('utf8')
    if (!Buffer.from(encoded).equals(raw))
      throw new Error('Native snapshot plan encoding is not UTF-8')
    const fresh = () => nativeTestSnapshots(JSON.parse(encoded) as UntrustedInput)
    this.contexts.set(id, { current: fresh(), fresh })
  }
  snapshot(testId: string): NativeTestSnapshots {
    const context = this.contexts.get(testId)
    if (!context) throw new Error('Native snapshot path has no associated test admission')
    return context.current
  }
  /** The host passes only the actual native BODY/CALL retry field, after its ownership checks. */
  admit(testId: string, retry: number): NativeTestSnapshots {
    z.number().int().min(0).parse(retry)
    const context = this.contexts.get(testId)
    if (!context) throw new Error('Native snapshot suffix has no associated test admission')
    if (context.retry !== undefined && context.retry !== retry) context.current = context.fresh()
    context.retry = retry
    return context.current
  }
}
