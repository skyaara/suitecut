// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace suitecut {
// Projects received attributes. This does not read or mutate a cookie jar, and
// never chooses a clock. The caller supplies the original native header receipt.
class NativeHARCookieExpiry {
 public:
  void Attribute(std::string_view key,std::string_view value);
  std::optional<std::string> Resolve(std::optional<double> receipt_wall_ms)const;
  // Complete RFC1123, RFC850 and asctime UTC forms only. Legacy two-digit
  // cookie years use the fixed RFC6265 pivot (70), never a current-clock guess.
  // Other timezone, partial-date and leap-second forms remain in raw headers.
  static std::optional<double> ParseUTC(std::string_view value);
  static std::optional<std::string> Format(double milliseconds);
  static bool ValidTime(double milliseconds);
 private:
  static std::optional<std::string> LegacyUTC(std::string_view value);
  static constexpr double Minimum(){return -11644473600000.;} // 1601-01-01 UTC.
  static constexpr double Maximum(){return 253402300799999.;} // 9999-12-31 UTC.
  static bool ASCIIEqual(std::string_view value,std::string_view lower);
  bool max_age_present_=false;
  std::optional<int64_t> max_age_;
  std::optional<double> absolute_;
};
}
