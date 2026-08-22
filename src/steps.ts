import { type TestError, type TestStep } from '@playwright/test/reporter'

import { SuiteCutStepCategorySchema } from './schemas.js'
import { toAttemptTime } from './timing.js'
import {
  type SuiteCutAttemptClock,
  type SuiteCutError,
  type SuiteCutExecutionStep,
  type SuiteCutSourceLocation,
  type SuiteCutStepCategory,
  type SuiteCutStepId,
} from './types.js'

function normalizeCategory(category: string): SuiteCutStepCategory {
  const result = SuiteCutStepCategorySchema.safeParse(category)
  return result.success ? result.data : 'unknown'
}

function normalizeLocation(location: TestStep['location']): SuiteCutSourceLocation | undefined {
  if (location === undefined) return undefined
  return {
    file: location.file,
    line: location.line,
    column: location.column,
  }
}

function normalizeError(error: TestError): SuiteCutError {
  const normalized: SuiteCutError = {
    message: error.message ?? error.value ?? 'Playwright step failed',
  }

  if (error.stack !== undefined) normalized.stack = error.stack
  if (error.location !== undefined) {
    normalized.location = {
      file: error.location.file,
      line: error.location.line,
      column: error.location.column,
    }
  }

  return normalized
}

function stepOutcome(step: TestStep): SuiteCutExecutionStep['outcome'] {
  if (step.error !== undefined) return 'failed'
  if (step.annotations.some((annotation) => annotation.type === 'skip')) return 'skipped'
  return 'passed'
}

export function normalizeExecutionSteps(
  steps: readonly TestStep[],
  clock: SuiteCutAttemptClock,
): SuiteCutExecutionStep[] {
  const normalized: SuiteCutExecutionStep[] = []
  let nextId = 1

  const visit = (step: TestStep, parentStepId?: SuiteCutStepId): void => {
    if (!Number.isFinite(step.duration) || step.duration < 0) {
      throw new RangeError('Playwright step duration must be finite and non-negative')
    }

    const id = `step-${nextId++}`
    const output: SuiteCutExecutionStep = {
      id,
      title: step.title,
      category: normalizeCategory(step.category),
      atMs: toAttemptTime(step.startTime.getTime(), clock.originEpochMs),
      durationMs: step.duration,
      outcome: stepOutcome(step),
    }

    if (parentStepId !== undefined) output.parentStepId = parentStepId
    const location = normalizeLocation(step.location)
    if (location !== undefined) output.location = location
    if (step.error !== undefined) output.error = normalizeError(step.error)

    normalized.push(output)
    for (const child of step.steps) visit(child, id)
  }

  for (const step of steps) visit(step)
  return normalized
}
