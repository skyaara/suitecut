import { mkdtemp, rm, writeFile } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { pathToFileURL } from 'node:url'

import { afterEach, describe, expect, it } from 'vitest'

import { loadAudioPlugin, resolveAudioPluginReferences } from '../src/audio-plugin-loader.js'
import { audioPluginReference, encodePcm16Wav } from '../src/audio-plugin.js'

const temporaryDirectories: string[] = []

afterEach(async () => {
  await Promise.all(
    temporaryDirectories
      .splice(0)
      .map((directory) => rm(directory, { recursive: true, force: true })),
  )
})

describe('SuiteCut audio plugins', () => {
  it('encodes a mono 16-bit PCM WAV at the requested sample rate', () => {
    const wav = encodePcm16Wav(new Float32Array([-2, -0.5, 0, 0.5, 2]), 16_000)
    const view = new DataView(wav.buffer)
    expect(new TextDecoder().decode(wav.slice(0, 4))).toBe('RIFF')
    expect(new TextDecoder().decode(wav.slice(8, 12))).toBe('WAVE')
    expect(view.getUint16(22, true)).toBe(1)
    expect(view.getUint32(24, true)).toBe(16_000)
    expect(view.getUint16(34, true)).toBe(16)
    expect(view.getInt16(44, true)).toBe(-32_768)
    expect(view.getInt16(52, true)).toBe(32_767)
  })

  it('validates public plugin references', () => {
    expect(
      audioPluginReference({
        provider: 'piper-lessac',
        module: '@suitecut/audio-vits',
        options: { model: 'voice.onnx', tokens: 'tokens.txt' },
      }),
    ).toEqual({
      provider: 'piper-lessac',
      module: '@suitecut/audio-vits',
      options: { model: 'voice.onnx', tokens: 'tokens.txt' },
    })
    expect(() => audioPluginReference({ provider: 'MMS French', module: 'plugin' })).toThrow()
  })

  it('resolves package names from the recording project', () => {
    const references = resolveAudioPluginReferences([{ provider: 'schema-audio', module: 'zod' }])
    expect(references.get('schema-audio')?.module).toMatch(/^file:/u)
    expect(references.get('schema-audio')?.module).toContain('/zod/')
  })

  it('rejects built-in replacements and duplicate provider IDs', () => {
    expect(() => resolveAudioPluginReferences([{ provider: 'kokoro', module: 'plugin' }])).toThrow(
      'cannot replace built-in provider kokoro',
    )
    expect(() =>
      resolveAudioPluginReferences([
        { provider: 'custom', module: 'zod' },
        { provider: 'custom', module: 'zod' },
      ]),
    ).toThrow('Duplicate SuiteCut audio plugin provider: custom')
  })

  it('loads a local module with a default synthesize export', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-audio-plugin-'))
    temporaryDirectories.push(directory)
    const modulePath = join(directory, 'plugin.mjs')
    await writeFile(modulePath, 'export default { async synthesize() {} }\n', 'utf8')
    const plugin = await loadAudioPlugin({
      provider: 'fixture',
      module: pathToFileURL(modulePath).href,
    })
    await expect(
      plugin.synthesize({
        text: 'Ready.',
        voice: 'default',
        speed: 1,
        outputPath: join(directory, 'audio.wav'),
      }),
    ).resolves.toBeUndefined()
  })

  it('rejects a plugin with a non-function dispose export', async () => {
    const directory = await mkdtemp(join(tmpdir(), 'suitecut-audio-plugin-'))
    temporaryDirectories.push(directory)
    const modulePath = join(directory, 'plugin.mjs')
    await writeFile(
      modulePath,
      "export default { async synthesize() {}, dispose: 'invalid' }\n",
      'utf8',
    )

    await expect(
      loadAudioPlugin({ provider: 'fixture', module: pathToFileURL(modulePath).href }),
    ).rejects.toThrow('dispose export must be a function')
  })
})
