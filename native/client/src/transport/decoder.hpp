// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/transport.hpp"
#include <array>
#include <functional>
#include <span>

namespace suitecut {
// Serialized reader-thread decoder. Callback owns each completed packet. An
// error poisons the decoder; no input, EOF or callbacks are accepted afterward.
class PacketDecoder {
 public:
  explicit PacketDecoder(std::size_t max_header_bytes = 65536,
                         std::size_t max_payload_bytes = 192 * 1024 * 1024);
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
