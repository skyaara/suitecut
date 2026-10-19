// SPDX-License-Identifier: MIT
import { readFileSync, readdirSync } from 'node:fs'
import { dirname, resolve, sep } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

const sdkRoot = dirname(fileURLToPath(import.meta.url))
const SourceMapSchema = z.object({
  sourceRoot: z.string().optional(),
  sources: z.array(z.string()).max(4096),
})
let mappedSources: Set<string> | undefined
function mappedSDKSources(): Set<string> {
  if (mappedSources) return mappedSources
  mappedSources = new Set<string>()
  // Select exact original files from the SDK's own module maps. A blanket
  // sibling src/ exclusion would also hide a real application's caller files.
  let names: string[]
  try {
    names = readdirSync(sdkRoot)
  } catch {
    return mappedSources
  }
  for (const name of names) {
    if (!/^native(?:-[a-z0-9-]+)?\.js\.map$/u.test(name)) continue
    try {
      const bytes = readFileSync(resolve(sdkRoot, name))
      if (bytes.length > 4 * 1024 * 1024) continue
      const parsed = JSON.parse(bytes.toString('utf8')) as UntrustedInput
      const map = SourceMapSchema.parse(parsed)
      const mapURL = pathToFileURL(resolve(sdkRoot, name))
      const root = map.sourceRoot
        ? new URL(map.sourceRoot.endsWith('/') ? map.sourceRoot : map.sourceRoot + '/', mapURL)
        : mapURL
      for (const source of map.sources) {
        const url = new URL(source, root)
        if (url.protocol === 'file:') mappedSources.add(fileURLToPath(url))
      }
    } catch {
      // Missing or malformed optional maps do not discard an external caller.
    }
  }
  return mappedSources
}
/** Actual caller metadata; native code owns the action clock and trace journal. */
export function nativeTraceStack(): {
  file: string
  line: number
  column: number
  function: string
}[] {
  const frames: { file: string; line: number; column: number; function: string }[] = []
  for (const line of (new Error().stack ?? '').split('\n').slice(1)) {
    const match = /^\s*at (?:(.*?) \()?(.+):(\d+):(\d+)\)?$/u.exec(line)
    if (!match) continue
    const raw = match[2]
    if (!raw || raw.startsWith('node:')) continue
    let file: string
    try {
      file = raw.startsWith('file:') ? fileURLToPath(raw) : resolve(raw)
    } catch {
      continue
    }
    if (
      file.startsWith(sdkRoot + sep) ||
      mappedSDKSources().has(file) ||
      file.includes(`${sep}node_modules${sep}`)
    )
      continue
    frames.push({
      file,
      line: Number(match[3]),
      column: Number(match[4]),
      function: match[1] ?? '',
    })
    // The first external caller is the public API invocation boundary; async
    // transport, fixture and runner continuation frames are not source callers.
    break
  }
  return frames
}
