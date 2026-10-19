// SPDX-License-Identifier: MIT
/**
 * Native browser-control SDK entry point. Test runners, assertions, fixtures,
 * and SuiteCut narration/render orchestration have separate entry points.
 * Shared transport/lifecycle helpers remain part of the browser runtime.
 */
export { NativeTracing } from './native-tracing.js'

export { NativeDebuggerSession } from './native-debugger.js'

export type {
  NativeDebuggerOptions,
  NativeDebuggerPause,
  NativeDebuggerCallFrame,
  NativeDebuggerLocation,
  NativeDebuggerBreakpoint,
  NativeDebuggerPrimitive,
  NativeDebuggerEvaluationException,
  NativeDebuggerEvaluation,
  NativeDebuggerScript,
  NativeDebuggerScriptCursor,
  NativeDebuggerScriptPage,
} from './native-debugger.js'

export type { NativeTraceDisposable } from './native-tracing.js'

export type { NativeHighlightDisposable, NativeHighlightOptions } from './native-highlight.js'

export { NativeBrowserController } from './native-browser-controller.js'
export { launchBrowser, type LaunchBrowserOptions } from './browser-launch.js'
export {
  connectBrowser,
  launchProtocolBrowser,
  ProtocolBrowser,
  ProtocolBrowserContext,
  ProtocolPage,
  ProtocolFrame,
  ProtocolFrameLocator,
  ProtocolDownload,
  ProtocolLocator,
  BrowserProtocolError,
} from './remote-browser.js'
export type {
  BrowserProtocol,
  BrowserProtocolCapabilities,
  BrowserCookie,
  BrowserViewport,
  BrowserScreenshotOptions,
  ConnectBrowserOptions,
  LaunchProtocolBrowserOptions,
  BrowserNavigationOptions,
  BrowserSelectOption,
  ProtocolDialog,
  BrowserTextOptions,
  BrowserRoleOptions,
} from './remote-browser.js'

export { createBrowserViewer } from './native-viewer.js'
export type {
  NativeBrowserViewer,
  NativeBrowserViewerOptions,
  NativeBrowserViewerState,
  NativeViewerResolutionMode,
} from './native-viewer.js'

export { launchNativeBrowser } from './native-browser.js'
export type { NativeBrowserCapabilities } from './native-browser-capabilities.js'

export type { NativeIdleState } from './native-idle.js'

export {
  NativeHTTPTransportError,
  NativeTimeoutError,
  NativeTimeoutError as TimeoutError,
} from './native-timeouts.js'

export type {
  NativeScreenshotOptions,
  NativePDFOptions,
  NativeMediaOptions,
} from './native-artifacts.js'

export type {
  NativeFormOptions,
  NativePageFormOptions,
  NativeSelectValues,
  NativeInputFiles,
} from './native-form.js'

export type { NativeRoleOptions } from './native-query.js'

export type { NativeBeforeStableHook } from './native-action-hooks.js'

export type { NativeBeforeScreenshotHook } from './native-artifacts.js'

export { setNativeTestIdAttribute } from './native-query.js'

export { NativeBrowserContexts, NativeBrowserContext } from './native-context.js'

export type { NativeCookie, NativeCookieInput, NativeContextTransport } from './native-context.js'

export type { NativeBrowserContextOptions } from './native-context.js'

export type { NativeStorageState, NativeStorageStateInput } from './native-storage-state.js'

export { NativeRequest, NativeRoute, type NativeRouteFetchOptions } from './native-network.js'

export type { NativeResponseServerAddr, NativeResponseSecurityDetails } from './native-network.js'

export { NativeVideo } from './native-video.js'

export type { NativeRecordVideoOptions } from './native-video.js'

export { NativeDownload } from './native-download.js'

export type { NativeDownloadInfo } from './native-download.js'

export { NativeAPIRequestContext, NativeAPIResponse } from './native-http.js'

export type {
  NativeAPIRequestContextOptions,
  NativeAPIRequestOptions,
  NativeMultipartFile,
} from './native-http.js'

export type { NativeFrameInfo, NativeAutomationEvent } from './native-protocol.js'

export type {
  NativeAudioPacket,
  NativeBrowser,
  NativeBrowserOptions,
  NativeBrowserState,
  NativePixelFormat,
  NativeVideoFrame,
} from './native-browser.js'

export {
  createNativePage,
  NativePage,
  NativeLocator,
  NativeJSHandle,
  NativeElementHandle,
  NativeFrame,
  NativeFrameLocator,
  NativeConsoleMessage,
  NativeResponse,
} from './native-page.js'

export type { NativeNavigationOptions, NativeLoadState } from './native-page.js'

export { NativeMouse, NativeKeyboard, NativeTouchscreen } from './native-input.js'
export type { NativeMouseBackend } from './native-input.js'

export type {
  NativeClickOptions,
  NativeEditableOptions,
  NativePageEditableOptions,
  NativeHoverOptions,
  NativeBlurOptions,
  NativeDispatchEventOptions,
  NativePageDispatchEventOptions,
  NativeDOMReadOptions,
  NativePredicateArgument,
  NativePageDOMReadOptions,
  NativeInputValueOptions,
  NativeKeyboardTargetOptions,
  NativeScrollOptions,
  NativeSelectTextOptions,
  NativeTapOptions,
} from './native-page.js'

export { NativeSelectors } from './native-selectors.js'

export type { NativeSelectorEngine } from './native-selectors.js'

export { NativeDialog } from './native-dialog.js'

export type { NativeLocatorScreenshotOptions, NativeScreenshotMask } from './native-artifacts.js'

export {
  NativeBindings,
  type NativeBindingSource,
  type NativeExposedBinding,
  type NativeBindingEvent,
} from './native-bindings.js'

export { NativeFileChooser } from './native-filechooser.js'

export type { NativeDragOptions } from './native-drag.js'

export { NativeWorker, NativeWorkerHandle } from './native-worker.js'

export type { NativeWorkerInfo, NativeWorkerEvent } from './native-worker-protocol.js'

export {
  NativeCredentials,
  type NativeCredentialOptions,
  type NativeVirtualCredential,
} from './native-credentials.js'

export {
  NativeWebSocket,
  NativeWebSockets,
  type NativeWebSocketFrame,
  type NativeWebSocketWaitOptions,
  type NativeWebSocketEvent,
} from './native-websocket.js'

export { NativeVirtualTime } from './native-virtual-time.js'

export type {
  NativeVirtualTimeInfo,
  NativeVirtualTimeOptions,
  NativeVirtualTimeSource,
} from './native-virtual-time.js'

export type { NativeRecordHAROptions, NativeRouteFromHAROptions } from './native-har.js'

export { NativeCoverage } from './native-coverage.js'

export type {
  NativeJSCoverageOptions,
  NativeCSSCoverageOptions,
  NativeJSCoverageEntry,
  NativeCSSCoverageEntry,
} from './native-coverage.js'

export type { NativeHTTPLookup, NativeHTTPLookupAddress } from './native-http-lookup.js'

export type { NativeCookieFilter } from './native-cookie-filter.js'

export { type NativeLocatorNormalization } from './native-locator-normalization.js'

export type {
  NativeLocatorPickPoint,
  NativeLocatorSuggestionOptions,
  NativeLocatorSuggestionRecipe,
  NativeLocatorSuggestionProvenance,
  NativeLocatorSuggestions,
} from './native-locator-suggestion.js'

export type { NativeAriaSnapshotJSONOptions, NativeAriaJSONNode } from './native-aria.js'

export {
  type NativeLocatorHandler,
  type NativeLocatorHandlerOptions,
} from './native-locator-handlers.js'

export type { NativeBindingCallback } from './native-bindings.js'
export type { NativeContextEvent } from './native-context.js'
export type { NativeEventPredicate, NativeEventPredicateOptions } from './native-event-predicate.js'
export type { NativeInitScript } from './native-init-script.js'
export type { NativeFilePayload, NativeSelectOption } from './native-form.js'
export type { NativeLocatorFilterOptions } from './native-page.js'
export type {
  NativeNetworkEventName,
  NativeNetworkURLMatcher,
  NativeRequestTiming,
  NativeRouteContinueOptions,
  NativeRouteFulfillOptions,
  NativeRouteHandler,
} from './native-network.js'
export type { NativeAriaSnapshotOptions } from './native-aria.js'

export {
  NativeWebSocketRouting,
  NativeRoutedWebSocket,
  NativeWebSocketRouteMessage,
} from './native-websocket-routing.js'
export type {
  NativeWebSocketRoutePayload,
  NativeWebSocketRoutedEvent,
  NativeWebSocketRouteWaitOptions,
} from './native-websocket-routing.js'

export type {
  NativeWebSocketRouteHandler,
  NativeWebSocketRouteURLMatcher,
  NativeWebSocketRouteRemovalOptions,
} from './native-websocket-routes.js'
export type {
  NativeWebSocketMessageHandler,
  NativeWebSocketCloseHandler,
} from './native-websocket-routing.js'
