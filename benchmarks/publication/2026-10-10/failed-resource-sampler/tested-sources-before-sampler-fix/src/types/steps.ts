import { type SuiteCutStepCategory } from '../schemas.js'

import { type SuiteCutError, type SuiteCutSourceLocation } from './errors.js'
import { type Milliseconds, type SuiteCutStepId } from './primitives.js'

export type { SuiteCutStepCategory } from '../schemas.js'

export type SuiteCutStepOutcome = 'passed' | 'failed' | 'skipped' | 'interrupted'

export interface SuiteCutExecutionStep {
  id: SuiteCutStepId
  parentStepId?: SuiteCutStepId
  title: string
  category: SuiteCutStepCategory
  atMs: Milliseconds
  durationMs: Milliseconds
  outcome: SuiteCutStepOutcome
  location?: SuiteCutSourceLocation
  error?: SuiteCutError
}
