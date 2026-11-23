// SPDX-License-Identifier: MIT
#include "network/observed_url.h"

#include <cassert>
#include <string>

int main() {
  using suitecut::NativeObservedURL;
  // Ordinary URLs and data: URLs of any size pass through whole.
  const std::string page = "https://example.test/" + std::string(9000, 'a');
  assert(NativeObservedURL(page) == page);
  const std::string small = "data:image/png;base64,AAAA";
  assert(NativeObservedURL(small) == small);
  const std::string large = "data:image/png;base64," + std::string(3 * 1048576, 'A');
  assert(NativeObservedURL(large) == large);
  std::string text = "data:text/plain," + std::string(1048576 - 17, 'x');
  text += "\xF0\x9F\x98\x80";
  text += std::string(64, 'y');
  assert(NativeObservedURL(text) == text);
  return 0;
}
