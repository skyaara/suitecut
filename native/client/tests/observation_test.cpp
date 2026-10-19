// SPDX-License-Identifier: MIT
#include "suitecut/observation.hpp"
#include <iostream>
#include <stdexcept>

using suitecut::Json;
using suitecut::compact_observation;
using suitecut::observation_diff;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F work) {
  bool rejected = false;
  try { work(); } catch (const std::exception&) { rejected = true; }
  check(rejected, "Expected bounded observation input rejection");
}
Json control(const char* name, const char* ref = "rFixture_1_0") {
  return Json{{"role", "button"}, {"name", name}, {"ref", ref}};
}
}
int main() {
  try {
    const Json forest = Json::array{
      Json{{"role", "region"}, {"name", "Checkout"}, {"children", Json::array{
        "Instructions", control("Pay"),
        Json{{"role", "textbox"}, {"name", "Password"}, {"type", "password"},
             {"value", "private-password"}, {"text", "private-password"},
             {"children", Json::array{"nested-password", Json{{"role", "text"}, {"name", "nested-password"}}}}},
        Json{{"role", "slider"}, {"name", "Quantity"}, {"states", Json{{"disabled", false}, {"valuenow", 3}}}}
      }}}
    };
    auto full = compact_observation(forest);
    check(full.at("nodes").as_array().size() == 7, "Flattening lost or duplicated nodes");
    check(!full.at("truncated").as_bool(), "Ordinary forest was truncated");
    check(full.at("nodes").at(2).at("path") == Json(Json::array{0, 1}), "Structural path is incorrect");
    check(full.at("nodes").at(2).at("context").at(0).at("name").as_string() == "Checkout", "Named context lost");
    check(full.dump().find("private-password") == std::string::npos &&
          full.dump().find("nested-password") == std::string::npos, "Password data leaked");
    check(full.at("nodes").at(3).at("name").as_string() == "Password", "Password label lost");
    check(full.at("nodes").at(6).at("valuenow").as_int() == 3, "Nested state was lost");
    suitecut::ObservationOptions options;
    options.interactive_only = true;
    auto controls = compact_observation(forest, options);
    check(controls.at("nodes").as_array().size() == 3, "Interactive filtering incorrect");
    check(controls.at("nodes").at(0).at("ref").as_string() == "rFixture_1_0", "Reference lost");
    check(controls.at("nodes").at(0).at("context").at(0).at("name").as_string() == "Checkout", "Filtering lost context");
    const auto custom = compact_observation(Json::array{
      Json{{"role", "generic"}, {"ref", "rFixture_1_2"}, {"name", "Custom handle"},
           {"cursor", "pointer"}, {"box", Json::array{1, 2, 10, 20}}},
      Json{{"role", "generic"}, {"ref", "rFixture_1_3"}, {"name", "Inert referenced layout"}}
    }, options);
    check(custom.at("nodes").as_array().size() == 1 &&
          custom.at("nodes").at(0).at("box") == Json(Json::array{1, 2, 10, 20}),
          "Custom clickable control lost or inert layout advertised as interactive");

    options.max_nodes = 1;
    auto bounded = compact_observation(forest, options);
    check(bounded.at("nodes").as_array().size() == 1 && bounded.at("nodeLimit").as_bool() &&
          bounded.at("truncated").as_bool(), "Node budget is not explicit");
    options = {};
    options.max_scan_nodes = 2;
    bounded = compact_observation(forest, options);
    check(bounded.at("scannedNodes").as_int() == 2 && bounded.at("scanLimit").as_bool(), "Scan budget exceeded");
    options = {};
    options.max_text_bytes = 6; // Four role bytes plus an incomplete three-byte scalar.
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
    options.max_nodes = 0;
    check(compact_observation(Json::array{control("Pay")}, options).at("truncated").as_bool(), "Zero node cap ignored");
    check(!compact_observation(Json::array{}, options).at("truncated").as_bool(), "Empty forest falsely truncated");

    auto previous = compact_observation(Json::array{control("Save"), control("Cancel", "rFixture_1_1")});
    auto current = compact_observation(Json::array{control("Cancel", "rFixture_99_8"), control("Save", "rFixture_99_9")});
    auto diff = observation_diff(previous, current);
    check(diff.at("changed").as_array().empty() && diff.at("added").as_array().empty() &&
          diff.at("removed").as_array().empty(), "References/reordering caused semantic changes");
    current = compact_observation(Json::array{control("Saved", "rFixture_2_0"), control("Cancel", "rFixture_2_1")});
    diff = observation_diff(previous, current);
    check(diff.at("changed").as_array().size() == 1 && diff.at("added").as_array().empty() &&
          diff.at("changed").at(0).at("after").at("ref").as_string() == "rFixture_2_0", "Rename/current ref lost");
    auto before_value = Json{{"role", "textbox"}, {"name", "Email"}, {"value", "a@example.org"}, {"disabled", false}};
    auto after_value = before_value;
    after_value["value"] = "b@example.org";
    after_value["disabled"] = true;
    diff = observation_diff(compact_observation(Json::array{before_value}), compact_observation(Json::array{after_value}));
    check(diff.at("changed").as_array().size() == 1, "State/value change not detected");
    diff = observation_diff(compact_observation(Json::array{"Instruction one", "Instruction two"}),
                            compact_observation(Json::array{"Instruction one", "Instruction three"}));
    check(!diff.at("ambiguous").as_bool() && diff.at("changed").as_array().size() == 1,
          "Distinct text nodes were treated as duplicate controls");
    auto duplicates = compact_observation(Json::array{control("Same"), control("Same", "rFixture_1_1")});
    diff = observation_diff(duplicates, duplicates);
    check(diff.at("ambiguous").as_bool() && diff.at("changed").as_array().empty() &&
          diff.at("added").as_array().size() == 2 && diff.at("removed").as_array().size() == 2,
          "Duplicate controls were paired arbitrarily");
    options = {};
    options.max_nodes = 1;
    diff = observation_diff(previous, compact_observation(Json::array{control("Save"), control("Cancel")}, options));
    check(diff.at("removed").as_array().empty() && diff.at("removalsSuppressed").as_bool() &&
          !diff.at("complete").as_bool(), "Partial observation caused false removal claims");
    diff = observation_diff(previous, compact_observation(Json::array{}), 1);
    check(diff.at("removed").as_array().size() == 1 && diff.at("truncated").as_bool() &&
          !diff.at("complete").as_bool(), "Difference output cap ignored");

    Json deep = Json::array{control("Deep")};
    for (int i = 0; i < 70; ++i) deep = Json::array{Json{{"role", "group"}, {"children", std::move(deep)}}};
    check(compact_observation(deep).at("depthLimit").as_bool(), "Depth budget ignored");
    Json::array wide(100000, control("Repeated"));
    options = {};
    options.interactive_only = true;
    options.max_nodes = 16;
    bounded = compact_observation(wide, options);
    check(bounded.at("scannedNodes").as_int() == 17 && bounded.at("nodes").as_array().size() == 16,
          "Wide input is not scanned lazily");
    rejects([] { (void)compact_observation(Json{{"role", "button"}}); });
    rejects([] { suitecut::ObservationOptions limits; limits.max_nodes = 4097; (void)compact_observation(Json::array{}, limits); });
    rejects([] { suitecut::ObservationOptions limits; limits.max_text_bytes = 1024 * 1024 + 1; (void)compact_observation(Json::array{}, limits); });
    rejects([] { suitecut::ObservationOptions limits; limits.max_scan_nodes = 65537; (void)compact_observation(Json::array{}, limits); });
    rejects([&] { (void)observation_diff(previous, current, 4097); });
    rejects([&] { (void)observation_diff(Json::array{}, current); });
    std::cout << "Compact observation bounds, UTF8, privacy, filtering and semantic differences passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
