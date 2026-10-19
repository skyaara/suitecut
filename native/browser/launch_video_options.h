// SPDX-License-Identifier: MIT
#pragma once
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include <charconv>
#include <string>

namespace suitecut {
// Native owns filesystem preparation and encoding. This only decodes the bounded
// private launch configuration; NativeContextVideo performs semantic validation.
inline bool ParseLaunchVideoOptions(const std::string& json,
                                    const std::string& timeout,
                                    CefRefPtr<CefDictionaryValue>& options,
                                    int& timeout_ms,std::string& error) {
  if(json.empty()||json.size()>32768){error="Invalid native launch recordVideo configuration";return false;}
  const auto value=CefParseJSON(json,JSON_PARSER_RFC);
  options=value?value->GetDictionary():nullptr;
  if(!options){error="Native launch recordVideo requires an object";return false;}
  const auto parsed=std::from_chars(timeout.data(),timeout.data()+timeout.size(),timeout_ms);
  if(parsed.ec!=std::errc{}||parsed.ptr!=timeout.data()+timeout.size()||timeout_ms<100||timeout_ms>120000){error="Invalid native launch recording startup timeout";return false;}
  return true;
}
}
