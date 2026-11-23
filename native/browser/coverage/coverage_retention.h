// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <string>
#include "include/cef_values.h"
namespace suitecut {
// Decodes the pinned retained-coverage protocol without consulting a live realm.
// The caller owns attestation of the actual session/agent and author/native
// script inventory. No identity is inferred from equal URL or source text.
class CoverageRetention {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using SelectScript=std::function<bool(int,const std::string&)>;
  static bool JavaScript(Dict payload,const SelectScript& select,Dict& output,std::string& error);
  static bool CSS(Dict payload,Dict& output,std::string& error);
};
} // namespace suitecut
