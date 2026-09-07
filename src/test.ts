import { expect as baseExpect, test as baseTest, type TestInfo } from '@playwright/test'

import { type SuiteCutArtifactSink } from './artifact-sink.js'
import { DEFAULT_SUITE_CUT_VIEWPORT } from './capture.js'
import { SUITECUT_EVENT_ATTACHMENT } from './constants.js'
import { type SuiteCutCaptureOptions, type SuiteCutFixture } from './fixtures.js'
import { createNarrationPipeline } from './narration.js'
import { type SuiteCutAudioPluginReference } from './schemas.js'
import { createRecordingSession, createSuiteCutFixture } from './suitecut.js'
import { parseCaptureOptions } from './validation.js'

interface SuiteCutFixtures {
  suitecut: SuiteCutFixture
  suitecutCapture: SuiteCutCaptureOptions
  suitecutAudioPlugins: readonly SuiteCutAudioPluginReference[]
}

function artifactSink(testInfo: TestInfo): SuiteCutArtifactSink {
  return {
    pathFor: (name) => testInfo.outputPath(name),
    attach: async (name, path, contentType) => {
      await testInfo.attach(name, { path, contentType })
    },
  }
}

export const test = baseTest.extend<SuiteCutFixtures>({
  viewport: DEFAULT_SUITE_CUT_VIEWPORT,
  suitecutCapture: [{}, { option: true }],
  suitecutAudioPlugins: [[], { option: true }],
  suitecut: async ({ page, suitecutAudioPlugins, suitecutCapture }, use, testInfo) => {
    const captureOptions = parseCaptureOptions(suitecutCapture)
    if (captureOptions.viewport !== undefined) {
      await page.setViewportSize(captureOptions.viewport)
    }
    const output = artifactSink(testInfo)
    const session = await createRecordingSession({ captureOptions, output, page })
    const narration = createNarrationPipeline(output, session, suitecutAudioPlugins)
    const suitecut = createSuiteCutFixture(session, narration, captureOptions)

    let useError: Error | undefined
    const teardownErrors: Error[] = []
    try {
      await use(suitecut)
    } catch (error) {
      useError =
        error instanceof Error
          ? error
          : new Error('The Playwright test rejected with a non-Error value')
    } finally {
      const [recordingResult, narrationResult] = await Promise.allSettled([
        session.endRecording(),
        narration.finish(),
      ])
      if (recordingResult.status === 'rejected') {
        teardownErrors.push(
          recordingResult.reason instanceof Error
            ? recordingResult.reason
            : new Error(String(recordingResult.reason)),
        )
      }
      if (narrationResult.status === 'rejected') {
        teardownErrors.push(
          narrationResult.reason instanceof Error
            ? narrationResult.reason
            : new Error(String(narrationResult.reason)),
        )
      }
      if (recordingResult.status === 'fulfilled') {
        try {
          const attachment = await session.seal()
          await testInfo.attach(SUITECUT_EVENT_ATTACHMENT, {
            body: Buffer.from(JSON.stringify(attachment)),
            contentType: 'application/json',
          })
        } catch (error) {
          teardownErrors.push(error instanceof Error ? error : new Error(String(error)))
        }
      }
    }
    const failures = useError === undefined ? teardownErrors : [useError, ...teardownErrors]
    const firstFailure = failures.at(0)
    if (failures.length === 1 && firstFailure !== undefined) throw firstFailure
    if (failures.length > 1) {
      throw new AggregateError(failures, 'SuiteCut test and recording failed')
    }
  },
})

export const expect = baseExpect
