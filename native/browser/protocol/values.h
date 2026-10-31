// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <string>
#include "include/cef_values.h"
namespace suitecut::browser {
using Clock = std::chrono::steady_clock;
using Dict = CefRefPtr<CefDictionaryValue>;
inline constexpr size_t kMaxResponseBytes = 192 * 1048576;
// Chrome loads URLs up to url::kMaxURLChars. A page that navigates a frame
// to a long GET form or redirect URL is ordinary, so observed URLs share it.
inline constexpr size_t kMaxObservedURL = 2 * 1048576;
double Now();
Dict Header(const char* type);
std::string Json(Dict dict);
std::string Literal(const std::string& text);
std::string HandleNamespace();
}  // namespace suitecut::browser
