// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <string>
#include "include/cef_values.h"
namespace suitecut {
// Profile-scoped Network commands and one strictly validated private V8 cookie
// filter are permitted; callers cannot submit arbitrary Runtime commands.
// Page-target Storage commands without a context ID use the default profile.
enum class CookieMethod { Get, Set, Clear, Delete, Filter };
using CookieDict=CefRefPtr<CefDictionaryValue>;
using CookieReply=std::function<void(bool,CookieDict,const std::string&)>;
using CookieChannel=std::function<void(int,int,CookieMethod,CookieDict,double,CookieReply)>;
inline const char* NativeCookieMethodName(CookieMethod method) {
  switch(method) {
    case CookieMethod::Get:return "Network.getAllCookies";
    case CookieMethod::Set:return "Network.setCookies";
    case CookieMethod::Clear:return "Network.clearBrowserCookies";
    case CookieMethod::Delete:return "Network.deleteCookies";
    case CookieMethod::Filter:return "Runtime.evaluate";
  }
  return nullptr;
}
}
