// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'
import { isAbsolute } from 'node:path'

import * as z from 'zod'

const path = z
  .string()
  .min(1)
  .max(16384)
  .refine(
    (value) =>
      isAbsolute(value) &&
      !value.includes('\0') &&
      Buffer.byteLength(value) <= 16384 &&
      Buffer.from(value, 'utf8').toString('utf8') === value,
    'Native discovery testDir requires bounded absolute scalar filesystem UTF8',
  )
const pattern = z.union([z.string().max(4096), z.instanceof(RegExp)])
const patterns = z.union([pattern, z.array(pattern).max(128)])

export const nativeDiscoveryShape = {
  /** Native C++ recursively enumerates this actual directory. */
  testDir: path.optional(),
  testMatch: patterns.optional(),
  testIgnore: patterns.optional(),
  fileFilters: z.array(z.string().max(4096)).max(128).optional(),
}

export interface NativeSuiteDiscoveryOptions {
  testDir?: string
  testMatch?: string | RegExp | readonly (string | RegExp)[]
  testIgnore?: string | RegExp | readonly (string | RegExp)[]
  /** Public filename regex arguments; matching and selection stay native. */
  fileFilters?: readonly string[]
}

type ParsedDiscovery = z.infer<z.ZodObject<typeof nativeDiscoveryShape>>

const wirePatterns = (value: ParsedDiscovery['testMatch']): string =>
  JSON.stringify(
    value === undefined
      ? null
      : (Array.isArray(value) ? value : [value]).map((item) =>
          typeof item === 'string'
            ? { kind: 'glob', source: item }
            : { kind: 'regex', source: item.source, flags: item.flags },
        ),
  )

/** Serialize declarations only: no JS discovery, matching, stat or file imports. */
export function nativeDiscoveryArguments(
  options: NativeSuiteDiscoveryOptions | ParsedDiscovery,
): string[] {
  const config = z.object(nativeDiscoveryShape).parse(options)
  if (config.testDir === undefined) {
    if (
      config.testMatch !== undefined ||
      config.testIgnore !== undefined ||
      config.fileFilters !== undefined
    )
      throw new Error('Native file patterns require testDir directory mode')
    return []
  }
  return [
    '--test-dir',
    config.testDir,
    ...(config.testMatch === undefined
      ? []
      : ['--test-match-json', wirePatterns(config.testMatch)]),
    ...(config.testIgnore === undefined
      ? []
      : ['--test-ignore-json', wirePatterns(config.testIgnore)]),
    ...(config.fileFilters === undefined
      ? []
      : ['--file-filters-json', JSON.stringify(config.fileFilters)]),
  ]
}
