import {
  type SuiteCutCaptureOptions,
  type SuiteCutCaptureSize,
  type SuiteCutCaptureViewport,
} from './schemas.js'

export const DEFAULT_SUITE_CUT_VIEWPORT = { width: 1600, height: 900 } as const
export const DEFAULT_SUITE_CUT_CAPTURE_FRAMES_PER_SECOND = 30
export const DEFAULT_SUITE_CUT_CHECKPOINT_DURATION_MS = 500

export interface SuiteCutCaptureLayout {
  captureSize: SuiteCutCaptureSize
  layoutScale: number
  layoutViewport: SuiteCutCaptureViewport
  surfaceViewport: SuiteCutCaptureViewport
}

/** Resolves the page composition, physical browser surface, and encoded source size. */
export function resolveCaptureLayout(
  options: SuiteCutCaptureOptions,
  fallbackViewport: SuiteCutCaptureViewport,
): SuiteCutCaptureLayout {
  const layoutViewport = options.viewport ?? fallbackViewport
  const captureSize = options.size ?? {
    width: layoutViewport.width - (layoutViewport.width % 2),
    height: layoutViewport.height - (layoutViewport.height % 2),
  }
  const needsLargerSurface =
    captureSize.width > layoutViewport.width || captureSize.height > layoutViewport.height

  if (!needsLargerSurface) {
    return {
      captureSize,
      layoutScale: 1,
      layoutViewport,
      surfaceViewport: layoutViewport,
    }
  }

  if (
    captureSize.width < layoutViewport.width ||
    captureSize.height < layoutViewport.height ||
    captureSize.width * layoutViewport.height !== captureSize.height * layoutViewport.width
  ) {
    throw new Error(
      `SuiteCut capture size ${captureSize.width}x${captureSize.height} must use the same ` +
        `aspect ratio as the ${layoutViewport.width}x${layoutViewport.height} viewport when ` +
        'the source is larger than the viewport.',
    )
  }

  return {
    captureSize,
    layoutScale: captureSize.width / layoutViewport.width,
    layoutViewport,
    surfaceViewport: captureSize,
  }
}
