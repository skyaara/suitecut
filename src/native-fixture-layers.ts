// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { NativeFixtureDeclarationSchema } from './native-fixture-declarations.js'
import { captureNativeFixtureLocations } from './native-fixture-location.js'
import { type NativeSelectableFixtureDefinition } from './native-fixture-values.js'
import { type UntrustedInput } from './untrusted.js'

export type NativeFixtureDeclarations = Readonly<Record<string, NativeSelectableFixtureDefinition>>
const layerBrand: unique symbol = Symbol('NativeFixtureLayers')
/** Immutable raw declarations. Native C++ resolves all overrides and dependencies. */
export interface NativeFixtureLayers {
  readonly [layerBrand]: true
  readonly layers: readonly NativeFixtureDeclarations[]
}
const issued = new WeakSet<object>()
export function isNativeFixtureLayers(value: UntrustedInput): value is NativeFixtureLayers {
  return typeof value === 'object' && value !== null && issued.has(value)
}
const name = z.string().regex(/^[A-Za-z_][A-Za-z0-9_]*$/u)
// Type-only facade imports can form an ESM cycle; resolve declarations only
// when parsing authored data, after module initialization has completed.
const record = z.record(
  name,
  z.lazy(() => NativeFixtureDeclarationSchema),
)

function snapshot(input: NativeFixtureDeclarations): NativeFixtureDeclarations {
  const parsed = record.parse(input)
  return Object.freeze(parsed)
}

/** Append declarations only; do not resolve self/super or effective bindings in JS. */
export function extendNativeFixtures(
  base: NativeFixtureDeclarations | NativeFixtureLayers,
  overrides: NativeFixtureDeclarations,
): NativeFixtureLayers {
  if (!isNativeFixtureLayers(base)) captureNativeFixtureLocations(base)
  captureNativeFixtureLocations(overrides)
  const layers = [...nativeFixtureDeclarationLayers(base), snapshot(overrides)]
  const result: NativeFixtureLayers = Object.freeze({
    [layerBrand]: true as const,
    layers: Object.freeze(layers),
  })
  issued.add(result)
  return result
}

/** Host serialization seam: preserves raw layer order, never selects a version. */
export function nativeFixtureDeclarationLayers(
  input: NativeFixtureDeclarations | NativeFixtureLayers,
): readonly NativeFixtureDeclarations[] {
  if (isNativeFixtureLayers(input)) return input.layers
  return Object.freeze([snapshot(input)])
}
