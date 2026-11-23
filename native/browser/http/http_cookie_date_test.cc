// SPDX-License-Identifier: MIT
#include "http/http_cookie.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <iostream>
int main() {
  const auto unixSentinel = suitecut::HTTPCookieDate("Wed, 31 Dec 1969 23:59:59 GMT");
  assert(unixSentinel && *unixSentinel <= 0);
  const auto expiredSentinel = suitecut::ParseNativeHTTPCookie(
      "a=v; expires=Wed, 31 Dec 1969 23:59:59 GMT", "example.test", "/", 1700000000);
  assert(expiredSentinel && expiredSentinel->expires && *expiredSentinel->expires <= 0);
  const auto ancient = suitecut::ParseNativeHTTPCookie(
      "a=v; expires=1 Jan 1000 00:00:00 +0000 (UTC)", "example.test", "/", 1700000000);
  assert(ancient && ancient->expires && *ancient->expires == -30610224000.0);
  assert(suitecut::HTTPCookieDate("Thu, 01 Jan 1601 00:00:00 GMT") == -11644473600.0);
  assert(suitecut::HTTPCookieDate("Wed, 01 Jan 2025 00:00:00 GMT") == 1735689600.0);
  assert(!suitecut::HTTPCookieDate("29 Feb 1000 00:00:00 GMT"));
  assert(suitecut::HTTPCookieDate("29 Feb 1200 00:00:00 GMT"));
  for (const auto* invalid :
       {"31 Feb 1000 00:00:00 GMT", "1 Jan 1000 24:00:00 GMT", "1 Jan 1000 00:60:00 GMT",
        "1 Jan 1000 00:00:60 GMT", "1 Jan 1000", "nonsense"})
    assert(!suitecut::HTTPCookieDate(invalid));
  const auto session = suitecut::ParseNativeHTTPCookie("a=v; expires=31 Feb 1000 00:00:00 GMT",
                                                       "example.test", "/", 1000);
  assert(session && !session->expires);
  const auto override = suitecut::ParseNativeHTTPCookie(
      "a=v; max-age=3600; expires=1 Jan 1000 00:00:00 GMT", "example.test", "/", 1000);
  assert(override && override->expires == 4600);
  const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int year = 1000; year <= 1600; year += 17)
    for (int month = 1; month <= 12; ++month)
      for (int day = 1; day <= 28; day += 9) {
        // std::chrono's proleptic Gregorian calendar; macOS timegm rejects years before 1901.
        using namespace std::chrono;
        const sys_days date = std::chrono::year{year} / month / day;
        const auto independent =
            (date + hours{17} + minutes{42} + seconds{5}).time_since_epoch().count();
        const auto text = std::to_string(day) + " " + months[month - 1] + " " +
                          std::to_string(year) + " 17:42:05 GMT";
        assert(suitecut::HTTPCookieDate(text) == static_cast<double>(independent));
      }
  {
    const auto long_line = suitecut::ParseNativeHTTPCookie(
        "big=" + std::string(8000, 'v') + "; Path=/" + std::string(4000, 'p'), "example.test", "/",
        1000);
    assert(long_line && long_line->value.size() == 8000 && long_line->path.size() == 4001);
  }
  std::cout << "Native API cookie dates: valid year1000 expires instead of becoming session; "
               "Gregorian leap/day/time validation and Max-Age precedence passed\n";
}
