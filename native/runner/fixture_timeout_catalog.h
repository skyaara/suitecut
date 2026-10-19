// SPDX-License-Identifier: MIT
#pragma once
#include "fixture_catalog.h"
#include "fixture_timeout_clocks.h"

namespace suitecut::runner {
struct FixtureTimeoutDeclaration { uint64_t declaration = 0, timeout = 0; };
// Native resolver for raw absent-vs-zero timeout metadata. Resolved versions
// retain exact source declaration identity; no author-name parsing or JS choice.
class FixtureTimeoutCatalog {
 public:
  struct Binding {
    uint64_t declaration = 0, origin = 0;
    std::optional<uint64_t> timeout;
  };
  FixtureTimeoutCatalog(const FixtureCatalog& graph,
                        const std::vector<FixtureTimeoutDeclaration>& declarations) {
    if (declarations.size() > FixtureCatalog::kDeclarations)
      throw std::length_error("Native fixture timeout declaration capacity exceeded");
    std::map<uint64_t, uint64_t> raw;
    for (const auto& declaration : declarations) {
      (void)graph.Get(declaration.declaration); // Exact real raw declaration required.
      if (declaration.timeout > FixtureChargedClock::kMaxTimeout ||
          !raw.emplace(declaration.declaration, declaration.timeout).second)
        throw std::invalid_argument("Native fixture timeout metadata rejected");
    }
    for (const auto& version : graph.Versions()) {
      Binding binding{version.id, 0, std::nullopt};
      if (version.previous) {
        const auto& previous = Get(version.previous);
        binding.origin = previous.origin; binding.timeout = previous.timeout;
      }
      if (const auto found = raw.find(version.id); found != raw.end()) {
        binding.timeout = found->second; binding.origin = version.id;
      }
      bindings_.emplace(version.id, binding);
      keys_.emplace(version.key, version.id);
    }
  }
  const Binding& Get(uint64_t declaration) const {
    const auto found = bindings_.find(declaration);
    if (found == bindings_.end()) throw std::invalid_argument("Unknown native fixture timeout declaration");
    return found->second;
  }
  const Binding& Resolve(const std::string& native_key) const {
    const auto found = keys_.find(native_key);
    if (found == keys_.end()) throw std::invalid_argument("Unknown native fixture timeout version key");
    return Get(found->second);
  }
 private:
  std::map<uint64_t, Binding> bindings_;
  std::map<std::string, uint64_t> keys_;
};
} // namespace suitecut::runner
