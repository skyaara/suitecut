import {
  DEFAULT_SUITE_CUT_ZOOM_ENTER_DURATION_MS,
  DEFAULT_SUITE_CUT_ZOOM_EXIT_DURATION_MS,
  DEFAULT_SUITE_CUT_ZOOM_HOLD_MS,
  DEFAULT_SUITE_CUT_ZOOM_PADDING_PX,
  DEFAULT_SUITE_CUT_ZOOM_SCALE,
} from './constants.js'
import {
  type SuiteCutAnimationOptions,
  type SuiteCutEasing,
  type SuiteCutZoomOptions,
} from './schemas.js'
import {
  type SuiteCutPageId,
  type SuiteCutRect,
  type SuiteCutViewport,
  type SuiteCutZoomEvent,
} from './types.js'

interface ResolvedZoomAnimation {
  type: NonNullable<SuiteCutAnimationOptions['type']>
  durationMs: number
  easing: SuiteCutEasing
}

export interface ResolvedSuiteCutZoomOptions {
  scale: number
  paddingPx: number
  enter: ResolvedZoomAnimation
  holdMs: number
  exit: ResolvedZoomAnimation
  totalDurationMs: number
}

export interface SuiteCutZoomFrame {
  scale: number
  centerX: number
  centerY: number
}

export interface ResolvedSuiteCutCameraZoom {
  event: SuiteCutZoomEvent
  resolved: ResolvedSuiteCutZoomOptions
  frame: SuiteCutZoomFrame
  startMs: number
  enterEndMs: number
  exitStartMs: number
  endMs: number
}

export interface SuiteCutCameraRun {
  pageId: SuiteCutPageId
  zooms: ResolvedSuiteCutCameraZoom[]
  startMs: number
  endMs: number
}

const CAMERA_RUN_MAX_GAP_MS = 1_000
const CAMERA_RUN_MAX_DISTANCE = 0.3

function resolveAnimation(
  animation: SuiteCutAnimationOptions | undefined,
  defaults: { durationMs: number; easing: SuiteCutEasing },
): ResolvedZoomAnimation {
  const type = animation?.type ?? 'scale'
  return {
    type,
    durationMs: type === 'none' ? 0 : (animation?.durationMs ?? defaults.durationMs),
    easing: animation?.easing ?? defaults.easing,
  }
}

/** Resolves the capture and renderer defaults for one camera zoom. */
export function resolveSuiteCutZoomOptions(
  options: SuiteCutZoomOptions,
): ResolvedSuiteCutZoomOptions {
  const enter = resolveAnimation(options.enter, {
    durationMs: DEFAULT_SUITE_CUT_ZOOM_ENTER_DURATION_MS,
    easing: 'ease-out',
  })
  const exit = resolveAnimation(options.exit, {
    durationMs: DEFAULT_SUITE_CUT_ZOOM_EXIT_DURATION_MS,
    easing: 'ease-in-out',
  })
  const holdMs = options.holdMs ?? DEFAULT_SUITE_CUT_ZOOM_HOLD_MS
  return {
    scale: options.scale ?? DEFAULT_SUITE_CUT_ZOOM_SCALE,
    paddingPx: options.paddingPx ?? DEFAULT_SUITE_CUT_ZOOM_PADDING_PX,
    enter,
    holdMs,
    exit,
    totalDurationMs: enter.durationMs + holdMs + exit.durationMs,
  }
}

function cubicCoordinate(time: number, first: number, second: number): number {
  const inverse = 1 - time
  return 3 * inverse * inverse * time * first + 3 * inverse * time * time * second + time ** 3
}

function cubicBezierProgress(
  progress: number,
  controlX1: number,
  controlY1: number,
  controlX2: number,
  controlY2: number,
): number {
  let low = 0
  let high = 1
  for (let iteration = 0; iteration < 16; iteration += 1) {
    const midpoint = (low + high) / 2
    if (cubicCoordinate(midpoint, controlX1, controlX2) < progress) low = midpoint
    else high = midpoint
  }
  return cubicCoordinate((low + high) / 2, controlY1, controlY2)
}

/** Maps the public easing names to their CSS cubic-bezier curves. */
export function suiteCutEasingProgress(easing: SuiteCutEasing, progress: number): number {
  const clamped = Math.max(0, Math.min(1, progress))
  if (easing === 'linear') return clamped
  if (easing === 'ease-in') return cubicBezierProgress(clamped, 0.42, 0, 1, 1)
  if (easing === 'ease-out') return cubicBezierProgress(clamped, 0, 0, 0.58, 1)
  return cubicBezierProgress(clamped, 0.42, 0, 0.58, 1)
}

function clamp(value: number, minimum: number, maximum: number): number {
  return Math.max(minimum, Math.min(maximum, value))
}

/** Resolves an in-bounds camera frame that keeps the visible target and padding on screen. */
export function suiteCutZoomFrameForTarget(
  resolved: ResolvedSuiteCutZoomOptions,
  rect: SuiteCutRect,
  viewport: SuiteCutViewport,
): SuiteCutZoomFrame {
  const left = clamp(rect.x, 0, viewport.width)
  const right = clamp(rect.x + rect.width, 0, viewport.width)
  const top = clamp(rect.y, 0, viewport.height)
  const bottom = clamp(rect.y + rect.height, 0, viewport.height)

  if (right <= left || bottom <= top) {
    return { scale: 1, centerX: viewport.width / 2, centerY: viewport.height / 2 }
  }

  const paddedLeft = Math.max(0, left - resolved.paddingPx)
  const paddedRight = Math.min(viewport.width, right + resolved.paddingPx)
  const paddedTop = Math.max(0, top - resolved.paddingPx)
  const paddedBottom = Math.min(viewport.height, bottom + resolved.paddingPx)
  const widthLimit = viewport.width / (paddedRight - paddedLeft)
  const heightLimit = viewport.height / (paddedBottom - paddedTop)
  const scale = Math.max(1, Math.min(resolved.scale, widthLimit, heightLimit))
  const preferredCenterX = (left + right) / 2
  const preferredCenterY = (top + bottom) / 2

  if (scale <= 1) {
    return { scale: 1, centerX: viewport.width / 2, centerY: viewport.height / 2 }
  }

  const visibleHalfWidth = viewport.width / (2 * scale)
  const visibleHalfHeight = viewport.height / (2 * scale)

  return {
    scale,
    centerX: clamp(preferredCenterX, visibleHalfWidth, viewport.width - visibleHalfWidth),
    centerY: clamp(preferredCenterY, visibleHalfHeight, viewport.height - visibleHalfHeight),
  }
}

function resolveCameraZoom(event: SuiteCutZoomEvent): ResolvedSuiteCutCameraZoom {
  const resolved = resolveSuiteCutZoomOptions(event.options)
  const startMs = event.atMs
  const enterEndMs = startMs + resolved.enter.durationMs
  const exitStartMs = enterEndMs + resolved.holdMs
  const endMs = exitStartMs + resolved.exit.durationMs
  return {
    event,
    resolved,
    frame: suiteCutZoomFrameForTarget(resolved, event.rect, event.viewport),
    startMs,
    enterEndMs,
    exitStartMs,
    endMs,
  }
}

function normalizedCameraDistance(
  left: ResolvedSuiteCutCameraZoom,
  right: ResolvedSuiteCutCameraZoom,
): number {
  const leftViewport = left.event.viewport
  const rightViewport = right.event.viewport
  const leftX = clamp(left.event.rect.x + left.event.rect.width / 2, 0, leftViewport.width)
  const leftY = clamp(left.event.rect.y + left.event.rect.height / 2, 0, leftViewport.height)
  const rightX = clamp(right.event.rect.x + right.event.rect.width / 2, 0, rightViewport.width)
  const rightY = clamp(right.event.rect.y + right.event.rect.height / 2, 0, rightViewport.height)
  const x = leftX / leftViewport.width - rightX / rightViewport.width
  const y = leftY / leftViewport.height - rightY / rightViewport.height
  return Math.hypot(x, y)
}

function cameraZoomsCanShareRun(
  previous: ResolvedSuiteCutCameraZoom,
  next: ResolvedSuiteCutCameraZoom,
): boolean {
  const gapMs = next.startMs - previous.endMs
  return (
    previous.event.pageId === next.event.pageId &&
    gapMs >= 0 &&
    gapMs <= CAMERA_RUN_MAX_GAP_MS &&
    normalizedCameraDistance(previous, next) <= CAMERA_RUN_MAX_DISTANCE
  )
}

/** Groups close zooms so the renderer can glide between them without returning to 1x. */
export function resolveSuiteCutCameraRuns(
  events: readonly SuiteCutZoomEvent[],
): SuiteCutCameraRun[] {
  const zooms = [...events]
    .sort((left, right) => left.atMs - right.atMs)
    .map(resolveCameraZoom)
    .filter((zoom) => zoom.frame.scale > 1.000_001)
  const runs: SuiteCutCameraRun[] = []

  for (const zoom of zooms) {
    const current = runs.at(-1)
    const previous = current?.zooms.at(-1)
    if (current !== undefined && previous !== undefined && cameraZoomsCanShareRun(previous, zoom)) {
      current.zooms.push(zoom)
      current.endMs = zoom.endMs
      continue
    }
    runs.push({
      pageId: zoom.event.pageId,
      zooms: [zoom],
      startMs: zoom.startMs,
      endMs: zoom.endMs,
    })
  }

  return runs
}

/** Limits zoom so the visible target and available padding remain inside the crop. */
export function suiteCutZoomScaleForTarget(
  resolved: ResolvedSuiteCutZoomOptions,
  rect: SuiteCutRect,
  viewport: SuiteCutViewport,
): number {
  return suiteCutZoomFrameForTarget(resolved, rect, viewport).scale
}

/** Returns the sampled camera scale at an attempt timestamp. */
export function suiteCutZoomScaleAt(
  resolved: ResolvedSuiteCutZoomOptions,
  targetScale: number,
  relativeMs: number,
): number | undefined {
  if (relativeMs < 0 || relativeMs >= resolved.totalDurationMs) return undefined
  if (relativeMs < resolved.enter.durationMs) {
    const progress = suiteCutEasingProgress(
      resolved.enter.easing,
      relativeMs / resolved.enter.durationMs,
    )
    return 1 + (targetScale - 1) * progress
  }
  const exitStartMs = resolved.enter.durationMs + resolved.holdMs
  if (relativeMs < exitStartMs || resolved.exit.durationMs === 0) return targetScale
  const progress = suiteCutEasingProgress(
    resolved.exit.easing,
    (relativeMs - exitStartMs) / resolved.exit.durationMs,
  )
  return targetScale + (1 - targetScale) * progress
}
