import { describe, expect, it } from 'vitest'

import {
  resolveSuiteCutZoomOptions,
  resolveSuiteCutCameraRuns,
  suiteCutZoomFrameForTarget,
  suiteCutZoomScaleAt,
  suiteCutZoomScaleForTarget,
} from '../src/zoom.js'

describe('SuiteCut zoom resolution', () => {
  it('resolves shared defaults and disables timed none animations', () => {
    expect(resolveSuiteCutZoomOptions({})).toMatchObject({
      scale: 1.15,
      paddingPx: 24,
      enter: { type: 'scale', durationMs: 260, easing: 'ease-out' },
      holdMs: 900,
      exit: { type: 'scale', durationMs: 220, easing: 'ease-in-out' },
      totalDurationMs: 1_380,
    })
    expect(
      resolveSuiteCutZoomOptions({
        enter: { type: 'none', durationMs: 500 },
        exit: { type: 'none', durationMs: 500 },
      }),
    ).toMatchObject({
      enter: { type: 'none', durationMs: 0 },
      exit: { type: 'none', durationMs: 0 },
      totalDurationMs: 900,
    })
  })

  it('limits the target scale so requested padding remains visible', () => {
    const resolved = resolveSuiteCutZoomOptions({ scale: 1.25, paddingPx: 100 })
    expect(
      suiteCutZoomScaleForTarget(
        resolved,
        { x: 100, y: 100, width: 800, height: 400 },
        { width: 1280, height: 720, scrollX: 0, scrollY: 0 },
      ),
    ).toBeCloseTo(1.2)
  })

  it('keeps a bottom-right target in frame without inventing pixels', () => {
    const resolved = resolveSuiteCutZoomOptions({ scale: 1.25, paddingPx: 24 })
    expect(
      suiteCutZoomFrameForTarget(
        resolved,
        { x: 1_180, y: 620, width: 100, height: 100 },
        { width: 1_280, height: 720, scrollX: 0, scrollY: 0 },
      ),
    ).toEqual({ scale: 1.25, centerX: 768, centerY: 432 })
  })

  it('pins a partially clipped target to the nearest valid camera edge', () => {
    const resolved = resolveSuiteCutZoomOptions({ scale: 1.25, paddingPx: 24 })
    expect(
      suiteCutZoomFrameForTarget(
        resolved,
        { x: -40, y: 100, width: 100, height: 80 },
        { width: 1_280, height: 720, scrollX: 0, scrollY: 0 },
      ),
    ).toEqual({ scale: 1.25, centerX: 512, centerY: 288 })
  })

  it('does not zoom stale geometry that is outside the viewport', () => {
    const resolved = resolveSuiteCutZoomOptions({ scale: 1.25, paddingPx: 24 })
    expect(
      suiteCutZoomFrameForTarget(
        resolved,
        { x: 20, y: 1_008, width: 120, height: 40 },
        { width: 640, height: 360, scrollX: 0, scrollY: 0 },
      ),
    ).toEqual({ scale: 1, centerX: 320, centerY: 180 })
  })

  it('backs out of zoom for a viewport-sized mobile target', () => {
    const resolved = resolveSuiteCutZoomOptions({ scale: 1.25, paddingPx: 24 })
    expect(
      suiteCutZoomFrameForTarget(
        resolved,
        { x: 0, y: 0, width: 360, height: 640 },
        { width: 360, height: 640, scrollX: 0, scrollY: 0 },
      ),
    ).toEqual({ scale: 1, centerX: 180, centerY: 320 })
  })

  it('samples enter, hold, and exit phases', () => {
    const resolved = resolveSuiteCutZoomOptions({
      scale: 1.2,
      enter: { durationMs: 100, easing: 'linear' },
      holdMs: 100,
      exit: { durationMs: 100, easing: 'linear' },
    })
    expect(suiteCutZoomScaleAt(resolved, 1.2, 0)).toBeCloseTo(1)
    expect(suiteCutZoomScaleAt(resolved, 1.2, 50)).toBeCloseTo(1.1)
    expect(suiteCutZoomScaleAt(resolved, 1.2, 150)).toBeCloseTo(1.2)
    expect(suiteCutZoomScaleAt(resolved, 1.2, 250)).toBeCloseTo(1.1)
    expect(suiteCutZoomScaleAt(resolved, 1.2, 300)).toBeUndefined()
  })

  it('joins close zooms and leaves distant zooms as separate camera runs', () => {
    const zoom = (
      id: string,
      atMs: number,
      rect: { x: number; y: number; width: number; height: number },
    ) => ({
      id,
      type: 'zoom' as const,
      atMs,
      pageId: 'page-main',
      rect,
      viewport: { width: 1_280, height: 720, scrollX: 0, scrollY: 0 },
      options: {
        scale: 1.2,
        enter: { durationMs: 100, easing: 'linear' as const },
        holdMs: 100,
        exit: { durationMs: 100, easing: 'linear' as const },
      },
    })

    const runs = resolveSuiteCutCameraRuns([
      zoom('zoom-1', 0, { x: 100, y: 100, width: 100, height: 40 }),
      zoom('zoom-2', 800, { x: 250, y: 140, width: 100, height: 40 }),
      zoom('zoom-3', 1_600, { x: 1_100, y: 620, width: 100, height: 40 }),
    ])

    expect(runs).toHaveLength(2)
    expect(runs[0]?.zooms.map((item) => item.event.id)).toEqual(['zoom-1', 'zoom-2'])
    expect(runs[1]?.zooms.map((item) => item.event.id)).toEqual(['zoom-3'])
  })
})
