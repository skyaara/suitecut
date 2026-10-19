// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { NativeTimeoutError, nativeCommandError } from '../src/native-timeouts.js'

describe('actual native coverage deadline error classification', () => {
  it('preserves the truthful native owner/queue diagnostic and public timeout class', () => {
    for (const scope of ['operation', 'queued command', 'target bootstrap']) {
      const diagnostic = `Native coverage ${scope} exceeded its original deadline`
      const error = nativeCommandError(diagnostic)
      expect(error).toBeInstanceOf(NativeTimeoutError)
      expect(error.name).toBe('TimeoutError')
      expect(error.message).toBe(diagnostic)
    }
  })
  it('keeps actual target loss and physical-retirement failure distinct from a public timeout', () => {
    for (const diagnostic of [
      'Actual native coverage target detached before its engine counters could be collected',
      'Native coverage physical command did not retire within its original deadline and bounded cleanup grace',
      'Native coverage operation canceled',
      'Author original deadline description',
    ]) {
      const error = nativeCommandError(diagnostic)
      expect(error).not.toBeInstanceOf(NativeTimeoutError)
      expect(error.name).toBe('Error')
      expect(error.message).toBe(diagnostic)
    }
  })
})
