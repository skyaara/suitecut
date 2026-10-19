// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <poll.h>
#include <span>
#include <string_view>
#include <sys/uio.h>

#include "accepted_packet_receipt.h"

namespace suitecut {
// The writer owns each segment until this returns. Scatter/gather preserves the
// wire format without concatenating payloads or polling before writable output.
// Only actual kernel writes advance the publication receipt; partial writes and
// cancellation retain the same physical byte accounting as individual writes.
inline bool WriteNativePacket(int output, std::string_view header,
                              std::span<const uint8_t> payload,
                              const std::atomic<bool>& stopping,
                              AcceptedPacketReceipt& receipt) {
  const auto size = static_cast<uint32_t>(header.size());
  std::array<uint8_t, 4> prefix{uint8_t(size), uint8_t(size >> 8),
                              uint8_t(size >> 16), uint8_t(size >> 24)};
  std::array<iovec, 3> segments{{
      {prefix.data(), prefix.size()},
      {const_cast<char*>(header.data()), header.size()},
      {const_cast<uint8_t*>(payload.data()), payload.size()},
  }};
  size_t first = 0;
  while (first < segments.size()) {
    if (segments[first].iov_len == 0) { ++first; continue; }
    if (stopping.load(std::memory_order_relaxed)) break;
    const auto written = writev(output, segments.data() + first,
                                static_cast<int>(segments.size() - first));
    if (written < 0) {
      if (errno == EINTR) continue;
      if (errno != EAGAIN && errno != EWOULDBLOCK) break;
      pollfd fd{output, POLLOUT, 0};
      const auto ready = poll(&fd, 1, 100);
      if (ready < 0 && errno != EINTR) break;
      if (ready > 0 && (fd.revents & (POLLERR | POLLHUP | POLLNVAL))) break;
      continue;
    }
    if (written == 0) break;
    auto remaining = static_cast<size_t>(written);
    receipt.Wrote(remaining);
    while (first < segments.size() && remaining >= segments[first].iov_len) {
      remaining -= segments[first].iov_len;
      ++first;
    }
    if (first < segments.size() && remaining) {
      auto& segment = segments[first];
      segment.iov_base = static_cast<uint8_t*>(segment.iov_base) + remaining;
      segment.iov_len -= remaining;
    }
  }
  if (first != segments.size()) receipt.Failed();
  return first == segments.size();
}
}  // namespace suitecut
