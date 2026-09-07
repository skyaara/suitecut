import { describe, expect, it } from 'vitest'

import { normalizeKokoroText, splitKokoroText, synthesizeKokoro } from '../src/kokoro.js'

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
})
