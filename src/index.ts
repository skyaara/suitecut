import {
  type defineSuiteCut,
  type SuiteCutNativeRecordOptions,
  type SuiteCutPlaywrightRecordOptions,
} from './native-default.js'
import { type NativeRecordingContext } from './native-recording.js'

export type { LiveStreamDiagnostic } from './live-congestion.js'
export {
  DEFAULT_BACKEND,
  defineSuiteCut,
  record,
  launchBrowser,
  createBroadcast,
} from './native-default.js'
export { createNativePage } from './native-page.js'
export type { NativePage, NativeLocator } from './native-page.js'
export type { NativeBrowser, NativeVideoFrame, NativeAudioPacket } from './native-browser.js'
export type { NativeRecordingCaptureOptions, NativeRecordingContext } from './native-recording.js'
export type { NativeBroadcast, NativeBroadcastOptions, NativeStreamOptions } from './native.js'
export type {
  SuiteCutBrowserOptions,
  SuiteCutNativeRecordOptions,
  SuiteCutPlaywrightRecordOptions,
  SuiteCutRecordingResult,
  SuiteCutNativeRecordingCallback,
} from './native-default.js'
export type { SuiteCutStreamOptions, SuiteCutReconnectOptions } from './schemas.js'
export type {
  SuiteCutBrowserName,
  SuiteCutCleanup,
  SuiteCutOutputOptions,
  SuiteCutSetupContext,
} from './playwright.js'
export { decodeWordTimingArtifact } from './captions.js'
export type {
  SuiteCutAudioPlugin,
  SuiteCutAudioPluginReference,
  SuiteCutAudioSynthesisRequest,
  SuiteCutJsonValue,
} from './audio-plugin.js'
export { renderSuiteCut } from './render.js'
export type {
  SuiteCutColorRange,
  SuiteCutOutputContainer,
  SuiteCutOutputFormat,
  SuiteCutRenderConfig,
  SuiteCutRenderFailureMode,
  SuiteCutRenderOutputConfig,
  SuiteCutRenderQuality,
  SuiteCutRenderReport,
  SuiteCutRenderRequest,
} from './render.js'
export type {
  SuiteCutCaptureSize,
  SuiteCutCaptureViewport,
  SuiteCutCheckpointOptions,
  SuiteCutNarrationOptions,
  SuiteCutPointerActionOptions,
  SuiteCutScrollOptions,
  SuiteCutTypeOptions,
} from './fixtures.js'
export type {
  Milliseconds,
  SuiteCutWordTiming,
  SuiteCutWordTimingArtifact,
  SuiteCutAnimationOptions,
  SuiteCutBorderStyle,
  SuiteCutEasing,
  SuiteCutHighlightMode,
  SuiteCutHighlightOptions,
  SuiteCutNarrationProvider,
  SuiteCutNarrationVoice,
  SuiteCutScrollAlignment,
  SuiteCutScrollBehavior,
  SuiteCutVisualAnimation,
  SuiteCutZoomOptions,
} from './types.js'

export type {
  SuiteCutNativeRecordOptions as SuiteCutDefinitionOptions,
  SuiteCutRecordingResult as SuiteCutRecordResult,
  SuiteCutNativeRecordingCallback as SuiteCutRecordingCallback,
} from './native-default.js'
export type { NativeRecordingContext as SuiteCutRecordingContext } from './native-recording.js'
export type SuiteCutRecordOptions = SuiteCutNativeRecordOptions | SuiteCutPlaywrightRecordOptions
export type SuiteCutRecorder = ReturnType<typeof defineSuiteCut>

export type { NativeRecordingCaptureOptions as SuiteCutCaptureOptions } from './native-recording.js'
export type { NativeRecordingContext as SuiteCutNativeRecordingContext } from './native-recording.js'
export type SuiteCutFixture = NativeRecordingContext['suitecut']
