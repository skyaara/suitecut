// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/json.hpp"
#include <string>

namespace suitecut {
// Comma-separated, deterministic capability groups. These are catalog choices,
// not a security sandbox. The CLI enforces the selected catalog on dispatch.
// core, network, state, debug, media, all. Advanced groups include core.
Json agent_tool_catalog(const std::string& profiles = "core");
Json agent_tool_profiles();
} // namespace suitecut
