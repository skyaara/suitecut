// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/json.hpp"
#include <string>
#include <vector>

namespace suitecut::control {
Json cli_request(const std::vector<std::string>& words,Json observe_options,
                 const std::string& response="compact");
std::string cli_help();
// Explicit, exclusive output. Screenshots are decoded; other results are JSON.
Json save_output(const Json& reply,const std::string& path);
} // namespace suitecut::control
