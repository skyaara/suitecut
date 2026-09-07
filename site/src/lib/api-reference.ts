export const playwrightTestConfig = `import { defineConfig } from '@playwright/test'

export default defineConfig({
  testDir: './tests',
  outputDir: './test-results',
  reporter: [
    ['line'],
    ['suitecut/reporter', {
      outputFile: '.suitecut/latest-run.json',
      pathKind: 'manifest-relative',
    }],
  ],
  use: {
    trace: 'off',
    video: 'off',
  },
  projects: [
    { name: 'chromium', use: { browserName: 'chromium' } },
    { name: 'firefox', use: { browserName: 'firefox' } },
    { name: 'webkit', use: { browserName: 'webkit' } },
  ],
})`

export const playwrightTestExample = `import { expect, test } from 'suitecut/test'
import { audioPluginReference } from 'suitecut/audio-plugin'

test.use({
  suitecutCapture: {
    viewport: { width: 1280, height: 720 },
    size: { width: 1280, height: 720 },
    framesPerSecond: 60,
    quality: 100,
  },
  suitecutAudioPlugins: [
    audioPluginReference({
      provider: 'vits',
      module: '@suitecut/audio-vits',
      options: {
        model: './models/piper/model.onnx',
        tokens: './models/piper/tokens.txt',
      },
    }),
  ],
})

test('records an account flow', async ({ page, suitecut }) => {
  await page.goto('/account')
  await suitecut.narrate('The account page is open.', {
    provider: 'vits',
    voice: 'default',
  })

  const billing = page.getByRole('link', { name: 'Billing' })
  await suitecut.highlight(billing, { label: 'Open billing' })
  await suitecut.click(billing)
  await expect(page).toHaveURL(/billing/)
  await suitecut.checkpoint('Billing page')
})`

export const plainPlaywrightConfig = `import { defineSuiteCut } from 'suitecut'
import { audioPluginReference } from 'suitecut/audio-plugin'

export const record = defineSuiteCut({
  browserName: 'chromium',
  launch: { headless: true },
  context: { colorScheme: 'light' },
  capture: {
    viewport: { width: 1280, height: 720 },
    size: { width: 1280, height: 720 },
    framesPerSecond: 60,
    quality: 100,
  },
  audioPlugins: [
    audioPluginReference({
      provider: 'vits',
      module: '@suitecut/audio-vits',
      options: {
        model: './models/piper/model.onnx',
        tokens: './models/piper/tokens.txt',
      },
    }),
  ],
  output: {
    directory: '.suitecut/recordings',
    manifestPath: '.suitecut/latest-run.json',
    pathKind: 'manifest-relative',
  },
})

await record('account flow', async ({ page, suitecut }) => {
  await page.goto('https://example.com/account')
  await suitecut.narrate('The account page is open.', {
    provider: 'vits',
    voice: 'default',
  })
  await suitecut.checkpoint('Account page')
})`

export const callableApi = `// suitecut
record
defineSuiteCut
renderSuiteCut(request: SuiteCutRenderRequest): Promise<SuiteCutRenderReport>

// suitecut/test
test
expect

// suitecut/audio-plugin
audioPluginReference(
  reference: SuiteCutAudioPluginReference,
): SuiteCutAudioPluginReference
defineSuiteCutAudioPlugin(plugin: SuiteCutAudioPlugin): SuiteCutAudioPlugin
encodePcm16Wav(samples: Float32Array, sampleRate: number): Uint8Array

// suitecut/reporter
new SuiteCutReporter(options?: SuiteCutReporterOptions)

// suitecut/types
SUITECUT_EVENT_ATTACHMENT

// @suitecut/audio-sherpa-core
createSherpaAudioPlugin<Options extends SherpaCommonOptions>(
  definition: SherpaAudioPluginDefinition<Options>,
): SuiteCutAudioPlugin`

export const fixtureInterface = `interface SuiteCutFixture {
  selectPage(page: Page): void
  narrate(text: string, options?: SuiteCutNarrationOptions): Promise<void>
  checkpoint(label: string, options?: SuiteCutCheckpointOptions): Promise<void>
  hold(durationMs: number): Promise<void>
  highlight(locator: Locator, options?: SuiteCutHighlightOptions): Promise<void>
  zoom(locator: Locator, options?: SuiteCutZoomOptions): Promise<void>
  hover(locator: Locator, options?: SuiteCutPointerActionOptions): Promise<void>
  click(locator: Locator, options?: SuiteCutPointerActionOptions): Promise<void>
  type(locator: Locator, text: string, options?: SuiteCutTypeOptions): Promise<void>
  scrollTo(locator: Locator, options?: SuiteCutScrollOptions): Promise<void>
  scrollTop(options?: SuiteCutScrollOptions): Promise<void>
}`

export const renderExample = `import { renderSuiteCut } from 'suitecut/render'

const report = await renderSuiteCut({
  manifestPath: '.suitecut/latest-run.json',
  outputPath: '.suitecut/videos/tour.mp4',
  selection: { testId, retry: 0 },
  config: {
    narrationEnabled: true,
    resultHoldMs: 500,
    failureMode: 'strict',
    output: {
      container: 'mp4',
      width: 3840,
      height: 2160,
      framesPerSecond: 60,
      quality: 'high',
    },
  },
})`

export const audioPluginExample = `import { writeFile } from 'node:fs/promises'
import {
  defineSuiteCutAudioPlugin,
  encodePcm16Wav,
} from 'suitecut/audio-plugin'

export default defineSuiteCutAudioPlugin({
  async synthesize({ text, voice, speed, outputPath, options }) {
    const { samples, sampleRate } = await synthesizeWithYourModel({
      text,
      voice,
      speed,
      options,
    })
    await writeFile(outputPath, encodePcm16Wav(samples, sampleRate))
  },
})`

export const rootExports = `import {
  defineSuiteCut,
  record,
  renderSuiteCut,
  type Milliseconds,
  type SuiteCutAnimationOptions,
  type SuiteCutAudioPlugin,
  type SuiteCutAudioPluginReference,
  type SuiteCutAudioSynthesisRequest,
  type SuiteCutBorderStyle,
  type SuiteCutCaptureOptions,
  type SuiteCutCaptureSize,
  type SuiteCutCaptureViewport,
  type SuiteCutCheckpointOptions,
  type SuiteCutColorRange,
  type SuiteCutEasing,
  type SuiteCutFixture,
  type SuiteCutHighlightMode,
  type SuiteCutHighlightOptions,
  type SuiteCutJsonValue,
  type SuiteCutNarrationOptions,
  type SuiteCutNarrationProvider,
  type SuiteCutNarrationVoice,
  type SuiteCutOutputContainer,
  type SuiteCutOutputFormat,
  type SuiteCutPointerActionOptions,
  type SuiteCutRenderConfig,
  type SuiteCutRenderFailureMode,
  type SuiteCutRenderOutputConfig,
  type SuiteCutRenderQuality,
  type SuiteCutRenderReport,
  type SuiteCutRenderRequest,
  type SuiteCutScrollAlignment,
  type SuiteCutScrollBehavior,
  type SuiteCutScrollOptions,
  type SuiteCutTypeOptions,
  type SuiteCutVisualAnimation,
  type SuiteCutZoomOptions,
} from 'suitecut'`

export const testExports = `import { expect, test } from 'suitecut/test'`

export const playwrightExports = `import {
  defineSuiteCut,
  record,
  type Milliseconds,
  type SuiteCutAudioPluginReference,
  type SuiteCutBrowserName,
  type SuiteCutCaptureOptions,
  type SuiteCutCheckpointOptions,
  type SuiteCutCleanup,
  type SuiteCutDefinitionOptions,
  type SuiteCutFixture,
  type SuiteCutHighlightOptions,
  type SuiteCutNarrationOptions,
  type SuiteCutOutputOptions,
  type SuiteCutPointerActionOptions,
  type SuiteCutRecordOptions,
  type SuiteCutRecordResult,
  type SuiteCutRecorder,
  type SuiteCutRecordingCallback,
  type SuiteCutRecordingContext,
  type SuiteCutScrollOptions,
  type SuiteCutTypeOptions,
  type SuiteCutSetupContext,
  type SuiteCutZoomOptions,
} from 'suitecut/playwright'`

export const audioPluginExports = `import {
  audioPluginReference,
  defineSuiteCutAudioPlugin,
  encodePcm16Wav,
  type SuiteCutAudioPlugin,
  type SuiteCutAudioPluginReference,
  type SuiteCutAudioSynthesisRequest,
  type SuiteCutJsonValue,
} from 'suitecut/audio-plugin'`

export const renderExports = `import {
  renderSuiteCut,
  type SuiteCutColorRange,
  type SuiteCutEditSegment,
  type SuiteCutOutputContainer,
  type SuiteCutOutputFormat,
  type SuiteCutRenderConfig,
  type SuiteCutRenderFailureMode,
  type SuiteCutRenderOutputConfig,
  type SuiteCutRenderQuality,
  type SuiteCutRenderReport,
  type SuiteCutRenderRequest,
} from 'suitecut/render'`

export const reporterExports = `import SuiteCutReporter, {
  type SuiteCutReporterOptions,
} from 'suitecut/reporter'`

export const typeExports = `import {
  SUITECUT_EVENT_ATTACHMENT,
  type EpochMilliseconds,
  type FilePath,
  type ISODateTime,
  type Milliseconds,
  type MimeType,
  type SuiteCutActiveScreencast,
  type SuiteCutAnimationOptions,
  type SuiteCutArtifact,
  type SuiteCutArtifactId,
  type SuiteCutArtifactRole,
  type SuiteCutAttempt,
  type SuiteCutAttemptClock,
  type SuiteCutAttemptId,
  type SuiteCutAudioStream,
  type SuiteCutBorderStyle,
  type SuiteCutCapturedArtifact,
  type SuiteCutCapturedCheckpointArtifact,
  type SuiteCutCapturedNarrationArtifact,
  type SuiteCutCapturedVideo,
  type SuiteCutCheckpointEvent,
  type SuiteCutCheckpointOptions,
  type SuiteCutDiagnostic,
  type SuiteCutDiagnosticCode,
  type SuiteCutDiagnosticLevel,
  type SuiteCutEasing,
  type SuiteCutError,
  type SuiteCutEvent,
  type SuiteCutEventAttachment,
  type SuiteCutEventBase,
  type SuiteCutEventId,
  type SuiteCutExecutionStep,
  type SuiteCutFixture,
  type SuiteCutHighlightEvent,
  type SuiteCutHighlightMode,
  type SuiteCutHighlightOptions,
  type SuiteCutHoldEvent,
  type SuiteCutHoldReason,
  type SuiteCutManifest,
  type SuiteCutMedia,
  type SuiteCutMediaId,
  type SuiteCutMediaStream,
  type SuiteCutNarrationEvent,
  type SuiteCutNarrationOptions,
  type SuiteCutNarrationProvider,
  type SuiteCutNarrationVoice,
  type SuiteCutPage,
  type SuiteCutPageEventBase,
  type SuiteCutPageId,
  type SuiteCutPageKind,
  type SuiteCutPathKind,
  type SuiteCutPoint,
  type SuiteCutPointerButton,
  type SuiteCutPointerButtonEvent,
  type SuiteCutPointerMoveEvent,
  type SuiteCutPointerType,
  type SuiteCutRecordingSession,
  type SuiteCutRect,
  type SuiteCutRunStatus,
  type SuiteCutScrollAlignment,
  type SuiteCutScrollBehavior,
  type SuiteCutScrollOptions,
  type SuiteCutSize,
  type SuiteCutSourceLocation,
  type SuiteCutStepCategory,
  type SuiteCutStepId,
  type SuiteCutStepOutcome,
  type SuiteCutTest,
  type SuiteCutTestId,
  type SuiteCutTestStatus,
  type SuiteCutTypeOptions,
  type SuiteCutVideoStream,
  type SuiteCutVideoTiming,
  type SuiteCutViewport,
  type SuiteCutVisualAnimation,
  type SuiteCutZoomEvent,
  type SuiteCutZoomOptions,
} from 'suitecut/types'`

export const providerExports = `// Each provider package exports one SuiteCutAudioPlugin as default.
import vits from '@suitecut/audio-vits'
import matcha from '@suitecut/audio-matcha'
import kokoro from '@suitecut/audio-kokoro-sherpa'
import kitten from '@suitecut/audio-kitten'
import zipvoice from '@suitecut/audio-zipvoice'
import pocket from '@suitecut/audio-pocket'
import supertonic from '@suitecut/audio-supertonic'

import {
  createSherpaAudioPlugin,
  sherpaCommonOptionsShape,
  sherpaFilePathSchema,
  sherpaPositiveScaleSchema,
  type SherpaAudioPluginDefinition,
  type SherpaCommonOptions,
  type SherpaModelFamily,
} from '@suitecut/audio-sherpa-core'`

export const cliApi = `npx suitecut test [--manifest path] -- [Playwright options]

npx suitecut render \\
  --manifest .suitecut/latest-run.json \\
  --output .suitecut/videos/tour.mp4 \\
  [--test-id id] [--retry number] \\
  [--container mp4|webm|mov|mkv] \\
  [--video-codec codec] [--audio-codec codec] \\
  [--pixel-format format] [--color-range auto|full|limited] \\
  [--width pixels --height pixels] [--fps 30|60] \\
  [--quality standard|high|master] \\
  [--result-hold-ms milliseconds] \\
  [--background-color color] \\
  [--failure-mode strict|best-effort] \\
  [--ffmpeg-path path] [--no-narration]`
