// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <string>
#include "include/cef_values.h"
namespace suitecut {
// Project only observed Chromium response metadata. Limits apply before any
// UTF-8 conversion; the resulting DTO is charged by NetworkControls' existing
// retained-response metadata budget, including redirect history.
inline bool NativeResponseString(CefRefPtr<CefDictionaryValue> value,const char* key,size_t limit) {
  return value&&value->GetType(key)==VTYPE_STRING&&value->GetString(key).length()<=limit;
}
inline bool NativeResponseNumber(CefRefPtr<CefDictionaryValue> value,const char* key,double& output) {
  if(!value)return false;
  if(value->GetType(key)==VTYPE_INT)output=value->GetInt(key);
  else if(value->GetType(key)==VTYPE_DOUBLE)output=value->GetDouble(key);
  else return false;
  return std::isfinite(output);
}
inline void NativeResponseDetails(CefRefPtr<CefDictionaryValue> source,CefRefPtr<CefDictionaryValue> output) {
  if(!source||!output)return;
  double port=0;
  if(NativeResponseString(source,"remoteIPAddress",128)&&!source->GetString("remoteIPAddress").empty()&&
     NativeResponseNumber(source,"remotePort",port)&&port>=1&&port<=65535&&std::floor(port)==port) {
    auto address=CefDictionaryValue::Create();address->SetString("ipAddress",source->GetString("remoteIPAddress"));
    address->SetInt("port",static_cast<int>(port));output->SetDictionary("serverAddr",address);
  }
  auto security=source->GetDictionary("securityDetails");double from=0,to=0;
  if(NativeResponseString(security,"issuer",8192)&&NativeResponseString(security,"subjectName",8192)&&
     NativeResponseString(security,"protocol",128)&&NativeResponseNumber(security,"validFrom",from)&&NativeResponseNumber(security,"validTo",to)) {
    auto details=CefDictionaryValue::Create();details->SetString("issuer",security->GetString("issuer"));
    details->SetString("subjectName",security->GetString("subjectName"));details->SetString("protocol",security->GetString("protocol"));
    details->SetDouble("validFrom",from);details->SetDouble("validTo",to);output->SetDictionary("securityDetails",details);
  }
}
// Every native response reply uses a fresh wire DTO. Retain raw Chromium
// dictionaries only in the internal caches; nested certificate dictionaries
// contain protocol fields outside SuiteCut's public response contract.
inline CefRefPtr<CefDictionaryValue> NativeResponseWireInfo(
    CefRefPtr<CefDictionaryValue> source,const std::string& request,
    const std::string& session,const std::string& frame,const std::string& method) {
  if(!source)return nullptr;
  auto info=CefDictionaryValue::Create();
  info->SetString("url",source->GetString("url"));
  double status=0;NativeResponseNumber(source,"status",status);
  info->SetInt("status",static_cast<int>(status));
  info->SetString("statusText",source->GetString("statusText"));
  auto headers=source->GetDictionary("headers");
  // Internal native HAR provenance; bounded SDK response DTOs strip this key.
  info->SetBool("_nativeHeadersObserved",headers!=nullptr);
  info->SetDictionary("headers",headers?headers->Copy(false):CefDictionaryValue::Create());
  info->SetString("requestId",request);info->SetString("session",session);
  info->SetString("frameId",frame);info->SetString("method",method);
  info->SetBool("fromServiceWorker",source->GetBool("fromServiceWorker"));
  if(source->GetType("protocol")==VTYPE_STRING)info->SetString("protocol",source->GetString("protocol"));
  if(source->GetType("mimeType")==VTYPE_STRING)info->SetString("mimeType",source->GetString("mimeType"));
  NativeResponseDetails(source,info);
  if(auto timing=source->GetDictionary("timing"))info->SetDictionary("timing",timing->Copy(false));
  return info;
}
}  // namespace suitecut
