// SPDX-License-Identifier: MIT
#include "runtime/tasks.h"
namespace suitecut::browser {
// Shared owner-only watchdogs carry no child payload. Keep their original
// reader/fixture provenance for public cuts, without borrowing one callback's
// private invocation when they service other operations on later turns.
suitecut::NativeWorkOrigin LocatorHandlerMonitorOrigin() noexcept {
  auto origin = suitecut::CurrentNativeWorkOrigin();
  origin.locator_handler_invocation = 0;
  return origin;
}

}  // namespace suitecut::browser
