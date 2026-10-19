// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <charconv>
#include <cctype>
#include <ctime>
#include <limits>
#include <optional>
#include <string>
#include <vector>
#include <curl/curl.h>
#include "http_cookie_date.h"
namespace suitecut {
struct NativeHTTPCookie {std::string name,value,domain,path;bool secure=false,http_only=false;int same_site=0;std::optional<double> expires;};
inline std::string CookieTrim(std::string value) {
  const auto first=value.find_first_not_of(" \t"),last=value.find_last_not_of(" \t");return first==std::string::npos?"":value.substr(first,last-first+1);
}
inline std::string CookieLower(std::string value){for(auto& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return value;}
// Raw response cookie attributes are parsed into the real CEF cookie manager;
// response headers always remain the actual received fields, never jar output.
inline std::optional<NativeHTTPCookie> ParseNativeHTTPCookie(const std::string& raw,const std::string& host,const std::string& request_path,double now) {
  if(raw.empty() || raw.size()>4096 || raw.find_first_of("\r\n")!=std::string::npos || raw.find('\0')!=std::string::npos)return std::nullopt;
  const auto end=raw.find(';'),equal=raw.find('=');if(equal==std::string::npos || (end!=std::string::npos && equal>end))return std::nullopt;
  NativeHTTPCookie cookie;cookie.name=CookieTrim(raw.substr(0,equal));cookie.value=CookieTrim(raw.substr(equal+1,end==std::string::npos?std::string::npos:end-equal-1));
  if(cookie.name.empty())return std::nullopt;
  for(const unsigned char c:cookie.name)if(c<=32 || c>=127 || std::string("()<>@,;:\\\"/[]?={}").find(static_cast<char>(c))!=std::string::npos)return std::nullopt;
  for(const unsigned char c:cookie.value)if(c<32 || c==127)return std::nullopt;
  const auto slash=request_path.rfind('/');cookie.path=slash==std::string::npos || slash==0?"/":request_path.substr(0,slash);
  std::optional<double> maximum_age;
  size_t cursor=end;
  while(cursor!=std::string::npos) {
    const auto next=raw.find(';',cursor+1);auto attribute=CookieTrim(raw.substr(cursor+1,next==std::string::npos?std::string::npos:next-cursor-1));
    const auto separator=attribute.find('=');const auto name=CookieLower(CookieTrim(attribute.substr(0,separator)));
    const auto value=separator==std::string::npos?"":CookieTrim(attribute.substr(separator+1));
    if(name=="secure")cookie.secure=true;
    else if(name=="httponly")cookie.http_only=true;
    else if(name=="domain") {
      auto domain=CookieLower(value);while(!domain.empty() && domain.front()=='.')domain.erase(domain.begin());
      const auto normalized=CookieLower(host);
      if(domain.empty() || (domain!=normalized && !(normalized.size()>domain.size() && normalized.ends_with("."+domain))))return std::nullopt;
      cookie.domain=std::move(domain);
    } else if(name=="path" && !value.empty() && value.front()=='/')cookie.path=value;
    else if(name=="samesite") {const auto site=CookieLower(value);cookie.same_site=site=="strict"?3:site=="lax"?2:site=="none"?1:0;}
    else if(name=="expires") {if(const auto time=HTTPCookieDate(value))cookie.expires=*time;}
    else if(name=="max-age" && !value.empty()) {
      int64_t seconds=0;const auto parsed=std::from_chars(value.data(),value.data()+value.size(),seconds);
      if(parsed.ptr==value.data()+value.size() && parsed.ec==std::errc{})maximum_age=seconds<=0?now-1:std::min(253402300799.0,now+static_cast<double>(seconds));
      else if(parsed.ec==std::errc::result_out_of_range && parsed.ptr==value.data()+value.size())maximum_age=value.front()=='-'?now-1:253402300799.0;
    }
    cursor=next;
  }
  if(maximum_age)cookie.expires=maximum_age;
  if(cookie.name.starts_with("__Secure-") && !cookie.secure)return std::nullopt;
  if(cookie.name.starts_with("__Host-") && (!cookie.secure || !cookie.domain.empty() || cookie.path!="/"))return std::nullopt;
  return cookie;
}
}  // namespace suitecut
