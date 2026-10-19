// SPDX-License-Identifier: MIT
import { readFileSync } from 'node:fs'
import { isAbsolute, resolve, sep } from 'node:path'
import { fileURLToPath } from 'node:url'
import * as util from 'node:util'

import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

// An inline type specifier emits an empty import and initializes the test suite.
// eslint-disable-next-line import-x/consistent-type-specifier-style
import type { NativeTestAnnotation } from './native-suite.js'

// Capture the actual Node/V8 callsite primitive before loading any user suite.
// Older supported Node releases lacking it simply supply no location metadata.
const capture = typeof util.getCallSites === 'function' ? util.getCallSites.bind(util) : undefined
const ownFiles = new Set<string>()
for (const name of [
  'native-runner-host.js',
  'native-runner-location.js',
  'native-fixture-location.js',
  'native-fixture-declarations.js',
  'native-fixture-layers.js',
  'native-runner-fixture-bindings.js',
  'native-suite.js',
  'native-runner-steps.js',
  'native-runner-attachment-view.js',
  'native-expect.js',
  'native-soft-assertions.js',
  'native-runner-soft-assertions.js',
  'native-matcher-failure.js',
]) {
  const url = new URL(name, import.meta.url)
  ownFiles.add(fileURLToPath(url))
  // The same authored modules may execute directly through a TypeScript loader.
  if (import.meta.url.endsWith('.ts'))
    ownFiles.add(fileURLToPath(new URL(name.replace(/\.js$/u, '.ts'), import.meta.url)))
  try {
    const data = readFileSync(new URL(name + '.map', import.meta.url))
    if (data.length > 4 * 1024 * 1024) continue
    const map = z
      .object({ sourceRoot: z.string().optional(), sources: z.array(z.string()).max(16) })
      .parse(JSON.parse(data.toString('utf8')) as UntrustedInput)
    const root = map.sourceRoot
      ? new URL(map.sourceRoot.endsWith('/') ? map.sourceRoot : map.sourceRoot + '/', url)
      : url
    for (const source of map.sources) {
      const original = new URL(source, root)
      if (original.protocol === 'file:') ownFiles.add(fileURLToPath(original))
    }
  } catch {
    // Optional absent source maps do not replace real runtime source locations.
  }
}
export function nativeRunnerCallsites(): readonly NonNullable<NativeTestAnnotation['location']>[] {
  if (!capture) return []
  const sites: NonNullable<NativeTestAnnotation['location']>[] = []
  try {
    for (const frame of capture(64, { sourceMap: true })) {
      const script = frame.scriptName
      if (
        !script ||
        script.startsWith('node:') ||
        (!script.startsWith('file:') && !isAbsolute(script))
      )
        continue
      const file = script.startsWith('file:') ? fileURLToPath(script) : resolve(script)
      if (ownFiles.has(file) || file.includes(sep + 'node_modules' + sep)) continue
      if (
        file.length > 4096 ||
        !Number.isSafeInteger(frame.lineNumber) ||
        !Number.isSafeInteger(frame.columnNumber) ||
        frame.lineNumber < 1 ||
        frame.columnNumber < 1 ||
        frame.lineNumber > 2147483647 ||
        frame.columnNumber > 2147483647
      )
        break
      sites.push(Object.freeze({ file, line: frame.lineNumber, column: frame.columnNumber }))
      if (sites.length === 2) break
    }
  } catch {
    // Genuine unavailable V8 metadata is omitted, never synthesized from a test.
  }
  return Object.freeze(sites)
}
export function nativeRunnerLocation(): NativeTestAnnotation['location'] {
  return nativeRunnerCallsites()[0]
}

export const nativeModifierLocation = nativeRunnerLocation
