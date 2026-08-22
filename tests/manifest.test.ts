import { describe, expect, it } from 'vitest'

import { decodeManifest, SuiteCutSchemaError } from '../src/manifest.js'
import { type SuiteCutArtifact, type SuiteCutManifest } from '../src/types.js'
import { type UntrustedInput } from '../src/untrusted.js'

import { cloneManifest, createManifest } from './fixtures/manifest.js'

function attempt(manifest: SuiteCutManifest = cloneManifest()) {
  return manifest.tests[0]!.attempts[0]!
}

function expectSchemaError(manifest: UntrustedInput, path: string): void {
  try {
    decodeManifest(manifest)
    expect.fail('Expected the manifest to be rejected')
  } catch (error) {
    expect(error).toBeInstanceOf(SuiteCutSchemaError)
    expect(error).toMatchObject({ path, code: expect.any(String) })
  }
}

describe('manifest decoding', () => {
  it('decodes a complete manifest without mutating its input', () => {
    const input = createManifest()
    const snapshot = structuredClone(input)
    expect(decodeManifest(input)).toEqual(snapshot)
    expect(input).toEqual(snapshot)
  })

  it('accepts negative step times for setup before the attempt clock', () => {
    const manifest = cloneManifest()
    attempt(manifest).steps[0]!.atMs = -25
    expect(decodeManifest(manifest)).toEqual(manifest)
  })

  it.each([Number.NaN, Number.POSITIVE_INFINITY, Number.NEGATIVE_INFINITY])(
    'rejects the non-finite duration %s',
    (durationMs) => {
      const manifest = cloneManifest()
      attempt(manifest).durationMs = durationMs
      expectSchemaError(manifest, 'tests[0].attempts[0].durationMs')
    },
  )

  it('rejects unresolved checkpoint artifacts and page references', () => {
    const missingArtifact = cloneManifest()
    const checkpoint = attempt(missingArtifact).events[1]
    if (checkpoint?.type !== 'checkpoint') throw new Error('Fixture checkpoint missing')
    checkpoint.artifactId = 'artifact-missing'
    expectSchemaError(missingArtifact, 'tests[0].attempts[0].events[1].artifactId')

    const missingPage = cloneManifest()
    const pageEvent = attempt(missingPage).events[1]
    if (pageEvent?.type !== 'checkpoint') throw new Error('Fixture checkpoint missing')
    pageEvent.pageId = 'page-missing'
    expectSchemaError(missingPage, 'tests[0].attempts[0].events[1].pageId')
  })

  it('rejects missing and cyclic page opener references', () => {
    const missing = cloneManifest()
    attempt(missing).pages.push({
      id: 'page-popup',
      kind: 'popup',
      openerPageId: 'page-missing',
      initialUrl: 'about:blank',
      createdAtMs: 1_000,
    })
    expectSchemaError(missing, 'tests[0].attempts[0].pages[1].openerPageId')

    const cyclic = cloneManifest()
    attempt(cyclic).pages[0]!.openerPageId = 'page-popup'
    attempt(cyclic).pages.push({
      id: 'page-popup',
      kind: 'popup',
      openerPageId: 'page-main',
      initialUrl: 'about:blank',
      createdAtMs: 1_000,
    })
    expectSchemaError(cyclic, 'tests[0].attempts[0].pages')
  })

  it('rejects missing and cyclic step parent references', () => {
    const missing = cloneManifest()
    attempt(missing).steps[0]!.parentStepId = 'step-missing'
    expectSchemaError(missing, 'tests[0].attempts[0].steps[0].parentStepId')

    const cyclic = cloneManifest()
    attempt(cyclic).steps = [
      {
        id: 'step-1',
        parentStepId: 'step-2',
        title: 'first',
        category: 'test.step',
        atMs: 10,
        durationMs: 10,
        outcome: 'passed',
      },
      {
        id: 'step-2',
        parentStepId: 'step-1',
        title: 'second',
        category: 'pw:api',
        atMs: 11,
        durationMs: 5,
        outcome: 'passed',
      },
    ]
    expectSchemaError(cyclic, 'tests[0].attempts[0].steps')
  })

  it('requires one source-video artifact for each page', () => {
    const missing = cloneManifest()
    attempt(missing).artifacts = attempt(missing).artifacts.filter(
      (artifact: SuiteCutArtifact) => artifact.role !== 'source-video',
    )
    expectSchemaError(missing, 'tests[0].attempts[0].artifacts')

    const duplicate = cloneManifest()
    const video = attempt(duplicate).artifacts.find(
      (artifact: SuiteCutArtifact) => artifact.role === 'source-video',
    )!
    attempt(duplicate).artifacts.push({ ...video, id: 'artifact-video-copy' })
    expectSchemaError(duplicate, 'tests[0].attempts[0].artifacts')
  })

  it('uses explicit artifact roles instead of file extensions', () => {
    const manifest = cloneManifest()
    const video = attempt(manifest).artifacts.find(
      (artifact: SuiteCutArtifact) => artifact.id === 'artifact-video',
    )!
    video.name = 'recording.bin'
    video.path = '/workspace/app/test-results/recording.bin'
    expect(decodeManifest(manifest)).toEqual(manifest)
  })

  it('requires source-video artifacts to identify their page', () => {
    const manifest = cloneManifest()
    delete attempt(manifest).artifacts.find(
      (artifact: SuiteCutArtifact) => artifact.id === 'artifact-video',
    )!.pageId
    expectSchemaError(manifest, 'tests[0].attempts[0].artifacts[1].pageId')
  })

  it('rejects media that references an unknown artifact', () => {
    const manifest = cloneManifest()
    attempt(manifest).media[0]!.artifactId = 'artifact-missing'
    expectSchemaError(manifest, 'tests[0].attempts[0].media[0].artifactId')
  })

  it('requires one timing record for each page', () => {
    const missing = cloneManifest()
    attempt(missing).videoTiming = []
    expectSchemaError(missing, 'tests[0].attempts[0].videoTiming')

    const duplicate = cloneManifest()
    attempt(duplicate).videoTiming.push({ ...attempt(duplicate).videoTiming[0]! })
    expectSchemaError(duplicate, 'tests[0].attempts[0].videoTiming')
  })

  it('requires timing media and pages to resolve to the same source video', () => {
    const manifest = cloneManifest()
    attempt(manifest).videoTiming[0]!.pageId = 'page-missing'
    expectSchemaError(manifest, 'tests[0].attempts[0].videoTiming[0].pageId')
  })

  it('validates first-frame timing against the attempt clock', () => {
    const manifest = cloneManifest()
    attempt(manifest).videoTiming[0]!.sourceStartedAtMs = 181
    expectSchemaError(manifest, 'tests[0].attempts[0].videoTiming[0].sourceStartedAtMs')
  })

  it('keeps retry timelines separate', () => {
    const manifest = cloneManifest()
    const retry = structuredClone(attempt(manifest))
    retry.id = 'attempt-2'
    retry.retry = 1
    retry.status = 'failed'
    retry.events[0]!.id = 'retry-event-1'
    manifest.tests[0]!.attempts.push(retry)

    const decoded = decodeManifest(manifest)
    const firstEvents = decoded.tests[0]!.attempts[0]!.events
    const retryEvents = decoded.tests[0]!.attempts[1]!.events
    expect(retryEvents).not.toBe(firstEvents)
    expect(firstEvents[0]!.id).toBe('event-1')
    expect(retryEvents[0]!.id).toBe('retry-event-1')
  })
})
