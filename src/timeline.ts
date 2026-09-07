import {
  type SuiteCutAttempt,
  type SuiteCutEvent,
  type SuiteCutPageId,
  type SuiteCutPageSelectionEvent,
} from './types.js'

export interface SuiteCutSequenceItem {
  kind: 'play' | 'hold'
  pageId: SuiteCutPageId
  executionStartMs: number
  executionEndMs: number
  durationMs: number
}

function frameAlignedMs(value: number, framesPerSecond: number): number {
  return (Math.ceil((value / 1_000) * framesPerSecond) / framesPerSecond) * 1_000
}

function pageTransitions(attempt: SuiteCutAttempt): readonly SuiteCutEvent[] {
  const selections = attempt.events.filter(
    (event): event is SuiteCutPageSelectionEvent => event.type === 'page-selected',
  )
  return selections.length > 0 ? selections : attempt.events
}

function initialPosterAtMs(
  attempt: SuiteCutAttempt,
  mainPageId: SuiteCutPageId,
  framesPerSecond: number,
): number | undefined {
  const firstMainEvent = [...attempt.events]
    .sort((left, right) => left.atMs - right.atMs)
    .find(
      (event) => event.pageId === mainPageId && event.type !== 'page-selected' && event.atMs > 0,
    )
  if (firstMainEvent === undefined) return undefined
  const aligned = frameAlignedMs(firstMainEvent.atMs, framesPerSecond)
  return aligned < attempt.durationMs ? aligned : undefined
}

/** Builds the page playback order for a recorded attempt. */
export function buildSuiteCutSequence(
  attempt: SuiteCutAttempt,
  extraTailMs: number,
  framesPerSecond: number,
): SuiteCutSequenceItem[] {
  const mainPage = attempt.pages.find((page) => page.kind === 'main') ?? attempt.pages[0]
  if (mainPage === undefined) throw new Error('The selected attempt has no recorded pages')
  const orderedTransitions = [...pageTransitions(attempt)].sort(
    (left, right) => left.atMs - right.atMs,
  )
  const boundarySet = new Set<number>([0, attempt.durationMs])
  const posterAtMs = initialPosterAtMs(attempt, mainPage.id, framesPerSecond)
  if (posterAtMs !== undefined) boundarySet.add(posterAtMs)
  let boundaryPage = mainPage.id
  for (const transition of orderedTransitions) {
    if (transition.pageId === boundaryPage) continue
    boundaryPage = transition.pageId
    if (transition.atMs <= 0 || transition.atMs >= attempt.durationMs) continue
    boundarySet.add(frameAlignedMs(transition.atMs, framesPerSecond))
  }
  const boundaries = [...boundarySet].sort((left, right) => left - right)
  const sequence: SuiteCutSequenceItem[] = []
  let selectedPage = mainPage.id
  let transitionIndex = 0
  const selectPageAt = (atMs: number): SuiteCutPageId => {
    while (true) {
      const transition = orderedTransitions[transitionIndex]
      if (transition === undefined || transition.atMs > atMs) break
      selectedPage = transition.pageId
      transitionIndex += 1
    }
    return selectedPage
  }
  for (let index = 0; index < boundaries.length - 1; index += 1) {
    const start = boundaries[index]
    const end = boundaries[index + 1]
    if (start === undefined || end === undefined || end <= start) continue
    if (start === 0 && posterAtMs !== undefined && end === posterAtMs) {
      sequence.push({
        kind: 'hold',
        pageId: mainPage.id,
        executionStartMs: posterAtMs,
        executionEndMs: posterAtMs,
        durationMs: posterAtMs,
      })
      selectPageAt(posterAtMs)
      continue
    }
    sequence.push({
      kind: 'play',
      pageId: selectPageAt(start),
      executionStartMs: start,
      executionEndMs: end,
      durationMs: end - start,
    })
  }
  if (extraTailMs > 0) {
    sequence.push({
      kind: 'hold',
      pageId: selectPageAt(attempt.durationMs),
      executionStartMs: attempt.durationMs,
      executionEndMs: attempt.durationMs,
      durationMs: extraTailMs,
    })
  }
  return sequence
}
