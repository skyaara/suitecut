// SPDX-License-Identifier: MIT
#pragma once
#include "api_cookie_jar.h"
#include "http_header_text.h"
#include "include/cef_values.h"

namespace suitecut {
// Selected native cookie fields only. A value is stored as its authoritative
// HTTP wire octets and projected through the same Latin-1 boundary as headers.
inline bool NativeAPIJarInputs(CefRefPtr<CefListValue> list,size_t maximum,
                               std::vector<NativeAPICookieJar::Input>& result,std::string& error){
  result.clear();if(!list||list->GetSize()>maximum){error="Native API cookie input entry capacity exceeded";return false;}
  std::vector<NativeAPICookieJar::Input> parsed;size_t bytes=0;
  for(size_t i=0;i<list->GetSize();++i){
    const auto value=list->GetDictionary(i);if(!value||value->GetType("name")!=VTYPE_STRING||value->GetType("value")!=VTYPE_STRING){error="Cookie name and value must be strings";return false;}
    CefDictionaryValue::KeyList keys;value->GetKeys(keys);for(const auto& key:keys){const auto field=key.ToString();if(field!="name"&&field!="value"&&field!="url"&&field!="domain"&&field!="path"&&field!="expires"&&field!="secure"&&field!="httpOnly"&&field!="sameSite"&&field!="partitionKey"&&field!="partitionKeyHasCrossSiteAncestor"){error="Unsupported native API cookie attribute: "+field;return false;}}
    NativeAPICookieJar::Input input;if(value->GetString("name").length()>4096||value->GetString("value").length()>4096){error="Cookie name and value exceed native capacity";return false;}input.name=value->GetString("name");if(!HTTPEncodeHeaderValue(value->GetString("value").ToString16(),input.value)){error="Cookie value contains an invalid HTTP byte";return false;}
    const auto string=[&](const char* name,std::optional<std::string>& output){if(!value->HasKey(name))return true;if(value->GetType(name)!=VTYPE_STRING){error="Cookie string attribute has invalid type";return false;}if(value->GetString(name).length()>8192){error="Cookie string attribute exceeds native capacity";return false;}output=value->GetString(name).ToString();bytes+=output->size();return true;};
    if(!string("url",input.url)||!string("domain",input.domain)||!string("path",input.path)||!string("partitionKey",input.partition_key))return false;
    if(value->HasKey("secure")){if(value->GetType("secure")!=VTYPE_BOOL){error="Cookie secure attribute must be boolean";return false;}input.secure=value->GetBool("secure");}
    if(value->HasKey("httpOnly")){if(value->GetType("httpOnly")!=VTYPE_BOOL){error="Cookie httpOnly attribute must be boolean";return false;}input.http_only=value->GetBool("httpOnly");}
    if(value->HasKey("partitionKeyHasCrossSiteAncestor")){if(!input.partition_key||value->GetType("partitionKeyHasCrossSiteAncestor")!=VTYPE_BOOL){error="Cookie partition ancestor flag requires a partition key and boolean value";return false;}input.partition_cross_site=value->GetBool("partitionKeyHasCrossSiteAncestor");}
    if(value->HasKey("sameSite")){const auto same=value->GetString("sameSite").ToString();if(value->GetType("sameSite")!=VTYPE_STRING||(same!="None"&&same!="Lax"&&same!="Strict")){error="Invalid cookie sameSite";return false;}input.same_site=same=="Strict"?3:same=="None"?1:2;}
    if(value->HasKey("expires")){if(value->GetType("expires")!=VTYPE_INT&&value->GetType("expires")!=VTYPE_DOUBLE){error="Cookie expires must be a finite Unix timestamp or -1";return false;}input.expires=value->GetType("expires")==VTYPE_INT?value->GetInt("expires"):value->GetDouble("expires");}
    bytes+=input.name.size()+input.value.size()+128;if(bytes>1048576){error="Native API cookie input byte capacity exceeded";return false;}parsed.push_back(std::move(input));
  }result=std::move(parsed);return true;
}
inline CefRefPtr<CefListValue> NativeAPIJarCookies(const std::vector<NativeAPICookieJar::Cookie>& cookies,bool identities=false){
  auto output=CefListValue::Create();for(const auto& cookie:cookies){auto value=CefDictionaryValue::Create();value->SetString("name",cookie.name);value->SetString("value",HTTPHeaderText(cookie.value));value->SetString("domain",cookie.domain);value->SetString("path",cookie.path);value->SetDouble("expires",cookie.expires);value->SetBool("secure",cookie.secure);value->SetBool("httpOnly",cookie.http_only);value->SetString("sameSite",cookie.same_site==3?"Strict":cookie.same_site==1?"None":"Lax");if(cookie.partition_key){value->SetString("partitionKey",*cookie.partition_key);value->SetBool("partitionKeyHasCrossSiteAncestor",cookie.partition_cross_site);}if(identities)value->SetString("nativeIdentity",std::to_string(cookie.identity));output->SetDictionary(output->GetSize(),value);}return output;
}
} // namespace suitecut
