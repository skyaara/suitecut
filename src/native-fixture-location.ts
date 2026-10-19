// SPDX-License-Identifier: MIT
import { nativeRunnerLocation } from './native-runner-location.js'
import { type NativeTestAnnotation } from './native-suite.js'
import { type UntrustedInput } from './untrusted.js'

type Location = NonNullable<NativeTestAnnotation['location']>
const locations = new WeakMap<object, Location>()
/** Capture only the actual declaration call; no authored location is accepted. */
export function captureNativeFixtureLocations(
  input: Readonly<Record<string, UntrustedInput>>,
): void {
  const site = nativeRunnerLocation()
  if (!site) return
  for (const descriptor of Object.values(Object.getOwnPropertyDescriptors(input))) {
    const value = descriptor.value as UntrustedInput
    if (typeof value === 'object' && value !== null && !locations.has(value))
      locations.set(value, site)
  }
}
/** Keep genuine captured provenance when validating/snapshotting the same declaration. */
export function copyNativeFixtureLocation(input: UntrustedInput, output: object): void {
  if (typeof input !== 'object' || input === null) return
  const site = locations.get(input)
  if (site) locations.set(output, site)
}
export function nativeFixtureLocation(input: object): Location | undefined {
  return locations.get(input)
}
