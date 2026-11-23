// SPDX-License-Identifier: MIT
#pragma once
#include <string>
namespace suitecut {
// Observed request/response DTOs carry the whole URL, data: URLs included;
// there is no suitecut byte cap. Kept as a pass-through for existing callers.
inline std::string NativeObservedURL(const std::string& url) { return url; }
}  // namespace suitecut
