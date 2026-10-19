// SPDX-License-Identifier: MIT
import { Buffer } from 'node:buffer'

import { describe, expect, it } from 'vitest'

import { NativeWebSocketRouting } from '../src/native-websocket-routing.js'
import { type UntrustedInput } from '../src/untrusted.js'

describe('native routed payload cleanup ownership', () => {
  it.each([true, false])(
    'preserves the original failure and one abort with cleanup=%s',
    async (customCleanup) => {
      const failure = new Error('append failed after staging admission')
      const transport = {
        aborts: 0,
        timeout: () => 1000,
        run(params: Record<string, UntrustedInput>): Promise<UntrustedInput> {
          if (params.action === 'routeEnable')
            return Promise.resolve({ routingToken: '1', revision: 1 })
          if (params.action === 'routePayloadBegin')
            return Promise.resolve({ payloadToken: 'owned-stage' })
          if (params.action === 'routePayloadAppend') return Promise.reject(failure)
          if (params.action === 'routePayloadAbort') {
            this.aborts++
            return Promise.resolve(null)
          }
          return Promise.reject(new Error('Unexpected transport operation'))
        },
        ...(customCleanup
          ? {
              cleanup(
                this: { aborts: number },
                params: Record<string, UntrustedInput>,
              ): Promise<UntrustedInput> {
                expect(params.action).toBe('routePayloadAbort')
                expect(params.payloadToken).toBe('owned-stage')
                this.aborts++
                return Promise.resolve(null)
              },
            }
          : {}),
      }
      const routing = new NativeWebSocketRouting(transport)
      await routing.enable()
      await expect(
        routing.command('channel', 'client', {
          payloadBase64: Buffer.alloc(65536, 7).toString('base64'),
        }),
      ).rejects.toBe(failure)
      expect(transport.aborts).toBe(1)
    },
  )
})
