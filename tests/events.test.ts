import { describe, expect, it } from 'vitest'

import { decodeManifest, SuiteCutSchemaError } from '../src/manifest.js'
import { type SuiteCutEvent } from '../src/types.js'
import { type UntrustedInput } from '../src/untrusted.js'

import { cloneManifest } from './fixtures/manifest.js'

const viewport = { width: 1280, height: 720, deviceScaleFactor: 1, scrollX: 0, scrollY: 0 }

function decodeEvent(event: SuiteCutEvent): SuiteCutEvent {
  const manifest = cloneManifest()
  manifest.tests[0]!.attempts[0]!.events = [event]
  return decodeManifest(manifest).tests[0]!.attempts[0]!.events[0]!
}

function expectEventError(event: UntrustedInput, path: string): void {
  const manifest = cloneManifest()
  manifest.tests[0]!.attempts[0]!.events = [event as SuiteCutEvent]
  try {
    decodeManifest(manifest)
    expect.fail('Expected the event to be rejected')
  } catch (error) {
    expect(error).toBeInstanceOf(SuiteCutSchemaError)
    expect(error).toMatchObject({ path })
  }
}

describe('manifest event decoding', () => {
  it.each<SuiteCutEvent>([
    {
      id: 'narration-1',
      type: 'narration',
      atMs: 10,
      pageId: 'page-main',
      text: 'Create the project.',
      provider: 'kokoro',
      voice: 'af_heart',
      speed: 1.1,
      caption: 'Create project',
    },
    {
      id: 'checkpoint-1',
      type: 'checkpoint',
      atMs: 20,
      pageId: 'page-main',
      label: 'Project created',
      artifactId: 'artifact-checkpoint',
      viewport,
    },
    {
      id: 'move-1',
      type: 'pointer-move',
      atMs: 30,
      pageId: 'page-main',
      point: { x: 100, y: 80 },
      pointerType: 'mouse',
      viewport,
      targetRect: { x: 90, y: 70, width: 120, height: 40 },
    },
    {
      id: 'down-1',
      type: 'pointer-down',
      atMs: 40,
      pageId: 'page-main',
      point: { x: 100, y: 80 },
      pointerType: 'mouse',
      button: 'left',
      viewport,
    },
    {
      id: 'up-1',
      type: 'pointer-up',
      atMs: 50,
      pageId: 'page-main',
      point: { x: 100, y: 80 },
      pointerType: 'mouse',
      button: 'left',
      viewport,
    },
    {
      id: 'click-1',
      type: 'click',
      atMs: 60,
      pageId: 'page-main',
      point: { x: 100, y: 80 },
      pointerType: 'mouse',
      button: 'left',
      viewport,
    },
    {
      id: 'highlight-1',
      type: 'highlight',
      atMs: 70,
      pageId: 'page-main',
      rect: { x: 90, y: 70, width: 120, height: 40 },
      viewport,
      options: {
        durationMs: 1_400,
        mode: 'spotlight',
        borderColor: '#22C55E',
        fillOpacity: 0.1,
        enter: { type: 'fade-scale', durationMs: 180, easing: 'ease-out' },
      },
    },
    {
      id: 'zoom-1',
      type: 'zoom',
      atMs: 80,
      pageId: 'page-main',
      rect: { x: 40, y: 40, width: 640, height: 480 },
      viewport,
      options: { scale: 1.18, paddingPx: 32, holdMs: 1_200 },
    },
    {
      id: 'hold-1',
      type: 'hold',
      atMs: 90,
      pageId: 'page-main',
      durationMs: 800,
      reason: 'author',
    },
  ])('accepts a valid $type event', (event) => {
    expect(decodeEvent(event)).toEqual(event)
  })

  it('allows narration and zoom to start together on the same page', () => {
    const manifest = cloneManifest()
    manifest.tests[0]!.attempts[0]!.events = [
      {
        id: 'narration-1',
        type: 'narration',
        atMs: 100,
        pageId: 'page-main',
        text: 'The final total is shown here.',
      },
      {
        id: 'zoom-1',
        type: 'zoom',
        atMs: 100,
        pageId: 'page-main',
        rect: { x: 840, y: 120, width: 280, height: 96 },
        viewport,
        options: { scale: 1.4, holdMs: 1_200 },
      },
    ]

    expect(decodeManifest(manifest).tests[0]!.attempts[0]!.events).toEqual(
      manifest.tests[0]!.attempts[0]!.events,
    )
  })

  it('rejects an unknown event type', () => {
    expectEventError(
      { id: 'event-unknown', type: 'confetti', atMs: 100 },
      'tests[0].attempts[0].events[0].type',
    )
  })

  it('checks fields required by the selected event type', () => {
    expectEventError(
      {
        id: 'click-1',
        type: 'click',
        atMs: 100,
        pageId: 'page-main',
        pointerType: 'mouse',
        button: 'left',
        viewport,
      },
      'tests[0].attempts[0].events[0].point',
    )
  })

  it.each([
    [
      'negative time',
      { id: 'event-1', type: 'narration', atMs: -1, pageId: 'page-main', text: 'No.' },
      'atMs',
    ],
    ['empty ID', { id: '', type: 'narration', atMs: 1, pageId: 'page-main', text: 'No.' }, 'id'],
    [
      'non-finite time',
      { id: 'event-1', type: 'narration', atMs: Number.NaN, pageId: 'page-main', text: 'No.' },
      'atMs',
    ],
    [
      'invalid opacity',
      {
        id: 'event-1',
        type: 'highlight',
        atMs: 1,
        pageId: 'page-main',
        rect: { x: 0, y: 0, width: 10, height: 10 },
        viewport,
        options: { fillOpacity: 1.1 },
      },
      'options.fillOpacity',
    ],
    [
      'invalid zoom scale',
      {
        id: 'event-1',
        type: 'zoom',
        atMs: 1,
        pageId: 'page-main',
        rect: { x: 0, y: 0, width: 10, height: 10 },
        viewport,
        options: { scale: 0.9 },
      },
      'options.scale',
    ],
  ])('rejects %s', (_name, event, suffix) => {
    expectEventError(event, `tests[0].attempts[0].events[0].${suffix}`)
  })

  it('requires events to be ordered by attempt time', () => {
    const manifest = cloneManifest()
    manifest.tests[0]!.attempts[0]!.events = [
      { id: 'event-2', type: 'narration', atMs: 20, pageId: 'page-main', text: 'Second.' },
      { id: 'event-1', type: 'narration', atMs: 10, pageId: 'page-main', text: 'First.' },
    ]
    expect(() => decodeManifest(manifest)).toThrowError(SuiteCutSchemaError)
  })

  it('requires event IDs to be unique within an attempt', () => {
    const manifest = cloneManifest()
    manifest.tests[0]!.attempts[0]!.events = [
      { id: 'event-1', type: 'narration', atMs: 10, pageId: 'page-main', text: 'First.' },
      { id: 'event-1', type: 'narration', atMs: 20, pageId: 'page-main', text: 'Second.' },
    ]
    expect(() => decodeManifest(manifest)).toThrowError(SuiteCutSchemaError)
  })
})
