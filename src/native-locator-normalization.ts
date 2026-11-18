// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { type NativeQueryStep } from './native-query.js'
import { type UntrustedInput } from './untrusted.js'

const exact = { text: z.string(), exact: z.literal(true) }
const step = z.discriminatedUnion('engine', [
  z.strictObject({ engine: z.literal('css'), selector: z.string().min(1) }),
  z.strictObject({ engine: z.literal('testId'), attribute: z.string().min(1), ...exact }),
  z.strictObject({
    engine: z.literal('role'),
    role: z
      .string()
      .min(1)
      .regex(/^[a-z-]+$/u),
    name: z.strictObject(exact),
  }),
  z.strictObject({ engine: z.enum(['placeholder', 'alt', 'title']), ...exact }),
])
const metadata = z
  .strictObject({
    kind: z.enum(['id', 'role', 'testId', 'attribute', 'path']),
    query: z.array(step).min(1),
  })
  .superRefine((value, context) => {
    const first = value.query[0]
    const valid =
      value.kind === 'path'
        ? value.query.every((item) => item.engine === 'css')
        : value.query.length === 1 &&
          (value.kind === 'id'
            ? first?.engine === 'css' && first.selector.startsWith('#')
            : value.kind === 'role'
              ? first?.engine === 'role'
              : value.kind === 'testId'
                ? first?.engine === 'testId'
                : first?.engine === 'placeholder' ||
                  first?.engine === 'alt' ||
                  first?.engine === 'title')
    if (!valid)
      context.addIssue({ code: 'custom', message: 'Invalid native normalized builtin locator AST' })
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
})
const packet = z.strictObject({ metadataJSON: z.string().min(1), provenance })

/** Actual native creation pins; future locator resolution uses the returned AST. */
export type NativeLocatorNormalization = Readonly<
  z.infer<typeof provenance> & {
    kind: z.infer<typeof metadata>['kind']
  }
>

/** Parses only the explicit native normalization reply, without author tag decoding. */
export function decodeNativeLocatorNormalization(value: UntrustedInput): {
  query: NativeQueryStep[]
  provenance: NativeLocatorNormalization
} {
  const result = packet.parse(value)
  const actual = metadata.parse(JSON.parse(result.metadataJSON) as UntrustedInput)
  return {
    query: actual.query,
    provenance: Object.freeze({ ...result.provenance, kind: actual.kind }),
  }
}
