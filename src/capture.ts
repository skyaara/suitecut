import { type SuiteCutCaptureSize, type SuiteCutCaptureViewport } from './schemas.js'

export const DEFAULT_SUITE_CUT_CHECKPOINT_DURATION_MS = 500

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
