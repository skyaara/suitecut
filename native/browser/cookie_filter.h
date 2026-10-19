// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include "include/cef_parser.h"
namespace suitecut {
using CookieFilterDict=CefRefPtr<CefDictionaryValue>;
// CEF's JSON parser rejects isolated UTF16 escapes which are legal in a JS
// RegExp source. Validate only this dedicated encoded string lexically; V8
// later decodes its original exact code units in the private policy realm.
inline bool CookiePatternJSONString(const std::string&value){
  if(value.size()<2||value.front()!='"'||value.back()!='"')return false;
  for(size_t i=1;i+1<value.size();++i){const unsigned char c=value[i];if(c<32||c=='"')return false;if(c!='\\')continue;if(++i+1>=value.size())return false;const char escaped=value[i];if(escaped=='u'){for(int n=0;n<4;++n){if(++i+1>=value.size())return false;const char digit=value[i];if(!((digit>='0'&&digit<='9')||(digit>='a'&&digit<='f')||(digit>='A'&&digit<='F')))return false;}}else if(std::string("\"\\/bfnrt").find(escaped)==std::string::npos)return false;}
  return true;
}
// Only a selected cookie-filter pattern is interpreted. Cookie values and
// arbitrary caller dictionaries never become an executable expression.
inline bool ValidateCookieFilter(CookieFilterDict filter,bool&regex,std::string&error){
  error.clear();regex=false;if(!filter)return true;
  CefDictionaryValue::KeyList keys;filter->GetKeys(keys);
  for(const auto&key:keys){
    const auto field=key.ToString();if(field!="name"&&field!="domain"&&field!="path"){error="Unknown native cookie filter field";return false;}
    if(filter->GetType(key)==VTYPE_STRING){if(filter->GetString(key).length()>8192){error="Native cookie filter string capacity exceeded";return false;}continue;}
    auto pattern=filter->GetDictionary(key);if(!pattern||pattern->GetSize()!=2||pattern->GetType("sourceJSON")!=VTYPE_STRING||pattern->GetType("flags")!=VTYPE_STRING){error="Native cookie filter must be a string or regular expression";return false;}
    const auto source=pattern->GetString("sourceJSON").ToString(),flags=pattern->GetString("flags").ToString();
    if(source.size()>32768||flags.size()>8){error="Native cookie regular expression capacity exceeded";return false;}
    if(!CookiePatternJSONString(source)){error="Invalid native cookie regular expression source";return false;}
    std::string seen;for(const char flag:flags){if(std::string("dgimsuvy").find(flag)==std::string::npos||seen.find(flag)!=std::string::npos){error="Invalid native cookie regular expression flags";return false;}seen+=flag;}
    if(flags.find('u')!=std::string::npos&&flags.find('v')!=std::string::npos){error="Incompatible native cookie regular expression flags";return false;}regex=true;
  }return true;
}
inline bool ValidateCookieFilterParameters(CookieFilterDict params,const std::string& action,bool&regex,std::string&error){
  if(!params||(params->HasKey("filter")&&params->GetType("filter")!=VTYPE_DICTIONARY)){error="Invalid native cookie filter";return false;}
  if(!ValidateCookieFilter(params->GetDictionary("filter"),regex,error))return false;
  if(regex&&action!="clearCookies"){error="Regular expression filters are supported for clearCookies only";return false;}
  return true;
}
inline bool BuildCookieFilterEvaluation(CookieFilterDict input,double deadline,double now,CookieFilterDict&output,std::string&error){
  error.clear();if(!input||!std::isfinite(now)||!std::isfinite(deadline)||deadline<0||(deadline&&now>=deadline)){error="Native cookie original deadline expired before filtering";return false;}
  bool regex=false;const auto filter=input->GetDictionary("filter");const auto cookies=input->GetList("cookies");
  if(!ValidateCookieFilter(filter,regex,error)||!regex||!cookies||cookies->GetSize()>4096){if(error.empty())error="Invalid native cookie regular expression input";return false;}
  auto payload=CefDictionaryValue::Create();payload->SetDictionary("filter",filter->Copy(false));auto fields=CefListValue::Create();
  for(size_t i=0;i<cookies->GetSize();++i){const auto cookie=cookies->GetDictionary(i);if(!cookie){error="Invalid native cookie filter record";return false;}auto item=CefDictionaryValue::Create();for(const auto*field:{"name","domain","path"}){if(cookie->GetType(field)!=VTYPE_STRING){error="Invalid native cookie filter field value";return false;}item->SetString(field,cookie->GetString(field));}fields->SetDictionary(i,item);}
  payload->SetList("cookies",fields);auto value=CefValue::Create();value->SetDictionary(payload);const auto json=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString();if(json.size()>1048576){error="Native cookie filter input byte capacity exceeded";return false;}
  // This runs only in the genuine context-private broker realm. V8 owns RegExp
  // syntax/flags/UTF16 semantics and its real execution timeout bounds backtracking.
  const auto expression=R"JS(((input)=>{const patterns=Object.entries(input.filter).map(([field,value])=>[field,typeof value==='string'?value:new RegExp(JSON.parse(value.sourceJSON),value.flags)]);const matches=[];for(let i=0;i<input.cookies.length;++i){const cookie=input.cookies[i];if(patterns.every(([field,value])=>{if(typeof value==='string')return cookie[field]===value;value.lastIndex=0;return value.test(cookie[field]);}))matches.push(i);}return {matches};})( )JS"+json+")";
  output=CefDictionaryValue::Create();output->SetString("expression",expression);output->SetBool("returnByValue",true);output->SetBool("awaitPromise",false);output->SetDouble("timeout",deadline?std::min(30000.0,deadline-now):30000.0);return true;
}
}
