// SPDX-License-Identifier: MIT
import { describe, expect, it } from 'vitest'

import { normalizeKokoroText, splitKokoroText, synthesizeKokoro, wordSpans } from '../src/kokoro.js'

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

  it('gives each word its own phoneme runs when there are enough', () => {
    // Two words, three runs of two tokens each; every token lasts one unit.
    const runs = [
      { startToken: 0, endToken: 2 },
      { startToken: 3, endToken: 5 },
      { startToken: 6, endToken: 8 },
    ]
    const cumulative = Array.from({ length: 10 }, (_, index) => index)
    expect(wordSpans([1, 2], runs, cumulative)).toEqual([
      { startUnits: 1, endUnits: 3 },
      { startUnits: 4, endUnits: 9 },
    ])
  })

  it('shares a phoneme run between words that espeak joined, such as "in the"', () => {
    // "steps in the script": espeak gives three runs for four words.
    const runs = [
      { startToken: 0, endToken: 4 },
      { startToken: 5, endToken: 9 },
      { startToken: 10, endToken: 16 },
    ]
    const cumulative = Array.from({ length: 18 }, (_, index) => index)
    const spans = wordSpans([1, 1, 1, 1], runs, cumulative)
    expect(spans).toHaveLength(4)
    expect(spans.map((span) => span.startUnits)).toEqual(
      [...spans.map((span) => span.startUnits)].sort((a, b) => a - b),
    )
    expect(spans[0]).toEqual({ startUnits: 1, endUnits: 5 })
    expect(spans.at(-1)?.endUnits).toBe(17)
    for (const span of spans) expect(span.endUnits).toBeGreaterThan(span.startUnits)
  })
})
