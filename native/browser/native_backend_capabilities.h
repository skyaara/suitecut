// SPDX-License-Identifier: MIT
#pragma once
#include "include/cef_values.h"

namespace suitecut {
// These flags describe the actual production adapter and pinned stock backend,
// not the presence of a proposed vendor patch in the source distribution.
inline CefRefPtr<CefDictionaryValue> NativeBackendCapabilities() {
  auto result = CefDictionaryValue::Create();
  result->SetInt("schemaVersion", 1);
  result->SetString("backend", "cef");
  for (const char* name : {"directoryUpload", "nativeFileSelectionReset",
                          "webSocketRouting", "fullClockControl",
                          "coverageAcrossNavigation", "offscreenOopifFullPageCapture",
                          "clientCertificateImport", "concurrentCredentialCeremonies",
                          "originalWidgetInputReceipts"})
    result->SetBool(name, false);
  result->SetBool("frameSubscription", true);
  return result;
}
}  // namespace suitecut
