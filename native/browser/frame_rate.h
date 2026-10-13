// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <optional>
#include <string_view>

inline std::optional<int> ParseSuiteCutFrameRate(std::string_view text) {
  int fps = 0;
  const auto parsed = std::from_chars(text.data(), text.data() + text.size(), fps);
  if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size() || fps < 1 || fps > 120)
    return std::nullopt;
  return fps;
}
