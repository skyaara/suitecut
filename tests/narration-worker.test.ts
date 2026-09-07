import { describe, expect, it } from 'vitest'

import {
  isNarrationWorkerRequest,
  isNarrationWorkerResponse,
} from '../src/narration-worker-protocol.js'

const JOB_ID = '00000000-0000-4000-8000-000000000001'

describe('SuiteCut narration worker protocol', () => {
  it('accepts a complete synthesis request', () => {
    expect(
      isNarrationWorkerRequest({
        type: 'synthesize',
        jobId: JOB_ID,
        text: 'The project is ready.',
        provider: 'kokoro',
        speed: 1,
        voice: 'af_heart',
        outputPath: '/tmp/narration.wav',
      }),
    ).toBe(true)
  })

  it('accepts a matching external audio plugin reference', () => {
    expect(
      isNarrationWorkerRequest({
        type: 'synthesize',
        jobId: JOB_ID,
        text: 'Le rapport est prêt.',
        provider: 'piper-lessac',
        speed: 1,
        voice: 'default',
        outputPath: '/tmp/narration.wav',
        plugin: {
          provider: 'piper-lessac',
          module: 'file:///tmp/suitecut-audio-plugin.mjs',
          options: { model: 'Xenova/mms-tts-fra' },
        },
      }),
    ).toBe(true)
  })

  it('requires external plugin references and protects built-in providers', () => {
    const baseRequest = {
      type: 'synthesize',
      jobId: JOB_ID,
      text: 'The project is ready.',
      speed: 1,
      voice: 'default',
      outputPath: '/tmp/narration.wav',
    }
    expect(isNarrationWorkerRequest({ ...baseRequest, provider: 'external' })).toBe(false)
    expect(
      isNarrationWorkerRequest({
        ...baseRequest,
        provider: 'kokoro',
        plugin: { provider: 'kokoro', module: 'file:///tmp/plugin.mjs' },
      }),
    ).toBe(false)
    expect(
      isNarrationWorkerRequest({
        ...baseRequest,
        provider: 'external',
        plugin: { provider: 'different', module: 'file:///tmp/plugin.mjs' },
      }),
    ).toBe(false)
  })

  it('rejects malformed requests', () => {
    expect(
      isNarrationWorkerRequest({
        type: 'synthesize',
        jobId: JOB_ID,
        text: 10,
        provider: 'macos-say',
        speed: 1,
        voice: 'default',
        outputPath: '/tmp/narration.aiff',
      }),
    ).toBe(false)
  })

  it('accepts success and failure responses', () => {
    expect(
      isNarrationWorkerResponse({
        type: 'synthesized',
        jobId: JOB_ID,
      }),
    ).toBe(true)
    expect(
      isNarrationWorkerResponse({
        type: 'synthesized',
        jobId: JOB_ID,
        timing: {
          text: 'The project is ready.',
          durationMs: 1_000,
          words: [{ text: 'The', startOffset: 0, endOffset: 3, startMs: 100, endMs: 250 }],
        },
      }),
    ).toBe(true)
    expect(
      isNarrationWorkerResponse({
        type: 'failed',
        jobId: JOB_ID,
        error: { name: 'Error', message: 'Speech failed' },
      }),
    ).toBe(true)
    expect(isNarrationWorkerResponse({ type: 'closed' })).toBe(true)
    expect(
      isNarrationWorkerResponse({
        type: 'close-failed',
        error: { name: 'Error', message: 'Disposal failed' },
      }),
    ).toBe(true)
  })

  it('rejects invalid IDs, speeds, extra fields, and malformed errors', () => {
    const baseRequest = {
      type: 'synthesize',
      jobId: JOB_ID,
      text: 'The project is ready.',
      provider: 'kokoro',
      speed: 1,
      voice: 'af_heart',
      outputPath: '/tmp/narration.wav',
    }
    expect(isNarrationWorkerRequest({ ...baseRequest, jobId: 'job-1' })).toBe(false)
    expect(isNarrationWorkerRequest({ ...baseRequest, speed: 5 })).toBe(false)
    expect(isNarrationWorkerRequest({ ...baseRequest, extra: true })).toBe(false)
    expect(
      isNarrationWorkerResponse({
        type: 'failed',
        jobId: JOB_ID,
        error: { name: 'Error', message: 42 },
      }),
    ).toBe(false)
  })
})
