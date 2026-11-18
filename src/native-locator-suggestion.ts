// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { type NativeLocator } from './native-page.js'
import { type UntrustedInput } from './untrusted.js'

const text = { text: z.string(), exact: z.literal(true) }
const step = z.discriminatedUnion('engine', [
  z.strictObject({ engine: z.literal('css'), selector: z.string().min(1) }),
  z.strictObject({ engine: z.literal('testId'), attribute: z.string().min(1), ...text }),
  z.strictObject({
    engine: z.literal('role'),
    role: z
      .string()
      .min(1)
      .regex(/^[a-z-]+$/u),
    name: z.strictObject(text),
  }),
  z.strictObject({ engine: z.enum(['label', 'text', 'placeholder', 'alt', 'title']), ...text }),
])
const recipe = z
  .strictObject({
    kind: z.enum(['testId', 'label', 'role', 'attribute', 'text', 'id', 'css', 'path']),
    query: z.array(step).min(1),
    code: z.string().min(1),
    matches: z.literal(1),
    sameNode: z.literal(true),
  })
  .superRefine((value, context) => {
    const first = value.query[0]
    const valid =
      value.kind === 'path'
        ? value.query.every((item) => item.engine === 'css')
        : value.query.length === 1 &&
          (value.kind === 'id' || value.kind === 'css'
            ? first?.engine === 'css'
            : value.kind === 'attribute'
              ? first?.engine === 'placeholder' ||
                first?.engine === 'alt' ||
                first?.engine === 'title'
              : first?.engine === value.kind)
    if (!valid)
      context.addIssue({
        code: 'custom',
        message: 'Invalid native locator suggestion recipe shape',
      })
  })
const provenance = z.strictObject({
  source: z
    .string()
    .min(1)
    .regex(/^[A-Za-z0-9]+$/u),
  frame: z.string().min(1),
  session: z.string(),
  loader: z.string(),
  mainIdentity: z.string().min(1),
  utilityIdentity: z.string().min(1),
  mainContext: z.number().int().positive().max(2_147_483_647),
  utilityContext: z.number().int().positive().max(2_147_483_647),
  documentGeneration: z.string().regex(/^(?:0|[1-9][0-9]{0,19})$/u),
  backendNodeId: z.number().int().positive().max(2_147_483_647),
  retainedHandle: z.number().int().positive().optional(),
  point: z
    .strictObject({
      x: z.number().int().min(0),
      y: z.number().int().min(0),
      inspectorX: z.number().int().min(0),
      inspectorY: z.number().int().min(0),
      inspectorRoot: z.string().min(1),
    })
    .optional(),
})
const diagnostic = z.strictObject({
  kind: z.string(),
  reason: z.string(),
  matches: z.number().int().nonnegative().optional(),
})
const packet = z
  .strictObject({
    suggestions: z.array(recipe).min(1),
    provenance,
    diagnostics: z.array(diagnostic),
    pointPicking: z.boolean(),
  })
  .superRefine((value, context) => {
    if (
      value.pointPicking !== (value.provenance.point !== undefined) ||
      (value.provenance.point !== undefined && value.provenance.retainedHandle !== undefined)
    )
      context.addIssue({
        code: 'custom',
        message: 'Native point selection receipt is incomplete or contradictory',
      })
  })
export const NativeLocatorSuggestionOptionsSchema = z.strictObject({
  timeout: z.number().int().min(0).max(120_000).optional(),
  signal: z.instanceof(AbortSignal).optional(),
})
export type NativeLocatorSuggestionOptions = z.infer<typeof NativeLocatorSuggestionOptionsSchema>
export type NativeLocatorSuggestionRecipe = Readonly<z.infer<typeof recipe>>
export type NativeLocatorSuggestionProvenance = Readonly<z.infer<typeof provenance>>
/** Capture facts only; each returned locator resolves its ordinary query later. */
export interface NativeLocatorSuggestions {
  readonly suggestions: readonly (NativeLocatorSuggestionRecipe & {
    readonly locator: NativeLocator
  })[]
  readonly provenance: NativeLocatorSuggestionProvenance
  readonly diagnostics: readonly Readonly<z.infer<typeof diagnostic>>[]
  readonly pointPicking: boolean
}
/** Decode native policy/results. No SDK ranking, DOM reads or selector retries. */
export function decodeNativeLocatorSuggestions(value: UntrustedInput): z.infer<typeof packet> {
  return packet.parse(value)
}

/** Integer viewport CSS pixels in the actual requested frame. */
export const NativeLocatorPickPointSchema = z.strictObject({
  x: z.number().int().min(0).max(1_000_000),
  y: z.number().int().min(0).max(1_000_000),
})
export type NativeLocatorPickPoint = z.infer<typeof NativeLocatorPickPointSchema>
