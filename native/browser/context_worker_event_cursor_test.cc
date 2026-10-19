// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "context_worker_event_cursor.h"
int main() {
  assert(cef_api_hash(CEF_API_VERSION, 0));
  using suitecut::NativeContextWorkerAfterSequence;
  auto parsed = CefParseJSON("{\"afterSequence\":23}", JSON_PARSER_RFC);
  auto params = parsed->GetDictionary();
  assert(params->GetType("afterSequence") == VTYPE_INT);
  assert(params->GetDouble("afterSequence") == 0); // Genuine CEF conversion boundary.
  assert(NativeContextWorkerAfterSequence(params, 24) == 23);
  assert(!NativeContextWorkerAfterSequence(params, 22));
  params->SetDouble("afterSequence", 2147483648.0);
  assert(NativeContextWorkerAfterSequence(params, 2147483649ULL) == 2147483648.0);
  params->SetDouble("afterSequence", double(suitecut::NativeContextWorkerCursors::kMaxSequence));
  assert(NativeContextWorkerAfterSequence(params, suitecut::NativeContextWorkerCursors::kMaxSequence));
  params->SetDouble("afterSequence", 9007199254740992.0);
  assert(!NativeContextWorkerAfterSequence(params, UINT64_MAX));
  for (double value : {-1.0, 1.5}) {
    params->SetDouble("afterSequence", value);
    assert(!NativeContextWorkerAfterSequence(params, 24));
  }
  params->SetBool("afterSequence", true); assert(!NativeContextWorkerAfterSequence(params, 24));
  params->SetString("afterSequence", "23"); assert(!NativeContextWorkerAfterSequence(params, 24));
  params->SetNull("afterSequence"); assert(!NativeContextWorkerAfterSequence(params, 24));
  params->Remove("afterSequence"); assert(!NativeContextWorkerAfterSequence(params, 24));
  assert(!NativeContextWorkerAfterSequence(nullptr, 24));
}
