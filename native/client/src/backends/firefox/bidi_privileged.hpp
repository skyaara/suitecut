// SPDX-License-Identifier: MIT
#pragma once
#include "bidi_runtime.hpp"
#include <memory>

namespace suitecut::detail {
// Gecko-native automation operations requiring Firefox's explicit
// -remote-allow-system-access permission. No profile preferences are changed.
class BidiPrivileged {
 public:
  BidiPrivileged();
  ~BidiPrivileged();
  // Internal authored expressions only: never pass page/agent supplied code.
  // Quote context IDs and all dynamic values with Json::dump before embedding.
  // Returns the JSON value of a real privileged chrome browsing context script.
  static Json evaluate(BidiRuntime&, const BidiPage&, const std::string& authored_chrome_expression, Deadline);
  Json command(BidiRuntime&, BidiPage&, const Json&, Deadline);
  void forget(const std::string& browsing_context);
  void close(BidiRuntime&, Deadline);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace suitecut::detail
