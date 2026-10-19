// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { copyNativeFixtureLocation } from './native-fixture-location.js'
import {
  nativeFixtureLiteralWire,
  type NativeFixtureJSON,
  type NativeFixtureLiteral,
  type NativeSelectableFixtureDefinition,
} from './native-fixture-values.js'
import { type NativeFixtureDefinition } from './native-suite.js'
import { type UntrustedInput } from './untrusted.js'

const name = z.string().regex(/^[A-Za-z_][A-Za-z0-9_]{0,255}$/u)
const fields = {
  title: z.string().max(4096).optional(),
  timeout: z.number().int().min(0).max(120000).optional(),
  scope: z.enum(['worker', 'test']).optional(),
  automatic: z.boolean().optional(),
  option: z.boolean().optional(),
}
const callback = z.strictObject({
  ...fields,
  dependencies: z.array(name).max(256).optional(),
  setup: z.custom<NativeFixtureDefinition['setup']>((value) => typeof value === 'function'),
})
const literal = z.strictObject({
  ...fields,
  value: z.custom<NativeFixtureLiteral>(() => true),
})
function freeze(value: NativeFixtureJSON): NativeFixtureJSON {
  if (value && typeof value === 'object') {
    for (const child of Object.values(value)) freeze(child)
    Object.freeze(value)
  }
  return value
}

/** Snapshot a raw declaration; never decide its effective value or dependencies. */
export function parseNativeFixtureDeclaration(
  input: UntrustedInput,
): NativeSelectableFixtureDefinition {
  if (typeof input === 'object' && input !== null)
    for (const descriptor of Object.values(Object.getOwnPropertyDescriptors(input)))
      if (!Object.hasOwn(descriptor, 'value'))
        throw new TypeError('Native fixture declarations require data properties')
  const parsed = z.union([callback, literal]).parse(input)
  const common = {
    ...(parsed.title === undefined ? {} : { title: parsed.title }),
    ...(parsed.timeout === undefined ? {} : { timeout: parsed.timeout }),
    ...(parsed.scope === undefined ? {} : { scope: parsed.scope }),
    ...(parsed.automatic === undefined ? {} : { automatic: parsed.automatic }),
    ...(parsed.option === undefined ? {} : { option: parsed.option }),
  }
  if ('setup' in parsed) {
    const output = Object.freeze({
      ...common,
      setup: parsed.setup,
      ...(parsed.dependencies === undefined
        ? {}
        : { dependencies: Object.freeze(parsed.dependencies) }),
    })
    copyNativeFixtureLocation(input, output)
    return output
  }
  if (typeof input !== 'object' || input === null || !Object.hasOwn(input, 'value'))
    throw new TypeError('Native literal fixture requires an explicit value property')
  const wire = nativeFixtureLiteralWire(parsed.value)
  const output = Object.freeze({
    ...common,
    value:
      wire.kind === 'undefined' ? undefined : freeze(JSON.parse(wire.json) as NativeFixtureJSON),
  })
  copyNativeFixtureLocation(input, output)
  return output
}

export const NativeFixtureDeclarationSchema = z
  .custom<UntrustedInput>(() => true)
  .transform((input, context) => {
    try {
      return parseNativeFixtureDeclaration(input)
    } catch (error) {
      context.addIssue({
        code: 'custom',
        message: error instanceof Error ? error.message : 'Invalid native fixture declaration',
      })
      return z.NEVER
    }
  })
