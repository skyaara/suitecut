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
        type: 'failed',
        jobId: JOB_ID,
        error: { name: 'Error', message: 'Speech failed' },
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
