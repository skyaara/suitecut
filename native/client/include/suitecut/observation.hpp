// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/json.hpp"
#include <cstddef>

namespace suitecut {
struct ObservationOptions {
  bool interactive_only = false;
  std::size_t max_nodes = 256;
  // Aggregate UTF-8 string bytes, including references and repeated context.
  std::size_t max_text_bytes = 32 * 1024;
  std::size_t max_scan_nodes = 16 * 1024;
};

// Processes the forest returned by Page::observe without further browser calls.
// Returns flat nodes with structural paths, useful states, actionable references,
// and up to four named ancestor contexts. Explicit limit flags distinguish an
// empty page from partial output. Password-marked values/text are omitted.
// Hard ceilings: 4096 output nodes, 1 MiB text, 65536 visited nodes, depth 64.
Json compact_observation(const Json& forest, ObservationOptions options = {});

// Stateless comparison of compact_observation outputs. Reference/box changes
// alone are ignored. Unique semantic identities survive reordering; a unique
// structural slot can identify a renamed control. Duplicate identities are
// conservatively reported as additions/removals with ambiguous=true. Removals
// are suppressed for incomplete observations. Results retain current refs.
// This is a UI difference, not proof that two same-looking nodes are identical.
Json observation_diff(const Json& previous, const Json& current,
                      std::size_t max_changes = 256);
} // namespace suitecut
