import { type Locator, type Page } from '@playwright/test'

import {
  type SuiteCutCheckpointOptions,
  type SuiteCutHighlightOptions,
  type SuiteCutNarrationOptions,
  type SuiteCutZoomOptions,
} from './schemas.js'
import { type Milliseconds } from './types.js'

export type {
  SuiteCutCaptureOptions,
  SuiteCutCheckpointOptions,
  SuiteCutNarrationOptions,
} from './schemas.js'

/** Controls the recording timeline from a Playwright test. */
export interface SuiteCutFixture {
  /**
   * Makes a page the source for later narration and visual events.
   * @param page - The main page, popup, or secondary page to record.
   */
  selectPage(page: Page): void
  /**
   * Adds synthesized speech and an optional caption at the current test time.
   * @param text - The text sent to the selected narration provider.
   * @param options - Provider, voice, speed, and caption settings.
   */
  narrate(text: string, options?: SuiteCutNarrationOptions): void
  /**
   * Captures a named screenshot and adds it to the recording manifest.
   * @param label - A stable human-readable name for the captured state.
   * @param options - Screenshot capture settings.
   */
  checkpoint(label: string, options?: SuiteCutCheckpointOptions): Promise<void>
  /**
   * Extends the rendered timeline without delaying the Playwright test.
   * @param durationMs - Presentation time to insert in milliseconds.
   */
  hold(durationMs: Milliseconds): void
  /**
   * Draws attention to a locator in the rendered video.
   * @param locator - The element whose current geometry should be captured.
   * @param options - Highlight appearance and timing settings.
   */
  highlight(locator: Locator, options?: SuiteCutHighlightOptions): Promise<void>
  /**
   * Frames a locator more closely in the rendered video.
   * @param locator - The element whose current geometry should be captured.
   * @param options - Zoom scale, padding, animation, and hold settings.
   */
  zoom(locator: Locator, options?: SuiteCutZoomOptions): Promise<void>
}
