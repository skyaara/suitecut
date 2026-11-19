// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <limits>
#include <string>
#include "include/cef_values.h"
namespace suitecut::browser {
using Clock = std::chrono::steady_clock;
using Dict = CefRefPtr<CefDictionaryValue>;
// Network.enable buffer sizes: the largest integer the protocol accepts, so
// Chromium's own limits apply instead of a suitecut-chosen one.
inline constexpr int kDevToolsBufferBytes = std::numeric_limits<int>::max();
// A byte count or offset on the wire: an integer while it fits an int, as
// every client expects, and a double (exact to 2^53) only beyond that.
inline void SetByteCount(const Dict& dict, const char* key, size_t value) {
  if (value <= static_cast<size_t>(std::numeric_limits<int>::max())) dict->SetInt(key, static_cast<int>(value));
  else dict->SetDouble(key, static_cast<double>(value));
}
double Now();
Dict Header(const char* type);
std::string Json(Dict dict);
std::string Literal(const std::string& text);
std::string HandleNamespace();
}  // namespace suitecut::browser
