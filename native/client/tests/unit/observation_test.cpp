// SPDX-License-Identifier: MIT
#include "suitecut/observation.hpp"
#include <iostream>
#include <stdexcept>

using suitecut::compact_observation;
using suitecut::Json;
using suitecut::observation_diff;
namespace {
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class F>
void rejects(F work) {
  bool rejected = false;
  try {
    work();
  } catch (const std::exception&) {
    rejected = true;
  }
  check(rejected, "Expected bounded observation input rejection");
}
Json control(const char* name, const char* ref = "rFixture_1_0") {
  return Json{{"role", "button"}, {"name", name}, {"ref", ref}};
}
}  // namespace
int main() {
  try {
    const Json forest = Json::array{Json{
        {"role", "region"},
        {"name", "Checkout"},
        {"children", Json::array{"Instructions", control("Pay"),
                                 Json{{"role", "textbox"},
                                      {"name", "Password"},
                                      {"type", "password"},
                                      {"value", "private-password"},
                                      {"text", "private-password"},
                                      {"children", Json::array{"nested-password",
                                                               Json{{"role", "text"},
                                                                    {"name", "nested-password"}}}}},
                                 Json{{"role", "slider"},
                                      {"name", "Quantity"},
                                      {"states", Json{{"disabled", false}, {"valuenow", 3}}}}}}}};
    auto full = compact_observation(forest);
    check(full.at("nodes").as_array().size() == 7, "Flattening lost or duplicated nodes");
    check(!full.at("truncated").as_bool(), "Ordinary forest was truncated");
    check(full.at("nodes").at(2).at("index") == Json(1) &&
              full.at("nodes").at(2).at("parent") == Json(0) &&
              !full.at("nodes").at(0).contains("parent"),
          "Structural position is incorrect");
    check(
        full.at("ancestors").at(0) == Json{{"index", 0}, {"role", "region"}, {"name", "Checkout"}},
        "Named context lost");
    check(full.at("nodes").at(4).at("parent") == Json(1) &&
              full.at("ancestors").at(1) == Json{{"index", 2}, {"parent", 0}},
          "Protected container exposed its context or lost its position");
    check(full.at("ancestors").as_array().size() == 2, "Ancestor entries were repeated");
    check(full.dump().find("private-password") == std::string::npos &&
              full.dump().find("nested-password") == std::string::npos,
          "Password data leaked");
    check(full.at("nodes").at(3).at("name").as_string() == "Password", "Password label lost");
    check(full.at("nodes").at(6).at("valuenow").as_int() == 3, "Nested state was lost");
    suitecut::ObservationOptions options;
    options.interactive_only = true;
    auto controls = compact_observation(forest, options);
    check(controls.at("nodes").as_array().size() == 3, "Interactive filtering incorrect");
    check(controls.at("nodes").at(0).at("ref").as_string() == "rFixture_1_0", "Reference lost");
    check(controls.at("ancestors")
                  .at(static_cast<std::size_t>(controls.at("nodes").at(0).at("parent").as_int()))
                  .at("name")
                  .as_string() == "Checkout",
          "Filtering lost context");
    const auto custom = compact_observation(Json::array{Json{{"role", "generic"},
                                                             {"ref", "rFixture_1_2"},
                                                             {"name", "Custom handle"},
                                                             {"cursor", "pointer"},
                                                             {"box", Json::array{1, 2, 10, 20}}},
                                                        Json{{"role", "generic"},
                                                             {"ref", "rFixture_1_3"},
                                                             {"name", "Inert referenced layout"}}},
                                            options);
    check(custom.at("nodes").as_array().size() == 1 &&
              custom.at("nodes").at(0).at("box") == Json(Json::array{1, 2, 10, 20}),
          "Custom clickable control lost or inert layout advertised as interactive");

    options.max_nodes = 1;
    auto bounded = compact_observation(forest, options);
    check(bounded.at("nodes").as_array().size() == 1 && bounded.at("nodeLimit").as_bool() &&
              bounded.at("truncated").as_bool(),
          "Node budget is not explicit");
    options = {};
    options.max_scan_nodes = 2;
    bounded = compact_observation(forest, options);
    check(bounded.at("scannedNodes").as_int() == 2 && bounded.at("scanLimit").as_bool(),
          "Scan budget exceeded");
    options = {};
    options.max_text_bytes = 6;  // Four role bytes plus an incomplete three-byte scalar.
    bounded = compact_observation(Json::array{Json{{"role", "text"}, {"text", "€€"}}}, options);
    check(bounded.at("textLimit").as_bool() && bounded.at("textBytes").as_int() == 4,
          "UTF8 byte budget split a scalar");
    (void)Json::parse(bounded.dump());
    options.max_text_bytes = 7;
    bounded = compact_observation(Json::array{Json{{"role", "text"}, {"text", "€€"}}}, options);
    check(bounded.at("nodes").at(0).at("text").as_string() == "€", "UTF8 prefix was not preserved");
    options = {};
    options.max_text_bytes = 8;
    bounded = compact_observation(Json::array{control("Pay")}, options);
    check(!bounded.at("nodes").at(0).contains("ref"), "Partial actionable reference exposed");
    options = {};
    options.max_nodes = 0;  // 0 means no limit.
    check(!compact_observation(Json::array{control("Pay")}, options).at("truncated").as_bool(),
          "Zero node option limited output");
    check(!compact_observation(Json::array{}, options).at("truncated").as_bool(),
          "Empty forest falsely truncated");

    auto previous =
        compact_observation(Json::array{control("Save"), control("Cancel", "rFixture_1_1")});
    auto current = compact_observation(
        Json::array{control("Cancel", "rFixture_99_8"), control("Save", "rFixture_99_9")});
    auto diff = observation_diff(previous, current);
    check(diff.at("changed").as_array().empty() && diff.at("added").as_array().empty() &&
              diff.at("removed").as_array().empty(),
          "References/reordering caused semantic changes");
    current = compact_observation(
        Json::array{control("Saved", "rFixture_2_0"), control("Cancel", "rFixture_2_1")});
    diff = observation_diff(previous, current);
    check(diff.at("changed").as_array().size() == 1 && diff.at("added").as_array().empty() &&
              diff.at("changed").at(0).at("after").at("ref").as_string() == "rFixture_2_0",
          "Rename/current ref lost");
    auto before_value = Json{
        {"role", "textbox"}, {"name", "Email"}, {"value", "a@example.org"}, {"disabled", false}};
    auto after_value = before_value;
    after_value["value"] = "b@example.org";
    after_value["disabled"] = true;
    diff = observation_diff(compact_observation(Json::array{before_value}),
                            compact_observation(Json::array{after_value}));
    check(diff.at("changed").as_array().size() == 1, "State/value change not detected");
    diff =
        observation_diff(compact_observation(Json::array{"Instruction one", "Instruction two"}),
                         compact_observation(Json::array{"Instruction one", "Instruction three"}));
    check(!diff.at("ambiguous").as_bool() && diff.at("changed").as_array().size() == 1,
          "Distinct text nodes were treated as duplicate controls");
    auto duplicates =
        compact_observation(Json::array{control("Same"), control("Same", "rFixture_1_1")});
    diff = observation_diff(duplicates, duplicates);
    check(diff.at("ambiguous").as_bool() && diff.at("changed").as_array().empty() &&
              diff.at("added").as_array().size() == 2 && diff.at("removed").as_array().size() == 2,
          "Duplicate controls were paired arbitrarily");
    options = {};
    options.max_nodes = 1;
    diff = observation_diff(
        previous, compact_observation(Json::array{control("Save"), control("Cancel")}, options));
    check(diff.at("removed").as_array().empty() && diff.at("removalsSuppressed").as_bool() &&
              !diff.at("complete").as_bool(),
          "Partial observation caused false removal claims");
    diff = observation_diff(previous, compact_observation(Json::array{}), 1);
    check(diff.at("removed").as_array().size() == 1 && diff.at("truncated").as_bool() &&
              !diff.at("complete").as_bool(),
          "Difference output cap ignored");

    Json deep = Json::array{control("Deep")};
    for (int i = 0; i < 70; ++i)
      deep = Json::array{Json{{"role", "group"}, {"children", std::move(deep)}}};
    const auto deep_observation = compact_observation(deep);
    check(!deep_observation.at("truncated").as_bool() &&
              deep_observation.at("nodes").as_array().size() == 71,
          "Deep forest was cut short");
    {
      // Every node refers to its chain by one index, so output stays linear at any depth (it used
      // to copy every named ancestor into every node: nodes x depth).
      constexpr std::size_t levels = 30000;
      const auto tower = [&](const char* leaf, const char* group) {
        Json level = Json::array{control(leaf)};
        for (std::size_t i = 0; i < levels; ++i) {
          Json::array children{control("Step")};
          Json wrapper{{"role", "group"}, {"name", i + 1 == levels ? group : "Level"}};
          wrapper["children"] =
              std::move(level);  // A braced initializer would copy the subtree each level.
          children.push_back(std::move(wrapper));
          level = Json(std::move(children));
        }
        return level;
      };
      const auto tall = compact_observation(tower("Deepest", "Top"));
      const auto& nodes = tall.at("nodes").as_array();
      check(!tall.at("truncated").as_bool() && nodes.size() == 2 * levels + 1 &&
                tall.at("ancestors").as_array().size() == levels,
            "Deep forest lost nodes or repeated ancestors");
      check(tall.dump().size() < 200 * nodes.size(), "Deep observation output is not linear");
      std::size_t named = 0;
      for (auto at = nodes.back().at("parent");;) {
        const auto& entry = tall.at("ancestors").at(static_cast<std::size_t>(at.as_int()));
        named += entry.at("role") == Json("group");
        if (!entry.contains("parent")) {
          check(entry.at("name") == Json("Top"), "Outermost context lost");
          break;
        }
        at = entry.at("parent");
      }
      check(nodes.back().at("name") == Json("Deepest") && named == levels,
            "Deepest node lost part of its context");
      auto refreshed = tall;
      for (auto& node : refreshed["nodes"].as_array()) node["ref"] = "fresh";
      auto deep_diff = observation_diff(tall, refreshed);
      check(deep_diff.at("changed").as_array().empty() &&
                deep_diff.at("added").as_array().empty() &&
                deep_diff.at("ancestors").as_array().empty(),
            "Deep ref refresh became a change");
      deep_diff = observation_diff(tall, compact_observation(tower("Renamed", "Top")));
      check(deep_diff.at("changed").as_array().size() == 1 &&
                deep_diff.at("ancestors").as_array().size() == 2 * levels &&
                deep_diff.at("changed").at(0).at("after").at("name") == Json("Renamed"),
            "Deep rename lost or carried no context");
      // The same slot under a renamed outer group is a context change, found through the interned
      // chains.
      deep_diff = observation_diff(tall, compact_observation(tower("Deepest", "Moved")));
      // Only the top-level step keeps its context; the renamed group and all it encloses changed.
      check(deep_diff.at("changed").as_array().size() == 2 * levels,
            "Outer context change was not reported for its descendants");
    }
    {
      // A slot keeps its identity only with the same context: a moved control is a change, not a
      // match.
      const auto in = [](const char* group) {
        return compact_observation(Json::array{
            Json{{"role", "group"}, {"name", group}, {"children", Json::array{control("Save")}}}});
      };
      const auto moved = observation_diff(in("Billing"), in("Shipping"));
      check(moved.at("changed").as_array().size() == 2 &&
                moved.at("ancestors").as_array().size() == 2,
            "Context change lost");
      for (const auto& change : moved.at("changed").as_array()) {
        if (!change.at("after").contains("ref")) continue;
        const auto& ancestors = moved.at("ancestors");
        check(ancestors.at(static_cast<std::size_t>(change.at("before").at("parent").as_int()))
                          .at("name") == Json("Billing") &&
                  ancestors.at(static_cast<std::size_t>(change.at("after").at("parent").as_int()))
                          .at("name") == Json("Shipping"),
              "Diff ancestors were not self-contained");
      }
      check(observation_diff(in("Billing"), in("Billing")).at("changed").as_array().empty(),
            "Equal context reported as changed");
      rejects([&] {
        auto broken = in("Billing");
        broken["nodes"].as_array()[1]["parent"] = 7;
        (void)observation_diff(broken, broken);
      });
      rejects([&] {
        auto broken = in("Billing");
        broken["ancestors"].as_array()[0]["parent"] = 0;
        (void)observation_diff(broken, broken);
      });
    }
    Json::array long_text(5000, Json(std::string(512, 'x')));
    bounded = compact_observation(long_text);
    check(!bounded.at("truncated").as_bool() && bounded.at("nodes").as_array().size() == 5000 &&
              bounded.at("textBytes").as_int() > 1024 * 1024,
          "Default observation was limited");
    Json::array wide(100000, control("Repeated"));
    options = {};
    options.interactive_only = true;
    options.max_nodes = 16;
    bounded = compact_observation(wide, options);
    check(bounded.at("scannedNodes").as_int() == 17 && bounded.at("nodes").as_array().size() == 16,
          "Wide input is not scanned lazily");
    rejects([] { (void)compact_observation(Json{{"role", "button"}}); });
    {
      suitecut::ObservationOptions limits;
      limits.max_nodes = 4097;
      limits.max_text_bytes = 1024 * 1024 + 1;
      limits.max_scan_nodes = 65537;
      check(!compact_observation(Json::array{}, limits).at("truncated").as_bool(),
            "Large caller limits were rejected");
    }
    (void)observation_diff(previous, current, 4097);
    rejects([&] { (void)observation_diff(Json::array{}, current); });
    std::cout
        << "Compact observation bounds, UTF8, privacy, filtering and semantic differences passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
