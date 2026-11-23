// SPDX-License-Identifier: MIT
#pragma once
#include <cerrno>
#include <curl/curl.h>
#include <limits>
#include <optional>
#include <string>
namespace suitecut {
struct HTTPTransportError {
  int curl_code=0,os_errno=0;
  std::string code;
};
// errno is an actual CURLINFO_OS_ERRNO observation, not a classification made
// from an origin-controlled message or an inferred socket syscall.
inline std::optional<HTTPTransportError> HTTPTransportErrorObservation(CURLcode curl_code,long os_errno) {
  if(curl_code<=CURLE_OK||curl_code>=CURL_LAST||os_errno<0||os_errno>std::numeric_limits<int>::max())return std::nullopt;
  HTTPTransportError result{int(curl_code),int(os_errno),{}};
  // libcurl can retain an earlier connection attempt's errno after a later TLS
  // or protocol error. Only these actual socket-failure codes attribute it.
  if(curl_code!=CURLE_COULDNT_CONNECT&&curl_code!=CURLE_SEND_ERROR&&curl_code!=CURLE_RECV_ERROR)return result;
  switch(os_errno){
    case ECONNRESET:result.code="ECONNRESET";break;
    case ECONNREFUSED:result.code="ECONNREFUSED";break;
    case EPIPE:result.code="EPIPE";break;
    case ETIMEDOUT:result.code="ETIMEDOUT";break;
    case ENETUNREACH:result.code="ENETUNREACH";break;
    case EHOSTUNREACH:result.code="EHOSTUNREACH";break;
    case EADDRNOTAVAIL:result.code="EADDRNOTAVAIL";break;
    case ECONNABORTED:result.code="ECONNABORTED";break;
    default:break;
  }
  return result;
}
} // namespace suitecut
