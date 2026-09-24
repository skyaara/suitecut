import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { DEFAULT_BACKEND, defineSuiteCut, launchBrowser, record } from '../src/beta.js'
import { launchNativeBrowser, type NativeBrowser } from '../src/native-browser.js'
import { recordNative } from '../src/native-recording.js'
import { record as recordPlaywright, type SuiteCutRecordResult } from '../src/playwright.js'

vi.mock('../src/native-browser.js', () => ({ launchNativeBrowser: vi.fn() }))
vi.mock('../src/native-recording.js', () => ({ recordNative: vi.fn() }))
vi.mock('../src/playwright.js', () => ({ record: vi.fn() }))

describe('native-default beta', () => {
  let source: NativeBrowser
  const result = {
    attemptId: 'attempt',
    manifestPath: '/manifest.json',
    outputDirectory: '/output',
    testId: 'test',
  } as SuiteCutRecordResult
  beforeEach(() => {
    vi.stubEnv('SUITECUT_NATIVE_EXECUTABLE', '/native/browser')
    source = { close: vi.fn(() => Promise.resolve()) } as Partial<NativeBrowser> as NativeBrowser
    vi.mocked(launchNativeBrowser).mockResolvedValue(source)
    vi.mocked(recordNative).mockResolvedValue(result)
    vi.mocked(recordPlaywright).mockResolvedValue(result)
  })
  afterEach(() => {
    vi.resetAllMocks()
    vi.unstubAllEnvs()
  })

  it('defaults to native I420, tags results, and owns browser cleanup', async () => {
    const callback = vi.fn()
    const recorded = await record('beta', callback)
    expect(DEFAULT_BACKEND).toBe('native')
    expect(launchNativeBrowser).toHaveBeenCalledWith({
      executablePath: '/native/browser',
      pixelFormat: 'i420',
    })
    expect(recordNative).toHaveBeenCalledWith('beta', callback, { source })
    expect(recorded.backend).toBe('native')
    expect(recordPlaywright).not.toHaveBeenCalled()
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting a mock.
    expect(source.close).toHaveBeenCalledTimes(1)
  })

  it('requires explicit Playwright fallback and leaves its options intact', async () => {
    const callback = vi.fn()
    const fallback = await record('baseline', callback, {
      backend: 'playwright',
      browserName: 'firefox',
      capture: { framesPerSecond: 30 },
    })
    expect(fallback.backend).toBe('playwright')
    expect(recordPlaywright).toHaveBeenCalledWith('baseline', callback, {
      browserName: 'firefox',
      capture: { framesPerSecond: 30 },
    })
    expect(launchNativeBrowser).not.toHaveBeenCalled()
  })

  it('does not silently fall back when the executable is missing or launch fails', async () => {
    vi.stubEnv('SUITECUT_NATIVE_EXECUTABLE', '')
    expect(() => launchBrowser()).toThrow('requires a native executable')
    vi.stubEnv('SUITECUT_NATIVE_EXECUTABLE', '/bad/browser')
    vi.mocked(launchNativeBrowser).mockRejectedValue(new Error('CEF launch failed'))
    await expect(record('beta', vi.fn())).rejects.toThrow('CEF launch failed')
    expect(recordPlaywright).not.toHaveBeenCalled()
  })

  it('closes its native source on recording failure', async () => {
    vi.mocked(recordNative).mockRejectedValue(new Error('record failed'))
    await expect(record('beta', vi.fn())).rejects.toThrow('record failed')
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting a mock.
    expect(source.close).toHaveBeenCalledTimes(1)
  })

  it('merges reusable defaults and honors an explicit native executable', async () => {
    const configured = defineSuiteCut({
      native: { executablePath: '/custom/browser', width: 1280 },
      capture: { narrationTailMs: 0, audio: true },
      output: { directory: '/output' },
    })
    await configured('configured', vi.fn(), {
      native: { height: 720 },
      output: { manifestPath: '/custom.json' },
    })
    expect(launchNativeBrowser).toHaveBeenCalledWith({
      executablePath: '/custom/browser',
      width: 1280,
      height: 720,
      pixelFormat: 'i420',
    })
    expect(recordNative).toHaveBeenCalledWith(
      'configured',
      expect.any(Function),
      expect.objectContaining({
        capture: { narrationTailMs: 0, audio: true },
        output: { directory: '/output', manifestPath: '/custom.json' },
      }),
    )
  })
})
