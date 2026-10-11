import { describe, expect, it } from 'vitest'

import { buildSuiteCutSequence } from '../src/timeline.js'

import { createManifest } from './fixtures/manifest.js'

describe('render timeline', () => {
  it('uses the first authored main-page frame as a poster over leading browser startup', () => {
    const attempt = createManifest().tests[0]!.attempts[0]!
    attempt.durationMs = 2_000
    attempt.pages[0]!.closedAtMs = 2_000
    attempt.events = [
      {
        id: 'checkpoint-ready',
        type: 'checkpoint',
        atMs: 610,
        pageId: 'page-main',
        label: 'Ready',
        artifactId: 'artifact-checkpoint',
        durationMs: 500,
        viewport: { width: 1280, height: 720, scrollX: 0, scrollY: 0 },
      },
    ]

    expect(buildSuiteCutSequence(attempt, 0, 30)).toEqual([
      {
        kind: 'hold',
        pageId: 'page-main',
        executionStartMs: 633.3333333333333,
        executionEndMs: 633.3333333333333,
        durationMs: 633.3333333333333,
      },
      {
        kind: 'play',
        pageId: 'page-main',
        executionStartMs: 633.3333333333333,
        executionEndMs: 2_000,
        durationMs: 1_366.6666666666667,
      },
    ])
  })

  it('returns to the opener after a popup closes and keeps it for the result hold', () => {
    const attempt = createManifest().tests[0]!.attempts[0]!
    attempt.durationMs = 3_000
    attempt.pages[0]!.closedAtMs = 3_000
    attempt.pages.push({
      id: 'page-popup',
      kind: 'popup',
      openerPageId: 'page-main',
      initialUrl: 'https://example.test/popup',
      createdAtMs: 1_000,
      closedAtMs: 2_000,
    })
    attempt.events = [
      {
        id: 'select-popup',
        type: 'page-selected',
        atMs: 1_000,
        pageId: 'page-popup',
        reason: 'opened',
      },
      {
        id: 'select-main',
        type: 'page-selected',
        atMs: 2_000,
        pageId: 'page-main',
        reason: 'closed',
      },
    ]

    expect(buildSuiteCutSequence(attempt, 500, 30)).toEqual([
      {
        kind: 'play',
        pageId: 'page-main',
        executionStartMs: 0,
        executionEndMs: 1_000,
        durationMs: 1_000,
      },
      {
        kind: 'play',
        pageId: 'page-popup',
        executionStartMs: 1_000,
        executionEndMs: 2_000,
        durationMs: 1_000,
      },
      {
        kind: 'play',
        pageId: 'page-main',
        executionStartMs: 2_000,
        executionEndMs: 3_000,
        durationMs: 1_000,
      },
      {
        kind: 'hold',
        pageId: 'page-main',
        executionStartMs: 3_000,
        executionEndMs: 3_000,
        durationMs: 500,
      },
    ])
  })

  it('keeps event-based page order for legacy manifests', () => {
    const attempt = createManifest().tests[0]!.attempts[0]!
    attempt.pages.push({
      id: 'page-popup',
      kind: 'popup',
      openerPageId: 'page-main',
      initialUrl: 'https://example.test/popup',
      createdAtMs: 1_000,
    })
    attempt.events = [
      {
        id: 'legacy-popup-event',
        type: 'narration',
        atMs: 1_000,
        pageId: 'page-popup',
        text: 'The popup is open.',
      },
    ]

    expect(buildSuiteCutSequence(attempt, 0, 30).map((item) => item.pageId)).toEqual([
      'page-main',
      'page-popup',
    ])
  })
})
