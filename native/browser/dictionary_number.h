// SPDX-License-Identifier: MIT
#pragma once
#include <limits>
#include "include/cef_values.h"
namespace suitecut {
// CefDictionaryValue::GetDouble does not convert integer JSON values.
inline double DictionaryNumber(CefRefPtr<CefDictionaryValue> value,const char* name) {
  if(!value)return std::numeric_limits<double>::quiet_NaN();
  const auto type=value->GetType(name);
  if(type==VTYPE_INT)return value->GetInt(name);
  if(type==VTYPE_DOUBLE)return value->GetDouble(name);
  return std::numeric_limits<double>::quiet_NaN();
}
}
