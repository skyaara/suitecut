// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <string>
namespace suitecut {
// Inline images and fonts are genuine requests whose data: URLs can be many
// megabytes. Observed request/response DTOs carry at most this many bytes of
// such a URL so one inline resource cannot exhaust the event stream budget;
// routing and matching keep the original URL.
inline constexpr std::size_t kNativeObservedDataURLBytes = 1048576;

inline std::string NativeObservedURL(const std::string& url) {
  if (url.size() <= kNativeObservedDataURLBytes || url.rfind("data:", 0) != 0) return url;
  std::size_t end = kNativeObservedDataURLBytes;
  // Never end inside a UTF-8 sequence: back off continuation bytes (10xxxxxx).
  while (end > 0 && (static_cast<unsigned char>(url[end]) & 0xC0) == 0x80) --end;
  return url.substr(0, end);
}
}  // namespace suitecut
