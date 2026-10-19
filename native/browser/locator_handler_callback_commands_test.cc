// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "locator_handler_callback_commands.h"
#include <cassert>
#include <initializer_list>
#include <string_view>

int main() {
  using suitecut::NativeLocatorHandlerCallbackCommandAllowed;
  // The identical action name must not grant an unrelated command capability.
  assert(NativeLocatorHandlerCallbackCommandAllowed("network", "waitEvent"));
  assert(NativeLocatorHandlerCallbackCommandAllowed("context", "closePage"));
  assert(NativeLocatorHandlerCallbackCommandAllowed("form", "setChecked"));
  assert(NativeLocatorHandlerCallbackCommandAllowed("form", "selectOption"));
  assert(NativeLocatorHandlerCallbackCommandAllowed("handle", "disposeMany"));
  assert(NativeLocatorHandlerCallbackCommandAllowed("input", "press"));
  // Self-removal reaches native disable-then-reject policy rather than being
  // rejected at ingress before it can disable the resident registration.
  assert(NativeLocatorHandlerCallbackCommandAllowed("locatorHandlerRemove", ""));
  assert(!NativeLocatorHandlerCallbackCommandAllowed("http", "waitEvent"));
  assert(NativeLocatorHandlerCallbackCommandAllowed("context", "close"));
  assert(NativeLocatorHandlerCallbackCommandAllowed("context", "newPage"));
  assert(NativeLocatorHandlerCallbackCommandAllowed("recoverPage", ""));
  assert(!NativeLocatorHandlerCallbackCommandAllowed("recoverPage", "futureAction"));
  assert(!NativeLocatorHandlerCallbackCommandAllowed("dialog", "subscribe"));
  for (const std::string_view action : {"fetch", "registerRoute", "removeRoute", "continue", "abort", "fulfill", "fallback", "bodyChunk", "resetBody", "setOffline"})
    assert(NativeLocatorHandlerCallbackCommandAllowed("network", action));
  for (const std::string_view action : {"setInputFiles", "uploadBegin", "uploadAppend", "uploadAbort"})
    assert(NativeLocatorHandlerCallbackCommandAllowed("form", action));
  for (const std::string_view command : {"http", "artifact", "har", "recording", "streaming", "coverage", "debugger", "drag"})
    assert(!NativeLocatorHandlerCallbackCommandAllowed(command, ""));
  // Fail closed for missing/unknown actions, prefixes and embedded wire bytes.
  assert(!NativeLocatorHandlerCallbackCommandAllowed("input", ""));
  assert(!NativeLocatorHandlerCallbackCommandAllowed("locator", "futureAction"));
  assert(!NativeLocatorHandlerCallbackCommandAllowed("networkBodyExtra", "waitEvent"));
  assert(!NativeLocatorHandlerCallbackCommandAllowed(std::string_view("evaluate\0http", 13), ""));
  assert(!NativeLocatorHandlerCallbackCommandAllowed("evaluate", "fetch"));
}
