// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/json.hpp"
#include <cstddef>

namespace suitecut {
struct ObservationOptions {
  bool interactive_only = false;
  // Each limit is optional: 0 (the default) means no limit.
  std::size_t max_nodes = 0;
  // Aggregate UTF-8 string bytes, including references and repeated context.
  std::size_t max_text_bytes = 0;
  std::size_t max_scan_nodes = 0;
};

// Processes the forest returned by Page::observe without further browser calls.
// Returns flat nodes with structural paths, useful states, actionable references,
// and every named ancestor context. Explicit limit flags distinguish an
// empty page from partial output. Password-marked values/text are omitted.
Json compact_observation(const Json& forest, ObservationOptions options = {});

// Stateless comparison of compact_observation outputs. Reference/box changes
// alone are ignored. Unique semantic identities survive reordering; a unique
// structural slot can identify a renamed control. Duplicate identities are
// conservatively reported as additions/removals with ambiguous=true. Removals
// are suppressed for incomplete observations. Results retain current refs.
// This is a UI difference, not proof that two same-looking nodes are identical.
// max_changes 0 (the default) reports every change.
Json observation_diff(const Json& previous, const Json& current,
                      std::size_t max_changes = 0);
} // namespace suitecut
