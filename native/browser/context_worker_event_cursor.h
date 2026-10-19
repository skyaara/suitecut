// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <optional>
#include "dictionary_number.h"
#include "context_worker_cursor.h"
namespace suitecut {
inline std::optional<double> NativeContextWorkerAfterSequence(
    CefRefPtr<CefDictionaryValue> params, uint64_t current) {
  const auto value = DictionaryNumber(params, "afterSequence");
  if (!std::isfinite(value) || value < 0 || std::floor(value) != value ||
      value > double(NativeContextWorkerCursors::kMaxSequence) ||
      value > double(current)) return {};
  return value;
}
}  // namespace suitecut
