import {
  type SuiteCutCaptureOptions,
  type SuiteCutCaptureSize,
  type SuiteCutCaptureViewport,
} from './schemas.js'

export const DEFAULT_SUITE_CUT_VIEWPORT = { width: 1600, height: 900 } as const
export const DEFAULT_SUITE_CUT_CAPTURE_FRAMES_PER_SECOND = 60

export function resolveCaptureSize(
  options: SuiteCutCaptureOptions,
  viewport: SuiteCutCaptureViewport,
): SuiteCutCaptureSize {
  const size = options.size ?? {
    width: viewport.width - (viewport.width % 2),
    height: viewport.height - (viewport.height % 2),
  }
  if (size.width > viewport.width || size.height > viewport.height) {
    throw new Error(
      `SuiteCut capture size ${size.width}x${size.height} exceeds the ` +
        `${viewport.width}x${viewport.height} Playwright viewport. ` +
        'Increase the viewport for a larger source, or request 4K from the renderer.',
    )
  }
  return size
}
