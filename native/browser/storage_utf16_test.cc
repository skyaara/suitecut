// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "storage_utf16.h"
int main() {
  using suitecut::StorageJSONValidationProjection;
  assert(StorageJSONValidationProjection(R"({"value":"\ud800"})") == R"({"value":"\ue000"})");
  assert(StorageJSONValidationProjection(R"({"value":"\udfff"})") == R"({"value":"\ue7ff"})");
  assert(StorageJSONValidationProjection(R"({"value":"\uD83D\uDE00"})") == R"({"value":"\uD83D\uDE00"})");
  assert(StorageJSONValidationProjection(R"({"value":"\\ud800"})") == R"({"value":"\\ud800"})");
  assert(StorageJSONValidationProjection(R"({"value":"quote\" and \ud800 then \\udc00"})") == R"({"value":"quote\" and \ue000 then \\udc00"})");
  assert(StorageJSONValidationProjection(R"({"value":"é 😀 \u1234"})") == R"({"value":"é 😀 \u1234"})");
  assert(StorageJSONValidationProjection(R"({"value":"\ud800\ud801\udc00\udfff"})") == R"({"value":"\ue000\ud801\udc00\ue7ff"})");
  assert(StorageJSONValidationProjection(R"({"value":"\uxxxx"})") == R"({"value":"\uxxxx"})");
  const std::string valid = R"({"localStorage":[{"name":"key","value":"literal \\ud800 and pair \ud800\udc00"}]})";
  for (int index = 0; index < 100000; ++index) assert(StorageJSONValidationProjection(valid) == valid);
  std::cout << "UTF-16 native validation projection: 8 edge cases +100000 ownership cycles passed\n";
}
