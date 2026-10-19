import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { record as betaRecord } from '../src/beta.js'
import { DEFAULT_BACKEND, defineSuiteCut, launchBrowser, record } from '../src/index.js'
import { launchNativeBrowser, type NativeBrowser } from '../src/native-browser.js'
import { recordNative } from '../src/native-recording.js'
import { type SuiteCutRecordResult } from '../src/native-recording.js'

vi.mock('../src/native-browser.js', () => ({ launchNativeBrowser: vi.fn() }))
vi.mock('../src/native-recording.js', () => ({ recordNative: vi.fn() }))

describe('native-default public API', () => {
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
  })
  afterEach(() => {
    vi.resetAllMocks()
    vi.unstubAllEnvs()
  })

  it('defaults to native I420, tags results, and owns browser cleanup', async () => {
    const callback = vi.fn()
    const recorded = await record('beta', callback)
    expect(DEFAULT_BACKEND).toBe('native')
    expect(record).toBe(betaRecord)
    expect(launchNativeBrowser).toHaveBeenCalledWith({
      executablePath: '/native/browser',
      pixelFormat: 'i420',
    })
    expect(recordNative).toHaveBeenCalledWith('beta', callback, { source })
    expect(recorded.backend).toBe('native')
    // eslint-disable-next-line @typescript-eslint/unbound-method -- Inspecting a mock.
    expect(source.close).toHaveBeenCalledTimes(1)
  })

  it('rejects unsupported backend options before launching', async () => {
    await expect(
      record('invalid backend', vi.fn(), { backend: 'external' } as never),
    ).rejects.toThrow('Unsupported SuiteCut backend')
    expect(launchNativeBrowser).not.toHaveBeenCalled()
  })

  it('does not silently fall back when the executable is missing or launch fails', async () => {
    vi.stubEnv('SUITECUT_NATIVE_EXECUTABLE', '')
    expect(() => launchBrowser()).toThrow('requires a native executable')
    vi.stubEnv('SUITECUT_NATIVE_EXECUTABLE', '/bad/browser')
    vi.mocked(launchNativeBrowser).mockRejectedValue(new Error('CEF launch failed'))
    await expect(record('beta', vi.fn())).rejects.toThrow('CEF launch failed')
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
