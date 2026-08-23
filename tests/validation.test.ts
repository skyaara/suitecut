import { describe, expect, it } from 'vitest'
import * as z from 'zod'

import SuiteCutReporter from '../src/reporter.js'
import { type UntrustedInput } from '../src/untrusted.js'
import {
  parseCapturedGeometry,
  parseCapturedViewport,
  parseCaptureOptions,
  parseCheckpointInput,
  parseHighlightOptions,
  parseHoldInput,
  parseNarrationInput,
  parsePointerActionOptions,
  parseScrollOptions,
  parseZoomOptions,
} from '../src/validation.js'

function expectZodIssue(
  operation: () => UntrustedInput,
  path: PropertyKey[],
  code?: z.core.$ZodIssue['code'],
): void {
  let caught: UntrustedInput

  try {
    operation()
  } catch (error) {
    caught = error as UntrustedInput
  }

  expect(caught).toBeInstanceOf(z.ZodError)
  if (!(caught instanceof z.ZodError)) throw new Error('Expected a ZodError')
  expect(caught.issues).toEqual(
    expect.arrayContaining([
      expect.objectContaining({
        path,
        ...(code === undefined ? {} : { code }),
      }),
    ]),
  )
}

describe('SuiteCut fixture input validation', () => {
  it('parses a complete capture profile', () => {
    const options = {
      viewport: { width: 1600, height: 900 },
      size: { width: 3840, height: 2160 },
      framesPerSecond: 60 as const,
      quality: 100,
      narrationTailMs: 250,
    }

    expect(parseCaptureOptions(options)).toEqual(options)
  })

  it.each<[UntrustedInput, PropertyKey[], z.core.$ZodIssue['code']]>([
    [{ size: { width: 0, height: 2160 } }, ['size', 'width'], 'too_small'],
    [{ size: { width: 3839, height: 2160 } }, ['size', 'width'], 'not_multiple_of'],
    [{ viewport: { width: 0, height: 900 } }, ['viewport', 'width'], 'too_small'],
    [{ framesPerSecond: 24 }, ['framesPerSecond'], 'invalid_union'],
    [{ quality: 101 }, ['quality'], 'too_big'],
    [{ mode: 'legacy' }, [], 'unrecognized_keys'],
    [{ narrationTailMs: -1 }, ['narrationTailMs'], 'too_small'],
    [{ bitrate: 10_000_000 }, [], 'unrecognized_keys'],
  ])('rejects invalid capture options', (options, path, code) => {
    expectZodIssue(() => parseCaptureOptions(options as never), path, code)
  })

  it('parses valid narration and checkpoint inputs', () => {
    expect(
      parseNarrationInput('Explain the result.', {
        provider: 'kokoro',
        voice: 'af_heart',
        speed: 1.1,
        caption: '',
      }),
    ).toEqual({
      text: 'Explain the result.',
      options: { provider: 'kokoro', voice: 'af_heart', speed: 1.1, caption: '' },
    })
    expect(parseCheckpointInput('Result loaded', { fullPage: true })).toEqual({
      label: 'Result loaded',
      options: { fullPage: true },
    })
  })

  it.each<[UntrustedInput, UntrustedInput, PropertyKey[], z.core.$ZodIssue['code']?]>([
    ['', undefined, ['text'], 'custom'],
    ['Narration', { voice: '' }, ['options', 'voice'], 'custom'],
    ['Narration', { provider: 'native' }, ['options', 'provider'], 'invalid_value'],
    ['Narration', { speed: 0.25 }, ['options', 'speed'], 'too_small'],
    ['Narration', { caption: 10 }, ['options', 'caption'], 'invalid_type'],
    ['Narration', { speech: 'default' }, ['options'], 'unrecognized_keys'],
  ])('rejects invalid narration input', (text, options, path, code) => {
    expectZodIssue(() => parseNarrationInput(text as never, options as never), path, code)
  })

  it.each<[UntrustedInput, UntrustedInput, PropertyKey[], z.core.$ZodIssue['code']?]>([
    ['', undefined, ['label'], 'custom'],
    ['Loaded', { fullPage: 'yes' }, ['options', 'fullPage'], 'invalid_type'],
    ['Loaded', { durationMs: 500 }, ['options'], 'unrecognized_keys'],
  ])('rejects invalid checkpoint input', (label, options, path, code) => {
    expectZodIssue(() => parseCheckpointInput(label as never, options as never), path, code)
  })

  it.each<[UntrustedInput, z.core.$ZodIssue['code']]>([
    [0, 'too_small'],
    [-1, 'too_small'],
    [Number.NaN, 'invalid_type'],
    [Number.POSITIVE_INFINITY, 'invalid_type'],
    ['500', 'invalid_type'],
  ])('rejects invalid hold duration %s', (durationMs, code) => {
    expectZodIssue(() => parseHoldInput(durationMs as never), ['durationMs'], code)
  })

  it('parses complete highlight options', () => {
    const options = {
      durationMs: 1_400,
      mode: 'spotlight' as const,
      paddingPx: 12,
      borderWidthPx: 4,
      borderStyle: 'solid' as const,
      borderColor: '#22C55E',
      borderRadiusPx: 12,
      fillColor: '#22C55E',
      fillOpacity: 0.1,
      backdropColor: '#000000',
      backdropOpacity: 0.35,
      label: 'Publish',
      enter: { type: 'fade-scale' as const, durationMs: 180, easing: 'ease-out' as const },
      exit: { type: 'fade' as const, durationMs: 140, easing: 'ease-in' as const },
    }

    expect(parseHighlightOptions(options)).toEqual(options)
  })

  it.each<[UntrustedInput, PropertyKey[], z.core.$ZodIssue['code']?]>([
    [{ durationMs: 0 }, ['durationMs'], 'too_small'],
    [{ mode: 'glow' }, ['mode'], 'invalid_value'],
    [{ paddingPx: -1 }, ['paddingPx'], 'too_small'],
    [{ fillOpacity: 1.1 }, ['fillOpacity'], 'too_big'],
    [{ borderColor: '' }, ['borderColor'], 'custom'],
    [{ enter: { durationMs: -1 } }, ['enter', 'durationMs'], 'too_small'],
    [{ enter: { easing: 'bounce' } }, ['enter', 'easing'], 'invalid_value'],
    [{ colour: 'red' }, [], 'unrecognized_keys'],
  ])('rejects invalid highlight options', (options, path, code) => {
    expectZodIssue(() => parseHighlightOptions(options as never), path, code)
  })

  it('parses complete zoom options', () => {
    const options = {
      scale: 1.18,
      paddingPx: 32,
      holdMs: 1_200,
      enter: { type: 'scale' as const, durationMs: 320, easing: 'ease-out' as const },
      exit: { type: 'scale' as const, durationMs: 260, easing: 'ease-in-out' as const },
    }

    expect(parseZoomOptions(options)).toEqual(options)
  })

  it.each<[UntrustedInput, PropertyKey[], z.core.$ZodIssue['code']?]>([
    [{ scale: 0.99 }, ['scale'], 'too_small'],
    [{ paddingPx: -1 }, ['paddingPx'], 'too_small'],
    [{ holdMs: -1 }, ['holdMs'], 'too_small'],
    [{ exit: { type: 'slide' } }, ['exit', 'type'], 'invalid_value'],
    [{ durationMs: 100 }, [], 'unrecognized_keys'],
  ])('rejects invalid zoom options', (options, path, code) => {
    expectZodIssue(() => parseZoomOptions(options as never), path, code)
  })

  it('parses pointer action timing', () => {
    const options = {
      moveDurationMs: 420,
      settleMs: 80,
      waitForAnimations: true,
      animationTimeoutMs: 2_500,
    }
    expect(parsePointerActionOptions(options)).toEqual(options)
  })

  it.each<[UntrustedInput, PropertyKey[], z.core.$ZodIssue['code']]>([
    [{ moveDurationMs: -1 }, ['moveDurationMs'], 'too_small'],
    [{ animationTimeoutMs: Number.NaN }, ['animationTimeoutMs'], 'invalid_type'],
    [{ cursorDurationMs: 100 }, [], 'unrecognized_keys'],
  ])('rejects invalid pointer action options', (options, path, code) => {
    expectZodIssue(() => parsePointerActionOptions(options as never), path, code)
  })

  it('parses native scroll options', () => {
    const options = {
      behavior: 'smooth' as const,
      block: 'center' as const,
      inline: 'nearest' as const,
      settleMs: 120,
      timeoutMs: 2_000,
    }
    expect(parseScrollOptions(options)).toEqual(options)
  })

  it.each<[UntrustedInput, PropertyKey[], z.core.$ZodIssue['code']]>([
    [{ behavior: 'animated' }, ['behavior'], 'invalid_value'],
    [{ block: 'middle' }, ['block'], 'invalid_value'],
    [{ settleMs: -1 }, ['settleMs'], 'too_small'],
    [{ timeoutMs: 0 }, ['timeoutMs'], 'too_small'],
    [{ durationMs: 500 }, [], 'unrecognized_keys'],
  ])('rejects invalid native scroll options', (options, path, code) => {
    expectZodIssue(() => parseScrollOptions(options as never), path, code)
  })

  it('parses valid captured geometry', () => {
    const rect = { x: -10, y: 20, width: 0, height: 40 }
    const viewport = {
      width: 1280,
      height: 720,
      deviceScaleFactor: 2,
      scrollX: 0,
      scrollY: 100,
    }

    expect(parseCapturedGeometry(rect, viewport)).toEqual({ rect, viewport })
    expect(parseCapturedViewport(viewport)).toEqual(viewport)
  })

  it.each<[UntrustedInput, UntrustedInput, PropertyKey[], z.core.$ZodIssue['code']]>([
    [
      { x: Number.NaN, y: 0, width: 10, height: 10 },
      { width: 1280, height: 720, deviceScaleFactor: 1, scrollX: 0, scrollY: 0 },
      ['rect', 'x'],
      'invalid_type',
    ],
    [
      { x: 0, y: 0, width: -1, height: 10 },
      { width: 1280, height: 720, deviceScaleFactor: 1, scrollX: 0, scrollY: 0 },
      ['rect', 'width'],
      'too_small',
    ],
    [
      { x: 0, y: 0, width: 10, height: 10 },
      { width: 0, height: 720, deviceScaleFactor: 1, scrollX: 0, scrollY: 0 },
      ['viewport', 'width'],
      'too_small',
    ],
    [
      { x: 0, y: 0, width: 10, height: 10 },
      { width: 1280, height: 720, deviceScaleFactor: 0, scrollX: 0, scrollY: 0 },
      ['viewport', 'deviceScaleFactor'],
      'too_small',
    ],
  ])('rejects invalid captured geometry', (rect, viewport, path, code) => {
    expectZodIssue(() => parseCapturedGeometry(rect as never, viewport as never), path, code)
  })
})

describe('SuiteCut reporter option validation', () => {
  it('parses valid reporter options', () => {
    const reporter = new SuiteCutReporter({
      outputFile: '.suitecut/latest-run.json',
      pathKind: 'manifest-relative',
      includeStepCategories: ['pw:api', 'expect'],
    })

    expect(reporter.options).toEqual({
      outputFile: '.suitecut/latest-run.json',
      pathKind: 'manifest-relative',
      includeStepCategories: ['pw:api', 'expect'],
    })
  })

  it.each<[UntrustedInput, PropertyKey[], z.core.$ZodIssue['code']]>([
    [{ outputFile: '' }, ['outputFile'], 'custom'],
    [{ pathKind: 'relative' }, ['pathKind'], 'invalid_value'],
    [{ includeStepCategories: ['network'] }, ['includeStepCategories', 0], 'invalid_value'],
    [{ outputDirectory: '.suitecut' }, [], 'unrecognized_keys'],
  ])('rejects invalid reporter options', (options, path, code) => {
    expectZodIssue(() => new SuiteCutReporter(options as never), path, code)
  })
})
