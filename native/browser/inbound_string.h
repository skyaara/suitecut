// SPDX-License-Identifier: MIT
#pragma once
#include "devtools_utf16.h"

// Explicit protocol fields only. A JSON string inside an ordinary outer JSON
// string survives CEF parsing without permitting private-tag author-object decode.
namespace suitecut {
inline bool InboundString(CefRefPtr<CefDictionaryValue> value,const std::string& key,
                          std::string& output,size_t max_units=65536){
  if(!value)return false;
  const auto encoded=key+"JSON";
  if(value->HasKey(key)==value->HasKey(encoded))return false;
  if(value->HasKey(encoded)){
    if(value->GetType(encoded)!=VTYPE_STRING)return false;
    const auto token=value->GetString(encoded).ToString();std::u16string units;
    if(token.size()>max_units*6+2||!DevToolsStringUnits(token,units)||units.size()>max_units)return false;
    output=DevToolsUnitsWTF8(units);return true;
  }
  if(value->GetType(key)!=VTYPE_STRING)return false;
  const auto text=value->GetString(key);if(text.length()>max_units)return false;
  output=text.ToString();return true;
}
inline std::string InboundString(CefRefPtr<CefDictionaryValue> value,const std::string& key){std::string out;InboundString(value,key,out);return out;}
inline std::string InboundQuote(const std::string& value){
  auto dict=CefDictionaryValue::Create();DevToolsSetString(dict,"v",value);auto scalar=dict->GetValue("v");std::string out;
  return DevToolsLosslessJSON(scalar,out,1048576)?out:std::string();
}
inline std::string InboundDisplay(const std::string& text){
  auto dict=CefDictionaryValue::Create();DevToolsSetString(dict,"v",text);std::u16string units;
  return DevToolsReadTag(dict->GetValue("v"),units)?DevToolsQuoteUnits(units):text;
}
inline void InboundSetString(CefRefPtr<CefDictionaryValue> value,const std::string& key,const std::string& text){
  auto dict=CefDictionaryValue::Create();DevToolsSetString(dict,"v",text);std::u16string units;
  if(DevToolsReadTag(dict->GetValue("v"),units))value->SetString(key+"JSON",DevToolsQuoteUnits(units));else value->SetString(key,text);
}
inline void InboundCopyString(CefRefPtr<CefDictionaryValue> from,CefRefPtr<CefDictionaryValue> to,const std::string& key){
  for(const auto& field:{key,key+"JSON"})if(from->HasKey(field))to->SetValue(field,from->GetValue(field)->Copy());
}
}
