import { describe, expect, it } from 'vitest'

import { decodeEventAttachment, SuiteCutSchemaError } from '../src/manifest.js'
import { type SuiteCutEventAttachment } from '../src/types.js'

function attachment(): SuiteCutEventAttachment {
  return {
    attemptId: 'attempt-1',
    clock: {
      originEpochMs: 10_000,
      originMonotonicMs: 500,
      startedAt: '1970-01-01T00:00:10.000Z',
    },
    endedAtMs: 1_000,
    pages: [
      {
        id: 'page-main',
        kind: 'main',
        initialUrl: 'https://example.test',
        createdAtMs: 0,
      },
    ],
    events: [
      {
        id: 'event-1',
        type: 'narration',
        atMs: 100,
        pageId: 'page-main',
        text: 'The page is ready.',
      },
    ],
    artifacts: [
      {
        id: 'audio-1',
        attachmentName: 'audio-1.aiff',
        role: 'narration-audio',
        contentType: 'audio/aiff',
        createdAtMs: 900,
        sourceEventId: 'event-1',
        provider: 'macos-say',
        voice: 'default',
      },
    ],
    videos: [
      {
        artifactId: 'video-1',
        pageId: 'page-main',
        attachmentName: 'video-1.webm',
        firstFrameEpochMs: 10_025,
        sourceStartedAtMs: 25,
      },
    ],
  }
}

describe('event attachment decoding', () => {
  it('decodes fixture transport without mutating it', () => {
    const input = attachment()
    const snapshot = structuredClone(input)
    expect(decodeEventAttachment(input)).toEqual(snapshot)
    expect(input).toEqual(snapshot)
  })

  it('rejects narration artifacts with unknown source events', () => {
    const input = attachment()
    const artifact = input.artifacts[0]
    if (artifact?.role !== 'narration-audio') throw new Error('Narration fixture missing')
    artifact.sourceEventId = 'missing-event'
    expect(() => decodeEventAttachment(input)).toThrowError(SuiteCutSchemaError)
  })

  it('decodes caption timing attachments linked to narration events', () => {
    const input = attachment()
    input.artifacts.push({
      id: 'captions-1',
      attachmentName: 'captions-1.json',
      role: 'captions',
      contentType: 'application/json',
      createdAtMs: 900,
      sourceEventId: 'event-1',
    })
    expect(decodeEventAttachment(input)).toEqual(input)

    const captions = input.artifacts.at(-1)
    if (captions?.role !== 'captions') throw new Error('Caption fixture missing')
    captions.sourceEventId = 'missing-event'
    expect(() => decodeEventAttachment(input)).toThrowError(SuiteCutSchemaError)
  })

  it('accepts source timing after excluded preparation time', () => {
    const input = attachment()
    input.videos[0]!.sourceStartedAtMs = 0
    expect(decodeEventAttachment(input)).toEqual(input)
  })
})
