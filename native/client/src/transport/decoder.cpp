// SPDX-License-Identifier: MIT
#include "decoder.hpp"
#include "suitecut/json.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace suitecut {
namespace {
constexpr std::size_t kMediaLimit = 3840 * 2160 * 4;
constexpr std::size_t kResponseLimit = 192 * 1024 * 1024;
constexpr std::string_view kTypes[] = {"ready","response","error","frame","audio","actionstage","binding","console","context","contextconsole","contextnetwork","contextnetworkbody","contextroute","contextroutemutation","contextworker","contextworkerpredicate","dialog","download","event","eventpredicate","filechooser","httplookup","navigation","network","networkbody","pagecrash","pageerror","pagerecovered","recording","streaming","urlpredicate","websocket","websocketroute","worker","workwatermark"};
} // namespace
PacketDecoder::PacketDecoder(std::size_t max_header_bytes, std::size_t max_payload_bytes)
    : max_header_(max_header_bytes), max_payload_(max_payload_bytes) {
  if (max_header_ < 2 || max_header_ > 65536 || max_payload_ > kResponseLimit)
    throw std::invalid_argument("Invalid native packet decoder limits");
}
void PacketDecoder::read_header() {
  const auto value = Json::parse(packet_.header, {max_header_, 32, 16384, max_header_});
  if (!value.is_object() || !value.at("version").is_integer() || value.at("version").as_int() != 1 ||
      !value.at("type").is_string() || !value.at("bytes").is_integer())
    throw std::runtime_error("Invalid native packet header");
  const auto& type = value.at("type").as_string();
  if (std::find(std::begin(kTypes), std::end(kTypes), type) == std::end(kTypes))
    throw std::runtime_error("Unknown native packet type");
  const auto length = value.at("bytes").as_int();
  const auto limit = std::min(max_payload_, type == "response" ? kResponseLimit : kMediaLimit);
  if (length < 0 || static_cast<std::uint64_t>(length) > limit)
    throw std::runtime_error("Native packet payload exceeds limit");
  if (value.contains("pageId") && (!value.at("pageId").is_integer() || value.at("pageId").as_int() < 1 || value.at("pageId").as_int() > std::numeric_limits<int>::max()))
    throw std::runtime_error("Invalid native packet page ID");
  if (type == "response" && (!value.at("id").is_integer() || value.at("id").as_int() < 1 || value.at("id").as_int() > std::numeric_limits<int>::max() || !value.at("ok").is_bool()))
    throw std::runtime_error("Invalid native response identity");
  if ((type == "ready" || type == "error" || type == "pagecrash" || type == "pagerecovered" || type == "navigation" || type == "event" || type == "actionstage" || type == "urlpredicate" || type == "workwatermark") && length != 0)
    throw std::runtime_error("Unexpected native control payload");
  if (type == "frame") {
    const auto width = value.at("width").as_int(), height = value.at("height").as_int();
    const auto format = value.contains("pixelFormat") ? value.at("pixelFormat").as_string() : "bgra";
    if (width < 1 || width > 3840 || height < 1 || height > 2160 ||
        (format != "bgra" && format != "i420") || (format == "i420" && (width % 2 || height % 2)) ||
        length != (format == "bgra" ? width * height * 4 : width * height * 3 / 2))
      throw std::runtime_error("Invalid native frame geometry");
  }
  if (type == "audio") {
    const auto frames = value.at("frames").as_int();
    if (frames < 1 || frames > 4096 || length != frames * 4) throw std::runtime_error("Invalid native PCM payload");
  }
  payload_size_ = static_cast<std::size_t>(length);
}
void PacketDecoder::emit(const std::function<void(Packet&&)>& callback) {
  Packet complete = std::move(packet_);
  packet_ = Packet{};
  prefix_size_ = header_size_ = payload_size_ = 0;
  callback(std::move(complete));
}
void PacketDecoder::feed(std::span<const std::uint8_t> bytes, const std::function<void(Packet&&)>& callback) {
  if (failed_ || finished_ || feeding_) throw std::runtime_error("Native decoder is closed, failed or reentrant");
  if (!callback) throw std::invalid_argument("Native decoder requires a packet callback");
  feeding_ = true;
  try {
    while (!bytes.empty()) {
      if (prefix_size_ < 4) {
        const auto take = std::min(4 - prefix_size_, bytes.size());
        std::copy_n(bytes.begin(), take, prefix_.begin() + static_cast<std::ptrdiff_t>(prefix_size_));
        prefix_size_ += take; bytes = bytes.subspan(take);
        if (prefix_size_ != 4) continue;
        header_size_ = std::uint32_t(prefix_[0]) | (std::uint32_t(prefix_[1]) << 8) | (std::uint32_t(prefix_[2]) << 16) | (std::uint32_t(prefix_[3]) << 24);
        if (header_size_ < 2 || header_size_ > max_header_) throw std::runtime_error("Invalid native packet header length");
        packet_.header.reserve(header_size_);
      }
      if (packet_.header.size() < header_size_) {
        const auto take = std::min(header_size_ - packet_.header.size(), bytes.size());
        packet_.header.append(reinterpret_cast<const char*>(bytes.data()), take);
        bytes = bytes.subspan(take);
        if (packet_.header.size() != header_size_) continue;
        read_header();
        if (!payload_size_) { emit(callback); continue; }
      }
      const auto take = std::min(payload_size_ - packet_.payload.size(), bytes.size());
      const auto required = packet_.payload.size() + take;
      if (required > packet_.payload.capacity()) {
        const auto grown = std::max({required, std::size_t(4096), packet_.payload.capacity() * 2});
        packet_.payload.reserve(std::min(payload_size_, grown));
      }
      packet_.payload.insert(packet_.payload.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(take));
      bytes = bytes.subspan(take);
      if (packet_.payload.size() == payload_size_) emit(callback);
    }
    feeding_ = false;
  } catch (...) {
    feeding_ = false; failed_ = true; packet_ = Packet{};
    throw;
  }
}
void PacketDecoder::finish() {
  if (failed_ || finished_ || feeding_) throw std::runtime_error("Native decoder is closed, failed or reentrant");
  if (prefix_size_ || header_size_ || !packet_.header.empty() || !packet_.payload.empty()) {
    failed_ = true; packet_ = Packet{};
    throw std::runtime_error("Truncated native packet stream");
  }
  finished_ = true;
}
} // namespace suitecut
