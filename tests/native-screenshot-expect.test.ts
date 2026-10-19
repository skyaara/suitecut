import { execFile } from 'node:child_process'
import { mkdtemp, readFile, rm, stat, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join, resolve } from 'node:path'
import process from 'node:process'
import { promisify } from 'node:util'

import { describe, expect, it } from 'vitest'

import { captureNativeScreenshot } from '../src/native-artifacts.js'
import {
  compareNativeScreenshot,
  parseNativeScreenshotAssertionOptions,
} from '../src/native-screenshot-expect.js'
import { type UntrustedInput } from '../src/untrusted.js'

import { InputFixture, png, result } from './native-screenshot-input-fixture.js'

describe('explicit-baseline native screenshot boundary', () => {
  it('admits one original native deadline before bounded baseline chunks and stable capture', async () => {
    const controller = new AbortController()
    const fixture = new InputFixture()
    const comparison = await compareNativeScreenshot(
      fixture.run,
      png,
      { signal: controller.signal, timeout: 37 },
      {},
      false,
      undefined,
      fixture.source,
    )
    expect(fixture.actions[0]).toMatchObject({
      action: 'comparePrepare',
      file: false,
      pngBytes: png.length,
      timeoutMs: 37,
    })
    expect(fixture.actions.find((a) => a.action === 'compareCommit')).toMatchObject({
      timeoutMs: 0,
    })
    expect(
      Buffer.concat(fixture.chunks.filter((c) => c.section === 'png').map((c) => c.bytes)),
    ).toEqual(png)
    expect(fixture.config()).toMatchObject({
      isNot: false,
      screenshot: { animations: 'disabled', scale: 'css' },
    })
    expect(comparison.result.passed).toBe(true)
    expect(comparison.actual).toEqual(png)
  })
  it('preserves disabled zero at native admission and transport commands', async () => {
    const fixture = new InputFixture()
    await compareNativeScreenshot(
      fixture.run,
      png,
      { timeout: 0 },
      {},
      false,
      undefined,
      fixture.source,
    )
    expect(fixture.actions.every((a) => a.timeoutMs === 0)).toBe(true)
  })
  it('keeps ordinary capture defaults while sharing typed screenshot conversion', async () => {
    let observed: UntrustedInput
    await captureNativeScreenshot((params) => {
      observed = params
      return Promise.resolve({ data: png.toString('base64') })
    })
    expect(observed).toMatchObject({
      action: 'screenshot',
      format: 'png',
      animations: 'allow',
      scale: 'device',
    })
  })
  it.skipIf(process.platform === 'win32')(
    'delegates a genuine FIFO path to native admission without opening it in the SDK',
    async () => {
      const directory = await mkdtemp(join(tmpdir(), 'suitecut-native-screenshot-fifo-'))
      try {
        const path = join(directory, 'baseline.png')
        await promisify(execFile)('mkfifo', [path])
        const fixture = new InputFixture(() =>
          Promise.reject(new Error('Native screenshot baseline must be a regular PNG file')),
        )
        await expect(
          compareNativeScreenshot(fixture.run, path, {}, {}, false, undefined, fixture.source),
        ).rejects.toThrow('regular PNG file')
        expect(fixture.actions[0]).toMatchObject({ file: true, pngBytes: 0 })
        expect(fixture.config()).toMatchObject({ baselinePath: resolve(path) })
        expect(fixture.chunks.filter((c) => c.section === 'png')).toHaveLength(0)
      } finally {
        await rm(directory, { recursive: true, force: true })
      }
    },
  )
  it('preserves the explicit baseline and forwards only actual native artifact publication paths', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-native-screenshot-'))
    try {
      const path = join(directory, 'baseline.png')
      await writeFile(path, png)
      const actualPath = join(directory, 'native-owned', 'actual.png')
      const diffPath = join(directory, 'native-owned', 'diff.png')
      const fixture = new InputFixture(() =>
        Promise.resolve({ ...result(false), actualPath, diffPath }),
      )
      const failure = await compareNativeScreenshot(
        fixture.run,
        path,
        { artifactsDir: directory },
        {},
        false,
        undefined,
        fixture.source,
      )
      expect(fixture.config()).toMatchObject({ baselinePath: path, artifactsDir: directory })
      expect(await readFile(path)).toEqual(png)
      expect(failure.actualPath).toBe(actualPath)
      expect(failure.diffPath).toBe(diffPath)
      // This controlled transport supplies paths, never files. The SDK must
      // not open/create them; actual writer filesystem controls live in C++.
      await expect(stat(join(directory, 'native-owned'))).rejects.toMatchObject({ code: 'ENOENT' })
      expect(failure.observedMismatch).toBe(false)
      expect(failure.message).toContain('Different pixels: 1/1')
    } finally {
      await rm(directory, { recursive: true, force: true })
    }
  })
  it('encodes only the real locator string field before nesting it in chunked screenshot configuration', async () => {
    const fixture = new InputFixture()
    await compareNativeScreenshot(
      fixture.run,
      png,
      {},
      {},
      false,
      { selector: '[id="\ud800"]' },
      fixture.source,
    )
    expect(fixture.config()).toMatchObject({
      screenshot: { element: { selectorJSON: JSON.stringify('[id="\ud800"]') } },
    })
  })
  it('validates explicit options and matcher timeout without named-baseline behavior', () => {
    expect(parseNativeScreenshotAssertionOptions({}, 43).timeout).toBe(43)
    expect(parseNativeScreenshotAssertionOptions({ timeout: 0 }, 43).timeout).toBe(0)
    expect(() => parseNativeScreenshotAssertionOptions({ name: 'implicit.png' })).toThrow()
    expect(() => parseNativeScreenshotAssertionOptions({ threshold: 1.1 })).toThrow()
    expect(() => parseNativeScreenshotAssertionOptions({ maxDiffPixelRatio: -1 })).toThrow()
  })
  it('rejects cross-page masks and locator clip before native admission', async () => {
    const fixture = new InputFixture()
    await expect(
      compareNativeScreenshot(
        fixture.run,
        png,
        { mask: [{ nativeScreenshotDescriptor: () => ({ page: {}, target: { selector: 'x' } }) }] },
        {},
        false,
        undefined,
        fixture.source,
      ),
    ).rejects.toThrow('different native page')
    await expect(
      compareNativeScreenshot(
        fixture.run,
        png,
        { clip: { x: 0, y: 0, width: 1, height: 1 } },
        {},
        false,
        { selector: 'x' },
        fixture.source,
      ),
    ).rejects.toThrow('do not accept clip')
    expect(fixture.actions).toHaveLength(0)
  })
  it('rejects invalid native output and already-aborted input', async () => {
    const fixture = new InputFixture(() => Promise.resolve({ ...result(), actual: 'AAAA' }))
    await expect(
      compareNativeScreenshot(fixture.run, png, {}, {}, false, undefined, fixture.source),
    ).rejects.toThrow('PNG')
    const controller = new AbortController()
    controller.abort()
    const aborted = new InputFixture()
    await expect(
      compareNativeScreenshot(
        aborted.run,
        png,
        { signal: controller.signal },
        {},
        false,
        undefined,
        aborted.source,
      ),
    ).rejects.toThrow()
    expect(aborted.actions).toHaveLength(0)
  })
})
