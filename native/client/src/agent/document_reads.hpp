// SPDX-License-Identifier: MIT
#pragma once
// Scripts that land while a navigation replaces the page's document. Chromium
// either refuses them before they start (the old document has no context
// left) or loses them while they run (the context is destroyed under them).
#include "suitecut/browser.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <thread>

namespace suitecut::detail {

inline bool error_mentions(const std::exception& error, std::initializer_list<const char*> reasons) {
  const std::string what = error.what();
  return std::any_of(reasons.begin(), reasons.end(),
                     [&](const char* reason) { return what.find(reason) != std::string::npos; });
}

// The script was refused before it started: there was no context to run in.
inline bool script_never_ran(const std::exception& error) {
  return error_mentions(error, {"Cannot find default execution context", "Cannot find context with specified id"});
}

// The document went away, before or during the script: the CEF engine's
// annotation and Chromium's own texts.
inline bool document_replaced(const std::exception& error) {
  return error_mentions(error, {"execution context unavailable after navigation", "Cannot find context",
                                "Execution context was destroyed", "Inspected target navigated"});
}

// Evaluates `expression` in the page's current document, running it again in
// the new document while `retry(error)` says that is safe, all within the
// page's one timeout.
template <class Retry> Json evaluate_in_current_document(const Page& page, const std::string& expression, Retry retry) {
  using Clock = std::chrono::steady_clock;
  constexpr auto pause = std::chrono::milliseconds{50};
  const auto deadline = Clock::now() + page.timeout();
  for (;;) {
    const auto left = std::chrono::ceil<Timeout>(deadline - Clock::now());
    try {
      return page.with_timeout(std::max(left, Timeout{1})).evaluate(expression);
    } catch (const std::exception& error) {
      if (!retry(error) || Clock::now() + pause >= deadline) throw;
    }
    std::this_thread::sleep_for(pause);
  }
}

// A side-effect-free read (URL, title, text) can always run again.
inline Json read_current_document(const Page& page, const std::string& expression) {
  return evaluate_in_current_document(page, expression, document_replaced);
}

// Arbitrary script runs again only if it never started. If its document was
// replaced while it ran, it may have had effects, so the error says so.
inline Json evaluate_script(const Page& page, const std::string& expression) {
  try {
    return evaluate_in_current_document(page, expression, script_never_ran);
  } catch (const std::exception& error) {
    if (script_never_ran(error) || !document_replaced(error)) throw;
    throw std::runtime_error(std::string(error.what()) +
                             "; the page navigated while the script ran, so it may have partly run and was not run again");
  }
}

}  // namespace suitecut::detail
