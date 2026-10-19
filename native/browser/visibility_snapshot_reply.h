// SPDX-License-Identifier: MIT
#pragma once
#include "include/cef_values.h"

// Only the dedicated native visibility mode uses this projection. Its observed
// value is the native fixed boolean; arbitrary authored QueryEvaluate values
// retain their existing transport contract.
inline bool SuiteCutVisibilitySnapshotReply(
    CefRefPtr<CefDictionaryValue> envelope,CefRefPtr<CefValue> observed) {
  if(!envelope||!observed||observed->GetType()!=VTYPE_BOOL)return false;
  auto actual=CefDictionaryValue::Create();
  actual->SetString("type","boolean");actual->SetBool("value",observed->GetBool());
  // Populate before adoption: do not mutate a detached CEF child dictionary.
  return envelope->SetDictionary("result",actual);
}
