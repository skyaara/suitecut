// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { type NativeArtifactRunner } from '../src/native-artifacts.js'
import { runNativeArtifactConfiguration } from '../src/native-screenshot-input.js'
import { type UntrustedInput } from '../src/untrusted.js'

import { InputFixture } from './native-screenshot-input-fixture.js'

class ConfigurationFixture extends InputFixture {
  readonly configRun: NativeArtifactRunner = async (params, signal) => {
    this.actions.push(params)
    signal?.throwIfAborted()
    if (params.action === 'capturePrepare')
      return { token: 'nativeScreenshotInput:1', contextId: 3, pageId: 7 }
    if (params.action === 'screenshot' || params.action === 'pdf') return this.completion()
    if (params.action === 'captureCommit') {
      this.finishWatch?.({ committed: true })
      return this.completion()
    }
    throw new Error('Unexpected configuration operation')
  }
}

describe('purpose-owned native artifact configuration upload', () => {
  it('uploads actual style and masks without inventing baseline bytes', async () => {
    const fixture = new ConfigurationFixture()
    const style = 'div{color:red}/*' + 'x'.repeat(140000) + '*/'
    const config = { action: 'screenshot', style, masks: [{ selector: '#sensitive' }] }
    await runNativeArtifactConfiguration(fixture.configRun, fixture.source, config, 1500)
    expect(fixture.config()).toEqual(config)
    expect(fixture.chunks.length).toBeGreaterThan(5)
    expect(
      fixture.chunks.every((chunk) => chunk.section === 'config' && chunk.bytes.length <= 24576),
    ).toBe(true)
    expect(fixture.actions[0]).toMatchObject({
      action: 'capturePrepare',
      kind: 'screenshot',
      timeoutMs: 1500,
    })
    expect(fixture.actions[0]).not.toHaveProperty('pngBytes')
    expect(fixture.actions[0]).not.toHaveProperty('file')
    expect(
      fixture.actions.some(
        (action) =>
          action.action === 'captureCommit' &&
          action.kind === 'screenshot' &&
          action.timeoutMs === 0,
      ),
    ).toBe(true)
    for (const action of fixture.actions)
      expect(
        Buffer.byteLength(
          JSON.stringify({ ...action, id: 2147483647, command: 'context', version: 1, pageId: 1 }),
        ) + 1,
      ).toBeLessThan(65536)
  })
  it('keeps genuine PDF templates and omitted native timeout policy', async () => {
    const fixture = new ConfigurationFixture()
    const config = {
      action: 'pdf',
      headerTemplate: '<div>Actual header</div><!--' + 'h'.repeat(150000) + '-->',
      footerTemplate: '<div>Actual footer</div><!--' + 'f'.repeat(160000) + '-->',
    }
    await runNativeArtifactConfiguration(fixture.configRun, fixture.source, config)
    expect(fixture.config()).toEqual(config)
    expect(fixture.actions[0]).toMatchObject({ action: 'capturePrepare', kind: 'pdf' })
    expect(fixture.actions[0]?.timeoutMs).toBeUndefined()
    expect(
      fixture.actions.some(
        (action) =>
          action.action === 'captureCommit' && action.kind === 'pdf' && action.timeoutMs === 0,
      ),
    ).toBe(true)
  })
  it('preserves disabled timeout and immediate caller abort without losing canonical cleanup', async () => {
    const fixture = new ConfigurationFixture()
    const controller = new AbortController(),
      reason = new Error('actual config canceled')
    controller.abort(reason)
    await expect(
      runNativeArtifactConfiguration(
        fixture.configRun,
        fixture.source,
        { action: 'screenshot', style: 'a{}' },
        0,
        controller.signal,
      ),
    ).rejects.toBe(reason)
    expect(fixture.actions).toHaveLength(0)
    await runNativeArtifactConfiguration(
      fixture.configRun,
      fixture.source,
      { action: 'screenshot' },
      0,
    )
    expect(fixture.actions[0]?.timeoutMs).toBe(0)
  })
  it('rejects unsupported purpose before reserving native input', async () => {
    const fixture = new ConfigurationFixture()
    await expect(
      runNativeArtifactConfiguration(fixture.configRun, fixture.source, { action: 'emulateMedia' }),
    ).rejects.toThrow()
    expect(fixture.actions).toHaveLength(0)
  })
  it('rejects oversized aggregate configuration before native dispatch', async () => {
    const fixture = new ConfigurationFixture()
    await expect(
      runNativeArtifactConfiguration(fixture.configRun, fixture.source, {
        action: 'pdf',
        headerTemplate: 'x'.repeat(16 * 1024 * 1024),
      }),
    ).rejects.toThrow(/configuration exceeds16MiB/)
    expect(fixture.actions).toHaveLength(0)
  })
  it('bounds actual retained SDK bytes across concurrent artifact kinds', async () => {
    const text = '\u4e2d'.repeat(1024 * 1024)
    const config = { action: 'pdf', headerTemplate: text, footerTemplate: text }
    const completions: ((value: UntrustedInput) => void)[] = []
    const pending: Promise<UntrustedInput>[] = []
    for (let i = 0; i < 3; ++i) {
      const completion = new Promise<UntrustedInput>((resolve) => completions.push(resolve))
      const fixture = new ConfigurationFixture(() => completion)
      pending.push(runNativeArtifactConfiguration(fixture.configRun, fixture.source, config, 0))
    }
    const fourth = new ConfigurationFixture()
    await expect(
      runNativeArtifactConfiguration(fourth.configRun, fourth.source, config, 0),
    ).rejects.toThrow(/64MiB staging byte budget/)
    expect(fourth.actions).toHaveLength(0)
    for (const complete of completions) complete({ unitBoundary: true })
    await Promise.all(pending)
  })
})
