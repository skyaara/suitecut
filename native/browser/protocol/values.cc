// SPDX-License-Identifier: MIT
#include "protocol/values.h"
#include "include/cef_parser.h"
#include <random>
namespace suitecut::browser {
const auto started = Clock::now();
double Now() { return std::chrono::duration<double, std::milli>(Clock::now() - started).count(); }
Dict Header(const char* type) {
  auto value = CefDictionaryValue::Create();
  value->SetInt("version", 1);
  value->SetString("type", type);
  return value;
}
std::string Json(Dict dict) {
  auto value = CefValue::Create();
  value->SetDictionary(dict);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}
std::string Literal(const std::string& text) {
  auto value = CefValue::Create();
  value->SetString(text);
  return CefWriteJSON(value, JSON_WRITER_DEFAULT).ToString();
}
std::string HandleNamespace() {
  std::random_device random;
  const char* hex = "0123456789abcdef";
  std::string value;
  for (int word = 0; word < 4; ++word) {
    const uint32_t bits = random();
    for (int shift = 28; shift >= 0; shift -= 4) value.push_back(hex[(bits >> shift) & 15]);
  }
  return value;
}

}  // namespace suitecut::browser
