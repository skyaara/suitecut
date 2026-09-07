import { describe, expect, it } from 'vitest'

import { decodeWordTimingArtifact, encodeWordTimingArtifact } from '../src/captions.js'
import { type UntrustedInput } from '../src/untrusted.js'

const artifact = {
  schemaVersion: 1 as const,
  type: 'word-timings' as const,
  sourceEventId: 'event-1',
  text: 'Create a report.',
  durationMs: 1_000,
  words: [
    { text: 'Create', startOffset: 0, endOffset: 6, startMs: 100, endMs: 390 },
    { text: 'a', startOffset: 7, endOffset: 8, startMs: 420, endMs: 500 },
    { text: 'report', startOffset: 9, endOffset: 15, startMs: 530, endMs: 900 },
  ],
}

describe('word timing artifacts', () => {
  it('round-trips relative word timings', () => {
    const parsed = JSON.parse(encodeWordTimingArtifact(artifact)) as UntrustedInput
    expect(decodeWordTimingArtifact(parsed)).toEqual(artifact)
  })

  it('rejects mismatched text, overlaps, and timings beyond the clip', () => {
    expect(() =>
      decodeWordTimingArtifact({
        ...artifact,
        words: [{ ...artifact.words[0], text: 'Wrong' }],
      }),
    ).toThrow()
    expect(() =>
      decodeWordTimingArtifact({
        ...artifact,
        words: [artifact.words[0], { ...artifact.words[1], startMs: 380 }],
      }),
    ).toThrow()
    expect(() =>
      decodeWordTimingArtifact({
        ...artifact,
        words: [{ ...artifact.words[0], endMs: 1_001 }],
      }),
    ).toThrow()
  })
})
