// SPDX-License-Identifier: MIT
// WebDriver BiDi RemoteValue decoding: no browser process or network needed.
#include "bidi_backend.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
using suitecut::Json;
using suitecut::detail::bidi_remote_value;
namespace {
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class F>
void rejects(F&& work) {
  bool rejected = false;
  try {
    work();
  } catch (const std::exception&) {
    rejected = true;
  }
  check(rejected, "Expected BiDi value rejection");
}
// Assign rather than brace-initialize: an initializer list would copy the subtree at every level.
Json remote(const char* type, Json value) {
  Json result{{"type", type}};
  result["value"] = std::move(value);
  return result;
}
}  // namespace
int main() {
  try {
    const auto value = bidi_remote_value(remote(
        "object",
        Json::array{Json::array{"list", remote("array", Json::array{remote("number", 1),
                                                                    remote("string", "two"),
                                                                    Json{{"type", "null"}},
                                                                    Json{{"type", "undefined"}}})},
                    Json::array{remote("string", "flag"), remote("boolean", true)},
                    Json::array{"set", remote("set", Json::array{remote("number", 2.5)})}}));
    check(value == Json::parse(R"({"list":[1,"two",null,null],"flag":true,"set":[2.5]})"),
          "Shallow RemoteValue decoding changed");
    check(bidi_remote_value(remote(
              "map",
              Json::array{Json::array{remote("string", "k"), remote("array", Json::array{})}})) ==
              Json::parse(R"({"k":[]})"),
          "Map decoding changed");
    rejects([] { (void)bidi_remote_value(remote("number", "NaN")); });
    rejects([] {
      (void)bidi_remote_value(
          remote("array", Json::array{remote("number", 1), Json{{"type", "symbol"}}}));
    });
    rejects([] {
      (void)bidi_remote_value(
          remote("object", Json::array{Json::array{"a", remote("number", 1)},
                                       Json::array{remote("string", "a"), remote("number", 2)}}));
    });
    rejects([] {
      (void)bidi_remote_value(
          remote("map", Json::array{Json::array{remote("number", 1), remote("number", 2)}}));
    });
    // A million nested arrays and objects decode without recursion: the page, not the stack,
    // chooses the depth.
    constexpr std::size_t levels = 1000000;
    Json deep = remote("number", 7), objects = remote("string", "leaf");
    for (std::size_t i = 0; i < levels; ++i) {
      Json::array item;
      item.push_back(std::move(deep));
      deep = remote("array", Json(std::move(item)));
      Json::array pair{Json("k")};
      pair.push_back(std::move(objects));
      Json::array pairs;
      pairs.push_back(Json(std::move(pair)));
      objects = remote("object", Json(std::move(pairs)));
    }
    std::string expected(levels, '[');
    expected += '7';
    expected.append(levels, ']');
    check(bidi_remote_value(deep).dump() == expected, "Million-level BiDi array did not decode");
    std::string expected_objects;
    for (std::size_t i = 0; i < levels; ++i) expected_objects += "{\"k\":";
    expected_objects += "\"leaf\"";
    expected_objects.append(levels, '}');
    check(bidi_remote_value(objects).dump() == expected_objects,
          "Million-level BiDi object did not decode");
    std::cout << "BiDi RemoteValue decoding, rejection and million-level nesting passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
