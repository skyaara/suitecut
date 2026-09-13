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
  deviceScaleFactor: number
  layoutScale: number
  layoutViewport: SuiteCutCaptureViewport
  physicalFrameSize: SuiteCutCaptureViewport
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
  const deviceScaleFactor = options.deviceScaleFactor ?? 1

  if (options.deviceScaleFactor !== undefined) {
    return {
      captureSize,
      deviceScaleFactor,
      layoutScale: 1,
      layoutViewport,
      physicalFrameSize: {
        width: Math.round(layoutViewport.width * deviceScaleFactor),
        height: Math.round(layoutViewport.height * deviceScaleFactor),
      },
      surfaceViewport: layoutViewport,
    }
  }
  const needsLargerSurface =
    captureSize.width > layoutViewport.width || captureSize.height > layoutViewport.height

  if (!needsLargerSurface) {
    return {
      captureSize,
      deviceScaleFactor,
      layoutScale: 1,
      layoutViewport,
      physicalFrameSize: captureSize,
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
    deviceScaleFactor,
    layoutScale: captureSize.width / layoutViewport.width,
    layoutViewport,
    physicalFrameSize: captureSize,
    surfaceViewport: captureSize,
  }
}

/** Builds a high-quality scale filter and only letterboxes differing aspect ratios. */
export function createCaptureScaleFilter(
  input: SuiteCutCaptureViewport,
  output: SuiteCutCaptureSize,
): string {
  const scale =
    `scale=${output.width}:${output.height}:` + 'flags=lanczos+accurate_rnd+full_chroma_int'
  if (input.width * output.height === input.height * output.width) return `${scale},setsar=1`
  return (
    `${scale}:force_original_aspect_ratio=decrease:force_divisible_by=2,` +
    `pad=${output.width}:${output.height}:(ow-iw)/2:(oh-ih)/2,setsar=1`
  )
}
