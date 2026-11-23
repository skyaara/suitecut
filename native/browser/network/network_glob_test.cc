// SPDX-License-Identifier: MIT
#include "network/network_glob.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
// Small independent recursive oracle for single/deep stars and quoted literals.
// Fuzz inputs are deliberately short so exhaustive states remain bounded.
bool Reference(const std::string& pattern, const std::string& url, size_t p, size_t u,
               std::map<std::pair<size_t, size_t>, bool>& cache) {
  const auto key = std::make_pair(p, u);
  const auto old = cache.find(key);
  if (old != cache.end()) return old->second;
  bool result = false;
  if (p == pattern.size())
    result = u == url.size();
  else if (pattern[p] == '*') {
    size_t end = p + 1;
    while (end < pattern.size() && pattern[end] == '*') ++end;
    const bool deep = end - p > 1;
    for (size_t consumed = u; consumed <= url.size(); ++consumed) {
      if (Reference(pattern, url, end, consumed, cache)) {
        result = true;
        break;
      }
      if (consumed == url.size() || (!deep && url[consumed] == '/')) break;
    }
  } else {
    const bool quote = pattern[p] == '\\' && p + 1 < pattern.size();
    const char literal = pattern[p + (quote ? 1 : 0)];
    result = u < url.size() && url[u] == literal &&
             Reference(pattern, url, p + (quote ? 2 : 1), u + 1, cache);
  }
  cache[key] = result;
  return result;
}
uint64_t Next(uint64_t& seed) {
  seed ^= seed << 13;
  seed ^= seed >> 7;
  seed ^= seed << 17;
  return seed;
}
// The pre-compilation semantics: expand every brace alternative, then match each.
bool Expanded(const std::string& pattern, const std::string& url) {
  std::vector<std::string> choices;
  suitecut::NativeURLGlobExpand(pattern, choices);
  for (const auto& choice : choices)
    if (suitecut::NativeURLGlobSimple(choice, url)) return true;
  return false;
}
}  // namespace
int main(int argc, char** argv) {
  using suitecut::NativeURLGlobMatches;
  assert(NativeURLGlobMatches("**/api/*", "https://example.test/api/data"));
  assert(!NativeURLGlobMatches("**/api/*", "https://example.test/api/deep/data"));
  assert(NativeURLGlobMatches("**/api/**", "https://example.test/api/deep/data"));
  assert(NativeURLGlobMatches("**?a=1", "https://example.test/?a=1"));
  assert(!NativeURLGlobMatches("**?a=1", "https://example.test/xa=1"));
  assert(NativeURLGlobMatches("**/*.{png,jpg}", "https://example.test/img/cat.jpg"));
  assert(!NativeURLGlobMatches("**/*.{png,jpg}", "https://example.test/img/cat.js"));
  assert(NativeURLGlobMatches("**/{a,{b,c}}/*.js", "https://example.test/c/file.js"));
  assert(NativeURLGlobMatches("a\\*b", "a*b"));
  assert(NativeURLGlobMatches("a\\?b", "a?b"));
  assert(NativeURLGlobMatches("**", ""));
  std::vector<std::string> alternatives;
  assert(suitecut::NativeURLGlobExpand("{a,b}{a,b}{a,b}{a,b}{a,b}{a,b}{a,b}{a,b}{a,b}",
                                       alternatives) &&
         alternatives.size() == 512);
  const std::string long_pattern(65536, 'x');
  assert(NativeURLGlobMatches(long_pattern + "{a,b}", long_pattern + "b"));
  // Compiled brace groups agree with expansion, including ** formed across a
  // group edge, literal braces of comma-less groups and escaped separators.
  for (const auto& [pattern, url] :
       std::vector<std::pair<std::string, std::string>>{{"*{*,a}", "x/y"},
                                                        {"*{*,a}", "xa"},
                                                        {"*{*,a}", "x/a"},
                                                        {"{,}{*,}*", "a/b"},
                                                        {"{a{b,c}}", "{ab}"},
                                                        {"{a{b,c}}", "ab"},
                                                        {"{a,b", "{a,b"},
                                                        {"a\\{b,c}", "a{b,c}"},
                                                        {"a\\{b,c}", "ab"},
                                                        {"{a\\,b,c}", "a,b"},
                                                        {"{a\\,b,c}", "c"},
                                                        {"{a\\,b,c}", "a"},
                                                        {"{}", "{}"},
                                                        {"x}{a,b}", "x}a"},
                                                        {"{a,b}\\", "a\\"},
                                                        {"**/{a,{b,c}}/*.js", "https://e/c/f.js"},
                                                        {"{*,b}/*", "a/b"}}) {
    const suitecut::NativeURLGlob glob(pattern);
    if (glob.Matches(url) != Expanded(pattern, url)) {
      std::fprintf(stderr, "compiled glob disagreement: pattern=%s url=%s\n", pattern.c_str(),
                   url.c_str());
      return 1;
    }
  }
  assert(suitecut::NativeURLGlob("*{*,a}").Matches("x/y") &&
         !suitecut::NativeURLGlob("*{b,a}").Matches("x/a"));
  // 24 groups would expand to 16M alternatives; the compiled glob matches without expanding.
  std::string groups = "**/", wanted = "https://example.test/", unwanted = wanted;
  for (int i = 0; i < 24; ++i) {
    groups += "{a,b}";
    wanted += i % 2 ? "a" : "b";
    unwanted += "b";
  }
  unwanted.back() = 'c';
  const suitecut::NativeURLGlob compiled(groups);
  assert(compiled.pattern() == groups);
  for (int i = 0; i < 1000; ++i) assert(compiled.Matches(wanted) && !compiled.Matches(unwanted));
  assert(NativeURLGlobMatches(groups, wanted));
  // Deep nesting compiles and matches iteratively.
  const std::string open(100000, '{'), shut(100000, '}');
  assert(
      suitecut::NativeURLGlob(open + "a,b" + shut).Matches(open.substr(1) + "b" + shut.substr(1)));
  const uint64_t iterations = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 100000;
  uint64_t seed = 0x72993a47cd61ULL;
  const std::string alphabet = "ab/*?\\";
  for (uint64_t round = 0; round < iterations; ++round) {
    std::string pattern, url;
    const auto plen = Next(seed) % 17, ulen = Next(seed) % 17;
    for (uint64_t i = 0; i < plen; ++i) pattern.push_back(alphabet[Next(seed) % alphabet.size()]);
    for (uint64_t i = 0; i < ulen; ++i) url.push_back(alphabet[Next(seed) % alphabet.size()]);
    std::map<std::pair<size_t, size_t>, bool> cache;
    const auto expected = Reference(pattern, url, 0, 0, cache);
    const auto actual = NativeURLGlobMatches(pattern, url);
    if (expected != actual) {
      std::fprintf(stderr, "URL glob disagreement: pattern=%s url=%s\n", pattern.c_str(),
                   url.c_str());
      return 1;
    }
    // Same budget for brace patterns against the expansion oracle.
    std::string braced, probe;
    const std::string braces = "ab/*?\\{},";
    const auto blen = Next(seed) % 14, qlen = Next(seed) % 9;
    for (uint64_t i = 0; i < blen; ++i) braced.push_back(braces[Next(seed) % braces.size()]);
    for (uint64_t i = 0; i < qlen; ++i) probe.push_back(braces[Next(seed) % braces.size()]);
    if (Next(seed) % 2) {
      std::vector<std::string> choices;
      suitecut::NativeURLGlobExpand(braced, choices);
      probe.clear();
      for (const char c : choices[Next(seed) % choices.size()])
        if (c != '\\')
          probe += c == '*' ? std::string(Next(seed) % 2 ? "a" : "a/b") : std::string(1, c);
    }
    if (suitecut::NativeURLGlob(braced).Matches(probe) != Expanded(braced, probe)) {
      std::fprintf(stderr, "compiled glob disagreement: pattern=%s url=%s\n", braced.c_str(),
                   probe.c_str());
      return 1;
    }
    if (round % 1000 == 0) {
      const std::string literal(8192, 'x');
      assert(NativeURLGlobMatches(literal, literal));
      assert(NativeURLGlobMatches("**", literal));
    }
  }
  std::printf("native URL glob oracle fuzz passed: %llu cases\n",
              static_cast<unsigned long long>(iterations));
}
