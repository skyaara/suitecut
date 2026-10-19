// SPDX-License-Identifier: MIT
#pragma once
#include "http_transport_error.h"
#include "include/cef_values.h"
namespace suitecut {
inline CefRefPtr<CefDictionaryValue> HTTPTransportErrorMetadata(CURLcode curl_code,long os_errno){
  const auto observation=HTTPTransportErrorObservation(curl_code,os_errno);if(!observation)return nullptr;
  auto output=CefDictionaryValue::Create();output->SetString("type","native-http-transport");
  output->SetInt("curlCode",observation->curl_code);output->SetInt("osErrno",observation->os_errno);
  if(!observation->code.empty())output->SetString("code",observation->code);
  return output;
}
} // namespace suitecut
