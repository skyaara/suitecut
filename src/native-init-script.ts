import { open } from 'node:fs/promises'

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
    if (config.path !== undefined) {
      const file = await open(config.path, 'r')
      try {
        const buffer = Buffer.alloc(65537)
        let length = 0
        while (length < buffer.length) {
          const result = await file.read(buffer, length, buffer.length - length, null)
          if (!result.bytesRead) break
          length += result.bytesRead
        }
        if (length > 65536) throw new Error('Native init script exceeds 65536 bytes')
        source = buffer.subarray(0, length).toString('utf8')
      } finally {
        await file.close()
      }
    } else source = config.content ?? ''
  }
  if (!source || source.includes('\0') || Buffer.byteLength(source) > 65536)
    throw new Error('Native init script must contain 1 through 65536 bytes without nulls')
  // The native command reader has a 64 KiB envelope limit. Check escaped source
  // size before sending so a rejected script cannot terminate the transport.
  if (Buffer.byteLength(JSON.stringify({ source })) > 64000)
    throw new Error('Native init script exceeds the command envelope capacity')
  return source
}
