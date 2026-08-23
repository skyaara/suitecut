import { mkdtemp, readFile, rm } from 'node:fs/promises'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

import { describe, expect, it, vi } from 'vitest'

import {
  encodePcm16Wav,
  normalizeKokoroText,
  splitKokoroText,
  synthesizeKokoro,
} from '../src/kokoro.js'

const bundledSynthesisTest = process.env.SUITECUT_KOKORO_INTEGRATION === '1' ? it : it.skip

describe('Kokoro narration helpers', () => {
  it('normalizes punctuation, titles, ranges, and whitespace', () => {
    expect(normalizeKokoroText('  Dr. Rivera reviewed 1,200-1,500 “items”.  ')).toBe(
      'Doctor Rivera reviewed 1200 to 1500 "items".',
    )
  })

  it('splits long narration without dropping text', () => {
    const text = 'First clause, second clause, third clause, fourth clause.'
    const chunks = splitKokoroText(text, 24)
    expect(chunks.length).toBeGreaterThan(1)
    expect(chunks.join(' ').replace(/\s+/gu, ' ')).toBe(text)
    expect(chunks.every((chunk) => chunk.length <= 24)).toBe(true)
  })

  it('writes a mono 24 kHz PCM WAV header and clamped samples', () => {
    const wav = encodePcm16Wav(new Float32Array([-2, -0.5, 0, 0.5, 2]))
    const view = new DataView(wav.buffer)
    expect(new TextDecoder().decode(wav.slice(0, 4))).toBe('RIFF')
    expect(new TextDecoder().decode(wav.slice(8, 12))).toBe('WAVE')
    expect(view.getUint16(22, true)).toBe(1)
    expect(view.getUint32(24, true)).toBe(24_000)
    expect(view.getUint16(34, true)).toBe(16)
    expect(view.getInt16(44, true)).toBe(-32_768)
    expect(view.getInt16(52, true)).toBe(32_767)
  })

  it('rejects empty, non-finite, and invalid-rate WAV inputs', () => {
    expect(() => encodePcm16Wav(new Float32Array())).toThrow('empty waveform')
    expect(() => encodePcm16Wav(new Float32Array([Number.NaN]))).toThrow('non-finite')
    expect(() => encodePcm16Wav(new Float32Array([0]), 0)).toThrow('positive integer')
  })

  it('rejects unknown voice IDs before fetching model assets', async () => {
    await expect(synthesizeKokoro('Hello.', '../unknown', 1, '/tmp/unused.wav')).rejects.toThrow(
      'Kokoro voice is unavailable',
    )
  })

  it('validates synthesis text, speed, and output path before loading assets', async () => {
    await expect(synthesizeKokoro('', 'af_heart', 1, '/tmp/unused.wav')).rejects.toThrow()
    await expect(synthesizeKokoro('Hello.', 'af_heart', 3, '/tmp/unused.wav')).rejects.toThrow()
    await expect(synthesizeKokoro('Hello.', 'af_heart', 1, '')).rejects.toThrow()
  })

  bundledSynthesisTest(
    'synthesizes the bundled default voice without a network request',
    async () => {
      const directory = await mkdtemp(join(tmpdir(), 'suitecut-kokoro-'))
      const outputPath = join(directory, 'narration.wav')
      const originalFetch = globalThis.fetch
      const fetchSpy = vi.fn((input: string | URL | Request, init?: RequestInit) => {
        const url =
          typeof input === 'string' ? input : input instanceof URL ? input.href : input.url
        if (/^https?:/u.test(url)) return Promise.reject(new Error(`Network disabled: ${url}`))
        return originalFetch(input, init)
      })
      vi.stubGlobal('fetch', fetchSpy)

      try {
        await synthesizeKokoro('The SuiteCut recording is ready.', 'af_heart', 1, outputPath)
        const wav = await readFile(outputPath)
        expect(wav.subarray(0, 4).toString()).toBe('RIFF')
        expect(wav.byteLength).toBeGreaterThan(44)
        expect(
          fetchSpy.mock.calls.some(([input]) => {
            const url =
              typeof input === 'string' ? input : input instanceof URL ? input.href : input.url
            return /^https?:/u.test(url)
          }),
        ).toBe(false)
      } finally {
        vi.unstubAllGlobals()
        await rm(directory, { recursive: true, force: true })
      }
    },
    120_000,
  )
})
