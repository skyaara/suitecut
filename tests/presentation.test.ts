// SPDX-License-Identifier: MIT
import { afterEach, describe, expect, it, vi } from 'vitest'

import { installSuiteCutPresentation } from '../src/presentation.js'

describe('installSuiteCutPresentation', () => {
  afterEach(() => {
    vi.unstubAllGlobals()
  })

  it('waits for a root element when a click has just navigated', () => {
    const observed: object[] = []
    vi.stubGlobal('document', { documentElement: null })
    vi.stubGlobal(
      'MutationObserver',
      class {
        observe(target: object) {
          observed.push(target)
        }
        disconnect() {
          observed.length = 0
        }
      },
    )
    const installed = installSuiteCutPresentation()
    expect(installed).toBeInstanceOf(Promise)
    expect(observed).toHaveLength(1)
  })
})
