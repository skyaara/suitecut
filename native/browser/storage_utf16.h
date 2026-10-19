// SPDX-License-Identifier: MIT
#pragma once
#include <string>
namespace suitecut {
// CEF JSON dictionaries use UTF-8 strings and reject isolated UTF-16 units.
// Only the validation projection substitutes units; retained stateJSON and the
// renderer/SDK round-trip keep the original JavaScript JSON text unchanged.
inline std::string StorageJSONValidationProjection(const std::string& input) {
  const auto unit = [&](size_t start) -> int {
    if (start + 6 > input.size() || input[start] != '\\' || input[start + 1] != 'u') return -1;
    int result = 0;
    for (size_t offset = 2; offset < 6; ++offset) {
      const auto ch = input[start + offset];
      const int digit = ch >= '0' && ch <= '9' ? ch - '0' : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10 : ch >= 'A' && ch <= 'F' ? ch - 'A' + 10 : -1;
      if (digit < 0) return -1;
      result = (result << 4) | digit;
    }
    return result;
  };
  std::string output; output.reserve(input.size()); bool text = false;
  constexpr char hex[] = "0123456789abcdef";
  for (size_t pos = 0; pos < input.size();) {
    const char ch = input[pos];
    if (ch == '"') { text = !text; output += ch; ++pos; continue; }
    if (!text || ch != '\\' || pos + 1 == input.size()) { output += ch; ++pos; continue; }
    const int first = unit(pos);
    if (first >= 0xd800 && first <= 0xdbff) {
      const int second = unit(pos + 6);
      if (second >= 0xdc00 && second <= 0xdfff) { output.append(input, pos, 12); pos += 12; continue; }
    }
    if (first >= 0xd800 && first <= 0xdfff) {
      const unsigned safe = 0xe000 + first - 0xd800;
      output += "\\u"; for (int shift = 12; shift >= 0; shift -= 4) output += hex[(safe >> shift) & 15];
      pos += 6; continue;
    }
    if (first >= 0) { output.append(input, pos, 6); pos += 6; }
    else { output.append(input, pos, 2); pos += 2; }
  }
  return output;
}
}
