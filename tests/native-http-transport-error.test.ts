// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import {
  nativeHTTPCommandError,
  NativeHTTPTransportError,
  NativeTimeoutError,
} from '../src/native-timeouts.js'

describe('separately framed native HTTP transport failures', () => {
  it('retains genuine numeric curl/OS observations and exposes the native attributed code', () => {
    const error = nativeHTTPCommandError('apiRequestContext.get: Recv failure', {
      type: 'native-http-transport',
      curlCode: 56,
      osErrno: 104,
      code: 'ECONNRESET',
    })
    expect(error).toBeInstanceOf(NativeHTTPTransportError)
    expect(error).toMatchObject({ curlCode: 56, osErrno: 104, code: 'ECONNRESET' })
    expect(error.message).toBe('apiRequestContext.get: Recv failure (ECONNRESET)')
  })
  it('preserves an absent OS error without deriving a code from diagnostic text', () => {
    for (const [curlCode, osErrno] of [
      [52, 0],
      [18, 0],
      [18, 104],
    ]) {
      const error = nativeHTTPCommandError('origin-controlled ECONNRESET text', {
        type: 'native-http-transport',
        curlCode,
        osErrno,
      })
      expect(error).toBeInstanceOf(NativeHTTPTransportError)
      expect(error).toMatchObject({ curlCode, osErrno, code: undefined })
    }
  })
  it('keeps ordinary failed command classification when there is no valid native metadata', () => {
    for (const value of [
      null,
      'ECONNRESET',
      { code: 'ECONNRESET' },
      { type: 'native-http-transport', curlCode: -1, osErrno: 104 },
    ]) {
      const error = nativeHTTPCommandError('Native original deadline expired', value)
      expect(error).toBeInstanceOf(NativeTimeoutError)
      expect(error).not.toBeInstanceOf(NativeHTTPTransportError)
      expect(error.message).toBe('Native original deadline expired')
    }
  })
})
