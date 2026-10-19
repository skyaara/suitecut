// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace suitecut {
// Authored URL glob matching. ? is a literal URL query separator; \ quotes
// the following character, * stays in a component, ** may cross separators.
inline bool NativeURLGlobSimple(const std::string& pattern, const std::string& url) {
  if (pattern.find_first_of("*\\") == std::string::npos) return pattern == url;
  std::vector<unsigned char> previous(url.size() + 1), next(url.size() + 1);
  previous[0] = 1;
  for (size_t p = 0; p < pattern.size(); ++p) {
    std::fill(next.begin(), next.end(), 0);
    if (pattern[p] == '*') {
      const bool deep = p + 1 < pattern.size() && pattern[p + 1] == '*';
      if (deep) while (p + 1 < pattern.size() && pattern[p + 1] == '*') ++p;
      next[0] = previous[0];
      for (size_t i = 1; i <= url.size(); ++i)
        next[i] = previous[i] || (next[i - 1] && (deep || url[i - 1] != '/'));
    } else if (pattern[p] == '\\' && p + 1 < pattern.size()) {
      const char literal = pattern[++p];
      for (size_t i = 1; i <= url.size(); ++i) next[i] = previous[i - 1] && literal == url[i - 1];
    } else {
      for (size_t i = 1; i <= url.size(); ++i) next[i] = previous[i - 1] && pattern[p] == url[i - 1];
    }
    previous.swap(next);
  }
  return previous.back();
}
// Bounded brace alternatives allow common **/*.{png,jpg} route patterns.
// Expansion is validated at route registration to avoid an exponential request
// callback. At most 256 flattened alternatives are accepted.
inline bool NativeURLGlobExpand(const std::string& input, std::vector<std::string>& output) {
  std::vector<std::string> pending{input};
  size_t examined = 0;
  while (!pending.empty()) {
    auto pattern = std::move(pending.back()); pending.pop_back();
    if (++examined > 1024 || pattern.size() > 8192) return false;
    bool expanded = false;
    for (size_t begin = 0; begin < pattern.size(); ++begin) {
      if (pattern[begin] == '\\') { if (begin + 1 < pattern.size()) ++begin; continue; }
      if (pattern[begin] != '{') continue;
      size_t end = begin + 1, start = end;
      unsigned depth = 0;
      std::vector<std::string> choices;
      for (; end < pattern.size(); ++end) {
        if (pattern[end] == '\\') { if (end + 1 < pattern.size()) ++end; continue; }
        if (pattern[end] == '{') ++depth;
        else if (pattern[end] == '}' && depth) --depth;
        else if (pattern[end] == '}' && !depth) break;
        else if (pattern[end] == ',' && !depth) {
          choices.push_back(pattern.substr(start, end - start)); start = end + 1;
        }
      }
      if (end == pattern.size() || choices.empty()) continue;
      choices.push_back(pattern.substr(start, end - start));
      if (choices.size() + pending.size() + output.size() > 256) return false;
      for (const auto& choice : choices)
        pending.push_back(pattern.substr(0, begin) + choice + pattern.substr(end + 1));
      expanded = true; break;
    }
    if (!expanded) {
      output.push_back(std::move(pattern));
      if (output.size() > 256) return false;
    }
  }
  return true;
}
inline bool NativeURLGlobMatches(const std::string& pattern, const std::string& url) {
  if (pattern.find('{') == std::string::npos) return NativeURLGlobSimple(pattern, url);
  std::vector<std::string> choices;
  if (!NativeURLGlobExpand(pattern, choices)) return false;
  return std::any_of(choices.begin(), choices.end(), [&](const auto& choice) { return NativeURLGlobSimple(choice, url); });
}
}  // namespace suitecut
