/** Compatibility entry point for scripts written against the native beta. */
export * from './native-default.js'
export type {
  SuiteCutBrowserOptions as BetaBrowserOptions,
  SuiteCutNativeRecordOptions as BetaNativeRecordOptions,
  SuiteCutPlaywrightRecordOptions as BetaPlaywrightRecordOptions,
  SuiteCutRecordingResult as BetaRecordResult,
  SuiteCutNativeRecordingCallback as BetaRecordingCallback,
} from './native-default.js'
