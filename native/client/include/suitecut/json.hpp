// SPDX-License-Identifier: MIT
#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace suitecut {
struct JsonLimits {
  std::size_t max_bytes = 192 * 1024 * 1024;
  std::size_t max_depth = 64;
  std::size_t max_nodes = 1024 * 1024;
  std::size_t max_string_bytes = 192 * 1024 * 1024;
};
// Small, owning JSON value. Integers remain exact; accessors never coerce types.
// Parsing rejects duplicate keys, invalid UTF-8, unpaired surrogates and numbers
// outside finite-double range. Integer literals outside int64 become doubles;
// as with JavaScript numbers, those large values can lose integer precision.
// Object order is deterministic on dump.
class Json {
 public:
  using array = std::vector<Json>;
  using object = std::map<std::string, Json, std::less<>>;
  Json() noexcept = default;
  Json(std::nullptr_t) noexcept {}
  Json(bool value) : value_(value) {}
  template<std::integral T> requires (!std::same_as<T, bool>)
  Json(T value) {
    if (!std::in_range<std::int64_t>(value)) throw std::out_of_range("JSON integer exceeds int64 range");
    value_ = static_cast<std::int64_t>(value);
  }
  Json(double value);
  Json(const char* value);
  Json(std::string value) : value_(std::move(value)) {}
  Json(std::string_view value) : value_(std::string(value)) {}
  Json(array value) : value_(std::move(value)) {}
  Json(object value) : value_(std::move(value)) {}
  Json(std::initializer_list<object::value_type> values);
  static Json parse(std::string_view input, JsonLimits limits = {});
  std::string dump(JsonLimits limits = {}) const;
  bool is_null() const noexcept { return std::holds_alternative<std::monostate>(value_); }
  bool is_bool() const noexcept { return std::holds_alternative<bool>(value_); }
  bool is_integer() const noexcept { return std::holds_alternative<std::int64_t>(value_); }
  bool is_number() const noexcept { return is_integer() || std::holds_alternative<double>(value_); }
  bool is_string() const noexcept { return std::holds_alternative<std::string>(value_); }
  bool is_array() const noexcept { return std::holds_alternative<array>(value_); }
  bool is_object() const noexcept { return std::holds_alternative<object>(value_); }
  bool as_bool() const;
  std::int64_t as_int() const;
  double as_double() const;
  const std::string& as_string() const;
  std::string& as_string();
  const array& as_array() const;
  array& as_array();
  const object& as_object() const;
  object& as_object();
  bool contains(std::string_view key) const noexcept;
  const Json& at(std::string_view key) const;
  Json& at(std::string_view key);
  const Json& at(std::size_t index) const;
  Json& at(std::size_t index);
  Json& operator[](std::string_view key);
  const Json& operator[](std::string_view key) const { return at(key); }
  bool operator==(const Json&) const = default;
 private:
  std::variant<std::monostate, bool, std::int64_t, double, std::string, array, object> value_;
};
} // namespace suitecut
