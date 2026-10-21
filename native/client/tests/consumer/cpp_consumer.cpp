// SPDX-License-Identifier: MIT
#include <suitecut/agent_tools.hpp>
#include <suitecut/observation.hpp>
#include <suitecut/browser.hpp>
#include <suitecut/image.hpp>
#include <stdexcept>
int main() {
  const auto value = suitecut::Json::parse("{\"integer\":9007199254740993}");
  if (value.at("integer").as_int() != 9007199254740993LL) return 1;
  if (suitecut::agent_tool_catalog().is_null()) return 2;
  if (suitecut::compact_observation(suitecut::Json::array{}).is_null()) return 3;
  // Exercise image error handling across the installed library boundary.
  try { suitecut::decode_png({}); return 4; } catch (const std::exception&) {}
  suitecut::Packet packet;
  packet.header = "{}";
  packet.payload = {'{', '}'};
  if (!suitecut::response_json(packet).is_object()) return 5;
  return 0;
}
