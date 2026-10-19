// SPDX-License-Identifier: MIT
import * as z from 'zod'

import { type UntrustedInput } from './untrusted.js'

const Staged = z.strictObject({ payloadToken: z.string().min(1).max(128) })
type Command = (params: Record<string, UntrustedInput>) => Promise<UntrustedInput>
/** Preserve the bounded 64 KiB command transport; commit invokes native send once. */
export async function runNativeWebSocketPayload(
  run: Command,
  params: Record<string, UntrustedInput>,
  cleanup: Command = run,
): Promise<void> {
  const payload = params.payloadBase64
  if (typeof payload !== 'string' || payload.length <= 32_768) {
    await run(params)
    return
  }
  const bytes =
    (payload.length / 4) * 3 - (payload.endsWith('==') ? 2 : payload.endsWith('=') ? 1 : 0)
  if (payload.length % 4 || bytes > 2_097_152)
    throw new Error('Invalid native WebSocket staged payload')
  // The native bounded append validates canonical base64 before retaining each
  // chunk. Avoid decoding/re-encoding the whole 2 MiB message in the SDK.
  const identity = { ...params }
  delete identity.payloadBase64
  delete identity.action
  const staged = Staged.parse(await run({ ...identity, action: 'routePayloadBegin', bytes }))
  try {
    // 32,768 base64 characters represent precisely 24 KiB, keeping all scope
    // metadata under the unchanged native command framing bound.
    for (let offset = 0; offset < payload.length; offset += 32_768)
      await run({
        ...identity,
        action: 'routePayloadAppend',
        payloadToken: staged.payloadToken,
        base64: payload.slice(offset, offset + 32_768),
      })
    await run({ ...identity, action: 'routePayloadCommit', payloadToken: staged.payloadToken })
  } catch (error) {
    try {
      await cleanup({ ...identity, action: 'routePayloadAbort', payloadToken: staged.payloadToken })
    } catch (failure) {
      throw new AggregateError(
        [error, failure],
        'Native WebSocket payload command and actual staging cleanup failed',
        { cause: failure },
      )
    }
    throw error
  }
}
