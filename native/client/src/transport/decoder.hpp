// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/transport.hpp"
#include "suitecut/json.hpp"
#include <array>
#include <cmath>
#include <optional>
#include <functional>
#include <span>

namespace suitecut {
// A wire byte count or offset: a non-negative integer, or an integral double
// up to 2^53 (the engine sends counts past INT_MAX as doubles).
inline std::optional<std::uint64_t> wire_count(const Json& value) {
  if (value.is_integer()) {
    if (value.as_int() < 0) return std::nullopt;
    return static_cast<std::uint64_t>(value.as_int());
  }
  if (!value.is_number()) return std::nullopt;
  const double number = value.as_double();
  if (!std::isfinite(number) || number < 0 || number > 9007199254740992.0 || std::floor(number) != number) return std::nullopt;
  return static_cast<std::uint64_t>(number);
}
// Serialized reader-thread decoder. Callback owns each completed packet. An
// error poisons the decoder; no input, EOF or callbacks are accepted afterward.
// Optional caller limits; 0 (the default) means no limit beyond the 4-byte
// header length field. Payload lengths are whatever the header declares.
class PacketDecoder {
 public:
  explicit PacketDecoder(std::size_t max_header_bytes = 0, std::size_t max_payload_bytes = 0);
  void feed(std::span<const std::uint8_t> bytes, const std::function<void(Packet&&)>& callback);
  void finish();
 private:
  std::size_t max_header_, max_payload_;
  std::array<std::uint8_t, 4> prefix_{};
  std::size_t prefix_size_ = 0, header_size_ = 0, payload_size_ = 0;
  Packet packet_;
  bool failed_ = false, finished_ = false, feeding_ = false;
  void read_header();
  void emit(const std::function<void(Packet&&)>& callback);
};
} // namespace suitecut
