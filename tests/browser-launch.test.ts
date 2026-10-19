// SPDX-License-Identifier: MIT
import { describe, expect, it, vi } from 'vitest'

import { launchBrowser } from '../src/browser-launch.js'

const launches = vi.hoisted(() => ({ native: vi.fn(), protocol: vi.fn() }))
vi.mock('../src/native-browser-controller.js', () => ({ launchBrowser: launches.native }))
vi.mock('../src/remote-browser.js', () => ({ launchProtocolBrowser: launches.protocol }))

describe('browser backend launch selection', () => {
  it('preserves default native launch and strips the explicit CEF discriminator', async () => {
    launches.native.mockResolvedValue({ backend: 'cef' })
    await launchBrowser({ executablePath: '/browser/native' })
    expect(launches.native).toHaveBeenLastCalledWith({ executablePath: '/browser/native' })
    await launchBrowser({ backend: 'cef', executablePath: '/browser/native' })
    expect(launches.native).toHaveBeenLastCalledWith({ executablePath: '/browser/native' })
  })

  it('selects CDP and BiDi without invoking native launch', async () => {
    launches.native.mockClear()
    for (const backend of ['cdp', 'bidi'] as const) {
      launches.protocol.mockResolvedValue({ backend })
      const options = {
        backend,
        executablePath: '/browser/installed',
        viewport: { width: 800, height: 600 },
      }
      expect(await launchBrowser(options)).toEqual({ backend })
      expect(launches.protocol).toHaveBeenLastCalledWith(options)
    }
    expect(launches.native).not.toHaveBeenCalled()
  })
})
