// SPDX-License-Identifier: MIT
#include "observed_url.h"

#include <cassert>
#include <string>

int main() {
  using suitecut::kNativeObservedDataURLBytes;
  using suitecut::NativeObservedURL;
  // Ordinary URLs and small data: URLs pass through unchanged.
  const std::string page = "https://example.test/" + std::string(9000, 'a');
  assert(NativeObservedURL(page) == page);
  const std::string small = "data:image/png;base64,AAAA";
  assert(NativeObservedURL(small) == small);
  const std::string exact = "data:," + std::string(kNativeObservedDataURLBytes - 6, 'A');
  assert(NativeObservedURL(exact) == exact);

  // Oversized data: URLs keep a bounded prefix.
  const std::string large = "data:image/png;base64," + std::string(3 * kNativeObservedDataURLBytes, 'A');
  const auto bounded = NativeObservedURL(large);
  assert(bounded.size() == kNativeObservedDataURLBytes);
  assert(large.compare(0, bounded.size(), bounded) == 0);

  // A cut never splits a multi-byte UTF-8 character.
  std::string text = "data:text/plain," + std::string(kNativeObservedDataURLBytes - 17, 'x');
  text += "\xF0\x9F\x98\x80";  // U+1F600 straddles the limit
  text += std::string(64, 'y');
  const auto cut = NativeObservedURL(text);
  assert(cut.size() == kNativeObservedDataURLBytes - 1);
  assert(cut.back() == 'x');
  return 0;
}
