// SPDX-License-Identifier: MIT
#include "suitecut/json.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

using suitecut::Json;
namespace {
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F&& work) { bool rejected = false; try { work(); } catch (const std::exception&) { rejected = true; } check(rejected, "Expected JSON rejection"); }
}
int main() {
  try {
    Json value{{"null", nullptr}, {"bool", true}, {"integer", 9223372036854775807LL},
               {"text", "literal is a string"}, {"array", Json::array{1, 2.5, false}}};
    check(value.at("text").is_string(), "Literal converted to bool");
    check(value.at("integer").as_int() == std::numeric_limits<std::int64_t>::max(), "Integer lost precision");
    check(Json::parse(value.dump()) == value, "Object roundtrip changed value");
    check(Json::parse("-9223372036854775808").as_int() == std::numeric_limits<std::int64_t>::min(), "Minimum int64 failed");
    check(!Json::parse("9223372036854775808").is_integer(), "Overflowing integer was truncated to int64");
    check(Json::parse("100000000000000000000").as_double() == 1e20, "Large JavaScript number rejected");
    check(Json::parse("-9223372036854775809").as_double() == -9223372036854775808.0, "Finite large negative number rejected");
    Json empty; empty["created"] = "field"; check(empty.at("created").as_string() == "field", "Null object promotion failed");
    check(!empty.contains("absent") && !Json(1).contains("a"), "contains changed type");
    rejects([&] { (void)value.at("absent"); }); rejects([&] { (void)value.at("integer").as_string(); });
    rejects([&] { (void)value.at("array").at(9); }); rejects([&] { (void)Json(1.0).as_int(); });
    rejects([&] { (void)Json(std::numeric_limits<std::uint64_t>::max()); });
    rejects([&] { (void)Json(std::numeric_limits<double>::infinity()); });
    rejects([&] { (void)Json(std::numeric_limits<double>::quiet_NaN()); });
    rejects([&] { (void)Json{{"same", 1}, {"same", 2}}; });
    for (const double number : {0.0, -0.0, 1.0, -2.5, 1e100, 1e-100, std::numeric_limits<double>::max(), std::numeric_limits<double>::denorm_min()}) {
      const auto roundtrip = Json::parse(Json(number).dump());
      check(roundtrip.is_number() && roundtrip.as_double() == number, "Double roundtrip changed number");
      check(std::signbit(roundtrip.as_double()) == std::signbit(number), "Negative zero lost sign");
    }
    const auto unicode = Json::parse(R"("A\u0000\u20ac\ud83d\ude00\/\b\f\n\r\t")").as_string();
    check(unicode == std::string("A\0", 2) + "\xe2\x82\xac\xf0\x9f\x98\x80/\b\f\n\r\t", "Unicode escape decoding failed");
    check(Json::parse(Json(unicode).dump()).as_string() == unicode, "Unicode string roundtrip failed");
    std::string controls; for (int i = 0; i < 32; ++i) controls += static_cast<char>(i);
    check(Json::parse(Json(controls).dump()).as_string() == controls, "Control quoting failed");
    check(Json::parse(" \n\t\r {\"a\": [null,true,false,-1,1e2]} \r").at("a").as_array().size() == 5, "JSON whitespace failed");
    for (const auto text : {"", " ", "null x", "truefalse", "[1,]", "{\"a\":1,}", "{a:1}", "{\"a\" 1}",
                           "{\"a\":1,\"a\":2}", "{\"a\":1,\"\\u0061\":2}", "[", "{", "\"", "'text'", "NaN", "Infinity",
                           "00", "01", "-01", "+1", "1.", ".1", "1e", "1e+", "--1", "1e9999", "1e-9999",
                           "\"\\x20\"", "\"\\u123\"", "\"\\uZZZZ\"",
                           "\"\\ud800\"", "\"\\udc00\"", "\"\\ud800x\"", "\"\\ud800\\u0041\"", "\"line\nfeed\""})
      rejects([&] { (void)Json::parse(text); });
    for (const std::string& invalid : {std::string("\xc0\xaf", 2), std::string("\xe0\x80\x80", 3), std::string("\xed\xa0\x80", 3),
                                     std::string("\xf0\x80\x80\x80", 4), std::string("\xf4\x90\x80\x80", 4), std::string("\xf5\x80\x80\x80", 4),
                                     std::string("\x80", 1), std::string("\xe2\x82", 2), std::string("\xe2\x28\xa1", 3)}) {
      rejects([&] { (void)Json::parse('"' + invalid + '"'); });
      rejects([&] { (void)Json(invalid).dump(); });
      rejects([&] { (void)Json(Json::object{{invalid, 1}}).dump(); });
    }
    rejects([] { (void)Json::parse("null", {3, 64, 10, 10}); });
    rejects([] { (void)Json::parse("\"long\"", {20, 64, 10, 3}); });
    rejects([] { (void)Json::parse("[1,2]", {20, 64, 2, 20}); });
    rejects([] { (void)Json::parse("[[0]]", {20, 1, 20, 20}); });
    rejects([] { (void)Json("long").dump({3, 64, 10, 10}); });
    rejects([] { (void)Json::parse("0", {20, 257, 20, 20}); });
    std::string nested(70, '['); nested += '0'; nested.append(70, ']'); rejects([&] { (void)Json::parse(nested); });
    const std::string large(1024 * 1024, 'x');
    check(Json::parse(Json(large).dump()).as_string() == large, "Large JSON string roundtrip failed");
    const std::string prefix(4096, 'a'), suffix(4096, 'z');
    const auto mixed = prefix + "\"\\\n東京" + suffix;
    const auto quoted = '"' + prefix + "\\\"\\\\\\u000a東京" + suffix + '"';
    suitecut::JsonLimits limits{quoted.size(), 64, 10, mixed.size()};
    check(Json(mixed).dump(limits) == quoted, "Mixed ASCII spans changed JSON escaping");
    check(Json::parse(quoted, limits).as_string() == mixed, "Mixed ASCII spans changed decoding");
    --limits.max_string_bytes;
    rejects([&] { (void)Json::parse(quoted, limits); });
    rejects([&] { (void)Json(mixed).dump(limits); });
    ++limits.max_string_bytes; --limits.max_bytes;
    rejects([&] { (void)Json(mixed).dump(limits); });
    std::cout << "JSON strict UTF8, escapes, exact numbers, limits and roundtrips passed\n";
    return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
