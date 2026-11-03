// SPDX-License-Identifier: MIT
import { readFile } from 'node:fs/promises'

import * as z from 'zod'

import { withoutNativeRetryOperation } from './native-callback-operations.js'
import { createNativeValueCodec } from './native-values.js'
import { type UntrustedInput } from './untrusted.js'

export type NativeInitScript<A = undefined> =
  string | { content?: string; path?: string } | ((arg: A) => void)

/** Serialize authored initialization code; execution belongs to CEF. */
export async function nativeInitScriptSource<A>(
  script: NativeInitScript<A>,
  arg?: A,
): Promise<string> {
  let source: string
  if (typeof script === 'function') {
    let body = withoutNativeRetryOperation(() => script.toString())
    if (!/^async\s+function\b/u.test(body) && /^async\s+[\p{L}\p{N}_$]+\s*\(/u.test(body))
      body = body.replace(/^async\s+/u, 'async function ')
    else if (
      /^[\p{L}\p{N}_$]+\s*\(/u.test(body) &&
      !body.startsWith('function') &&
      !/^async\s*\(/u.test(body)
    )
      body = `function ${body}`
    const encoded = withoutNativeRetryOperation(() =>
      createNativeValueCodec().encode(arg as UntrustedInput, 'argument'),
    )
    source = `(${body})((${createNativeValueCodec.toString()})().decode(${JSON.stringify(encoded)}));`
  } else if (typeof script === 'string') source = script
  else {
    const config = withoutNativeRetryOperation(() =>
      z.strictObject({ content: z.string().optional(), path: z.string().optional() }).parse(script),
    )
    if ((config.content !== undefined) === (config.path !== undefined))
      throw new Error('addInitScript requires exactly one of content or path')
    source =
      config.path !== undefined ? await readFile(config.path, 'utf8') : (config.content ?? '')
  }
  if (!source || source.includes('\0'))
    throw new Error('Native init script must be non-empty and contain no nulls')
  return source
}
