// SPDX-License-Identifier: MIT
#pragma once
#include <string_view>

namespace suitecut {
// Capability policy only: ingress must additionally authenticate the native
// invocation/operation token and retain its actual physical descendants. Retain
// actual async callback/job receipts before admitting their finite operations.
inline constexpr bool NativeLocatorHandlerCallbackCommandAllowed(
    std::string_view command, std::string_view action) noexcept {
  if (command == "evaluate" || command == "queryEvaluate" ||
      command == "normalizeLocator" || command == "waitEvent" ||
      command == "waitEventPredicate" || command == "continueEventPredicate" ||
      command == "urlPredicate" || command == "continueURLPredicate" ||
      command == "waitNavigation" || command == "waitFrameNavigation" ||
      command == "waitLoadState" || command == "waitTimeout" ||
      command == "continueAction" || command == "locatorHandlerRegister" || command == "locatorHandlerRemove" ||
      command == "cancel" || command == "navigate" || command == "navigateFrame" ||
      command == "reload" || command == "recoverPage" || command == "historyBack" || command == "historyForward" ||
      command == "responseBody" || command == "gc" || command == "viewport" ||
      command == "devtools" || command == "snapshotReferences" ||
      command == "suggestLocator" || command == "bringToFront" ||
      command == "diagnostics" || command == "claimConsoleArguments" ||
      command == "acknowledgeConsole") return action.empty();
  if (command == "locator") {
    return action == "click" || action == "tap" || action == "hover" ||
        action == "scrollIntoViewIfNeeded" || action == "selectText" ||
        action == "fill" || action == "clear" || action == "focus" ||
        action == "blur" || action == "dispatchEvent" || action == "inputValue" ||
        action == "boundingBox" || action == "waitForFunction" ||
        action == "ariaSnapshotJSON" || action == "press" || action == "type" ||
        action == "sendKeys";
  }
  if (command == "input") {
    return action == "move" || action == "down" || action == "up" ||
        action == "click" || action == "tap" || action == "wheel" ||
        action == "press" || action == "type" || action == "insertText";
  }
  if (command == "handle") {
    return action == "create" || action == "property" || action == "evaluate" ||
        action == "dispose" || action == "disposeMany" || action == "focus" ||
        action == "boundingBox" || action == "contentFrame" ||
        action == "ownerFrame" || action == "frameElement";
  }
  if (command == "form") return action == "setChecked" || action == "selectOption" ||
      action == "dragTo" || action == "setInputFiles" || action == "uploadBegin" ||
      action == "uploadAppend" || action == "uploadAbort" ||
      action == "fileChooserSubscribe" || action == "fileChooserElement";
  // Browser network reads and route mutations use the actual native reply/job
  // scopes; no SDK networking or guessed response completion is introduced.
  if (command == "network") {
    return action == "waitEvent" || action == "response" || action == "finished" ||
        action == "timing" || action == "requestHeaders" || action == "responseHeaders" ||
        action == "fetch" || action == "registerRoute" || action == "removeRoute" ||
        action == "continue" || action == "abort" || action == "fulfill" || action == "fallback" ||
        action == "bodyChunk" || action == "resetBody" || action == "setOffline" ||
        action == "matchRoute" || action == "setExtraHTTPHeaders" || action == "setContextExtraHTTPHeaders";
  }
  if (command == "console") return action == "list" || action == "errors" || action == "clear";
  if (command == "selectors") return action == "register" || action == "diagnostics";
  if (command == "worker") return action == "list" || action == "evaluate" ||
      action == "create" || action == "property" || action == "dispose" || action == "waitClose";
  if (command == "routeCallback") return action == "start" || action == "finish";
  if (command == "bindings") return action == "register" || action == "remove" ||
      action == "resolve" || action == "argument" || action == "stage";
  if (command == "highlight") return action == "show" || action == "hide" || action == "claim";
  if (command == "pickLocator") return action.empty();
  if (command == "clock") return action == "status" || action == "installPaused" ||
      action == "pause" || action == "advance" || action == "runFor";
  if (command == "websocket") return action == "waitEvent" || action == "waitPredicate" ||
      action == "routeRegister" || action == "routeUnregister" || action == "routeUnregisterAll" ||
      action == "routePolicyCommand" || action == "routePayloadBegin" || action == "routePayloadAppend" ||
      action == "routePayloadCommit" || action == "routePayloadAbort";
  if (command == "dialog") return action == "answer";
  if (command == "networkBody") return action.empty();
  if (command == "artifact") return action == "screenshot" || action == "pdf" ||
      action == "emulateMedia" || action == "emulateIdleState" ||
      action == "capturePrepare" || action == "captureCommit";
  if (command == "coverage") return action == "startJS" || action == "stopJS" ||
      action == "startCSS" || action == "stopCSS";
  if (command == "debugger") return action == "open" || action == "pause" ||
      action == "resume" || action == "stepOver" || action == "stepInto" ||
      action == "stepOut" || action == "waitForPause" || action == "scripts" ||
      action == "scriptSource" || action == "evaluateOnCallFrame" ||
      action == "setBreakpoint" || action == "removeBreakpoint" || action == "close";
  if (command == "recording" || command == "streaming") return action == "start" ||
      action == "stop" || action == "cancel" || action == "pause" || action == "resume" ||
      action == "status" || (command == "streaming" && action == "select");
  if (command == "har") return action == "record" || action == "replay";
  if (command == "http") return action == "create" || action == "fetch" ||
      action == "fetchMultipart" || action == "prepareMultipart" ||
      action == "appendPreparedMultipart" || action == "commitMultipart" ||
      action == "upload" || action == "resolveLookup" || action == "body" ||
      action == "disposeResponse" || action == "dispose" || action == "cookies" ||
      action == "addCookies" || action == "clearCookies" || action == "initializeStorage" ||
      action == "contextStorage" || action == "storageState" || action == "tracing";
  if (command == "context") return action == "closePage" || action == "close" ||
      action == "list" || action == "pages" || action == "newPage" || action == "create" ||
      action == "newOwnedPage" || action == "waitEvent" || action == "cookies" ||
      action == "addCookies" || action == "clearCookies" || action == "grantPermissions" ||
      action == "clearPermissions" || action == "storageState" || action == "setStorageState" ||
      action == "storageUploadBegin" || action == "storageUploadAppend" ||
      action == "storageUploadDiscard" || action == "addInitScript" ||
      action == "exposeBinding" || action == "removeBinding" ||
      action == "credentialsCreate" || action == "credentialsGet" ||
      action == "credentialsDelete" || action == "credentialsInstall" ||
      action == "setExtraHTTPHeaders" || action == "setOffline" || action == "setGeolocation" ||
      action == "video" || action == "download" || action == "har" || action == "trace" ||
      action == "waitConsole" || action == "waitNetwork" || action == "routes" || action == "route" || action == "websocket" ||
      action == "setHTTPCredentials" ||
      action == "worker" || action == "workerNetwork" || action == "workerNetworkBody";
  return false;
}
}  // namespace suitecut
