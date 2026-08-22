import { type TestStep } from '@playwright/test/reporter'
import { describe, expect, it } from 'vitest'

import { normalizeExecutionSteps } from '../src/steps.js'
import { type SuiteCutAttemptClock } from '../src/types.js'

const clock: SuiteCutAttemptClock = {
  originEpochMs: 10_000,
  originMonotonicMs: 500,
  startedAt: '1970-01-01T00:00:10.000Z',
}

interface StepOptions {
  title: string
  category: string
  startEpochMs: number
  durationMs: number
  location?: { file: string; line: number; column: number }
  error?: { message: string; stack?: string }
  steps?: TestStep[]
}

function reporterStep(options: StepOptions): TestStep {
  return {
    title: options.title,
    category: options.category,
    startTime: new Date(options.startEpochMs),
    duration: options.durationMs,
    location: options.location,
    error: options.error,
    steps: options.steps ?? [],
    annotations: [],
    attachments: [],
    titlePath: () => [options.title],
  } as TestStep
}

describe('reporter step normalization', () => {
  it('flattens nested steps and preserves parent IDs', () => {
    const child = reporterStep({
      title: 'page.getByRole.click',
      category: 'pw:api',
      startEpochMs: 10_120,
      durationMs: 35,
    })
    const parent = reporterStep({
      title: 'Create project',
      category: 'test.step',
      startEpochMs: 10_100,
      durationMs: 80,
      steps: [child],
    })

    expect(normalizeExecutionSteps([parent], clock)).toEqual([
      {
        id: 'step-1',
        title: 'Create project',
        category: 'test.step',
        atMs: 100,
        durationMs: 80,
        outcome: 'passed',
      },
      {
        id: 'step-2',
        parentStepId: 'step-1',
        title: 'page.getByRole.click',
        category: 'pw:api',
        atMs: 120,
        durationMs: 35,
        outcome: 'passed',
      },
    ])
  })

  it('maps unknown Playwright categories to unknown', () => {
    const step = reporterStep({
      title: 'custom reporter work',
      category: 'third-party',
      startEpochMs: 10_100,
      durationMs: 10,
    })
    expect(normalizeExecutionSteps([step], clock)[0]!.category).toBe('unknown')
  })

  it('preserves source location and errors', () => {
    const step = reporterStep({
      title: 'expected heading',
      category: 'expect',
      startEpochMs: 10_200,
      durationMs: 50,
      location: { file: '/workspace/app/tests/example.spec.ts', line: 12, column: 5 },
      error: { message: 'Expected heading to be visible', stack: 'Error: expected heading' },
    })
    expect(normalizeExecutionSteps([step], clock)[0]).toMatchObject({
      location: { file: '/workspace/app/tests/example.spec.ts', line: 12, column: 5 },
      error: { message: 'Expected heading to be visible', stack: 'Error: expected heading' },
      outcome: 'failed',
    })
  })

  it('keeps negative times for steps before the attempt clock', () => {
    const step = reporterStep({
      title: 'fixture setup',
      category: 'fixture',
      startEpochMs: 9_975,
      durationMs: 40,
    })
    expect(normalizeExecutionSteps([step], clock)[0]!.atMs).toBe(-25)
  })

  it('does not parse coordinates or meaning from step titles', () => {
    const step = reporterStep({
      title: 'click at x=40 y=70 on Publish',
      category: 'pw:api',
      startEpochMs: 10_100,
      durationMs: 20,
    })
    expect(normalizeExecutionSteps([step], clock)[0]).toEqual({
      id: 'step-1',
      title: 'click at x=40 y=70 on Publish',
      category: 'pw:api',
      atMs: 100,
      durationMs: 20,
      outcome: 'passed',
    })
  })
})
