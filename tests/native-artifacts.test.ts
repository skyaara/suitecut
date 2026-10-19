import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { describe, expect, it, vi } from 'vitest'

import {
  captureNativeScreenshot,
  captureNativeElementScreenshot,
  printNativePDF,
} from '../src/native-artifacts.js'

const png = Buffer.from([137, 80, 78, 71, 13, 10, 26, 10])
const pdf = Buffer.from('%PDF-1.7\n')

describe('native artifact boundaries', () => {
  it('aborts before native dispatch or file output', async () => {
    const run = vi.fn().mockResolvedValue({ data: png.toString('base64') })
    const controller = new AbortController()
    controller.abort(new Error('capture canceled before dispatch'))
    await expect(
      captureNativeScreenshot(run, { signal: controller.signal, path: '/unused.png' }),
    ).rejects.toThrow('capture canceled before dispatch')
    expect(run).not.toHaveBeenCalled()
  })
  it('rejects unsupported options before native dispatch', async () => {
    const run = vi.fn().mockResolvedValue({ data: png.toString('base64') })
    await expect(captureNativeScreenshot(run, { quality: 75 })).rejects.toThrow(
      'unsupported for the png',
    )
    await expect(
      captureNativeScreenshot(run, { clip: { x: NaN, y: 0, width: 1, height: 1 } }),
    ).rejects.toThrow()
    expect(run).not.toHaveBeenCalled()
  })
  it('allows clipping the full document independently of viewport scroll', async () => {
    const run = vi.fn().mockResolvedValue({ data: png.toString('base64') })
    await captureNativeScreenshot(run, {
      fullPage: true,
      clip: { x: 0, y: 800, width: 10, height: 10 },
    })
    expect(run).toHaveBeenCalledWith(
      expect.objectContaining({ fullPage: true, clip: { x: 0, y: 800, width: 10, height: 10 } }),
      undefined,
    )
  })

  it('rejects caller supplied element clips before native geometry resolution', async () => {
    const run = vi.fn().mockResolvedValue({ data: png.toString('base64') })
    const options = { type: 'png' as const, clip: { x: 0, y: 0, width: 1, height: 1 } }
    await expect(
      captureNativeElementScreenshot(run, { selector: '#item' }, options),
    ).rejects.toThrow()
    expect(run).not.toHaveBeenCalled()
  })

  it('rejects masks owned by a different page before native capture', async () => {
    const run = vi.fn().mockResolvedValue({ data: png.toString('base64') })
    const mask = {
      nativeScreenshotDescriptor: () => ({ page: {}, target: { selector: '#private' } }),
    }
    await expect(captureNativeScreenshot(run, { mask: [mask] }, {})).rejects.toThrow(
      'different native page',
    )
    expect(run).not.toHaveBeenCalled()
  })

  it('validates encoded data and the declared document type', async () => {
    await expect(captureNativeScreenshot(() => Promise.resolve({ data: '!!!!' }))).rejects.toThrow(
      'encoding',
    )
    await expect(
      captureNativeScreenshot(() => Promise.resolve({ data: pdf.toString('base64') })),
    ).rejects.toThrow('png')
    await expect(
      printNativePDF(() => Promise.resolve({ data: png.toString('base64') })),
    ).rejects.toThrow('pdf')
  })

  it('returns and atomically writes the same native bytes', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-artifact-test-'))
    try {
      const path = join(directory, 'nested', 'capture.PNG')
      const captured = await captureNativeScreenshot(
        () => Promise.resolve({ data: png.toString('base64') }),
        { path },
      )
      expect(captured).toEqual(png)
      expect(await readFile(path)).toEqual(png)
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })

  it('converts mixed physical and CSS units before native PDF layout', async () => {
    const run = vi.fn().mockResolvedValue({ data: pdf.toString('base64') })
    await printNativePDF(run, {
      width: '25.4cm',
      height: 960,
      margin: { left: '25.4mm', top: '1in', right: 96 },
    })
    expect(run).toHaveBeenCalledWith(
      expect.objectContaining({
        paperWidth: 10,
        paperHeight: 10,
        marginLeft: 1,
        marginTop: 1,
        marginRight: 1,
      }),
    )
  })

  it('uses named paper size in preference to custom dimensions', async () => {
    const run = vi.fn().mockResolvedValue({ data: pdf.toString('base64') })
    await printNativePDF(run, { format: 'a4', width: '1in', height: '1in' })
    expect(run).toHaveBeenCalledWith(
      expect.objectContaining({ paperWidth: 8.27, paperHeight: 11.7 }),
    )
  })

  it('bounds custom print geometry before allocating native output', async () => {
    const run = vi.fn().mockResolvedValue({ data: pdf.toString('base64') })
    await expect(printNativePDF(run, { width: '100000in' })).rejects.toThrow('200 inches')
    await expect(printNativePDF(run, { height: 0 })).rejects.toThrow('greater than zero')
    expect(run).not.toHaveBeenCalled()
  })
})
