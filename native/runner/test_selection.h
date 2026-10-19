// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <set>
#include <optional>
#include <stdexcept>
#include <vector>

namespace suitecut::runner {
struct TestSelection {
  std::vector<uint64_t> ids;
  uint32_t shard_current = 1;
  uint32_t shard_total = 1;
  bool forbid_only = false;
  // Native grep eligibility is distinct from explicit IDs: empty means none.
  std::optional<std::set<uint64_t>> eligible=std::nullopt;
  // Config grep filters each project catalog, including dependency support.
  std::optional<std::set<uint64_t>> catalog_eligible=std::nullopt;
};
// Select original declaration positions. The host never decides admission, and
// excluded declarations acquire neither fixture scopes nor test attempts.
inline std::vector<size_t> SelectTestIndices(const std::vector<uint64_t>& declarations,
                                           const TestSelection& selection,
                                           const std::vector<uint64_t>& focused = {},
                                           bool has_suite_focus = false) {
  if (declarations.size() > 4096 || selection.ids.size() > 4096 || focused.size() > 4096 ||
      !selection.shard_current || !selection.shard_total ||
      selection.shard_current > selection.shard_total || selection.shard_total > 65536)
    throw std::invalid_argument("Invalid native test selection or shard (current/total, total<=65536)");
  std::set<uint64_t> available, requested, focus;
  for (auto id : declarations)
    if (!id || !available.insert(id).second)
      throw std::invalid_argument("Invalid actual native test declaration identity");
  for (auto id : selection.ids)
    if (!available.contains(id) || !requested.insert(id).second)
      throw std::invalid_argument("Unknown or duplicate native selected test identity");
  if(selection.eligible)for(const auto id:*selection.eligible)
    if(!available.contains(id))throw std::invalid_argument("Unknown native grep eligibility identity");
  if(selection.catalog_eligible)for(const auto id:*selection.catalog_eligible)
    if(!available.contains(id))throw std::invalid_argument("Unknown native config grep catalog identity");
  for (auto id : focused)
    if (!available.contains(id) || !focus.insert(id).second)
      throw std::invalid_argument("Invalid native focused test identity");
  if (selection.forbid_only && (!focus.empty() || has_suite_focus))
    throw std::invalid_argument("Focused native test declarations are forbidden by --forbid-only");
  std::vector<size_t> matches;
  for (size_t index = 0; index < declarations.size(); ++index)
    if ((!selection.catalog_eligible || selection.catalog_eligible->contains(declarations[index])) &&
        (!selection.eligible || selection.eligible->contains(declarations[index])) &&
        (requested.empty() || requested.contains(declarations[index])) &&
        ((!has_suite_focus && focus.empty()) || focus.contains(declarations[index]))) matches.push_back(index);
  const size_t base = matches.size() / selection.shard_total;
  const size_t remainder = matches.size() % selection.shard_total;
  const size_t prior = selection.shard_current - 1;
  const size_t first = prior * base + std::min(prior, remainder);
  const size_t count = base + (selection.shard_current <= remainder ? 1 : 0);
  return {matches.begin() + first, matches.begin() + first + count};
}
}  // namespace suitecut::runner
