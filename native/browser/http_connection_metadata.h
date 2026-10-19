// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <curl/curl.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

namespace suitecut {
struct HTTPConnectionSecurity {
  std::string issuer, protocol, subject_name;
  double valid_from=0,valid_to=0;
};
struct HTTPConnectionTiming {
  double start_time=0,domain_lookup_start=-1,domain_lookup_end=-1,connect_start=-1;
  double secure_connection_start=-1,connect_end=-1,request_start=-1,response_start=-1,response_end=-1;
};
inline double HTTPWallTime(){return std::chrono::duration<double,std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();}
inline std::string HTTPCommonName(X509_NAME* name){
  if(!name)return {};
  const int first=X509_NAME_get_index_by_NID(name,NID_commonName,-1);
  if(first<0)return {};
  auto entry=X509_NAME_get_entry(name,first);auto value=entry?X509_NAME_ENTRY_get_data(entry):nullptr;
  unsigned char* utf8=nullptr;const int length=value?ASN1_STRING_to_UTF8(&utf8,value):-1;
  if(length<0)return {};
  const std::string result(reinterpret_cast<const char*>(utf8),static_cast<size_t>(length));OPENSSL_free(utf8);return result;
}
inline std::optional<double> HTTPASN1Time(const ASN1_TIME* value){
  std::tm parsed{};if(!value||ASN1_TIME_to_tm(value,&parsed)!=1)return std::nullopt;
  const auto result=timegm(&parsed);if(result==-1)return std::nullopt;return static_cast<double>(result);
}
inline std::optional<HTTPConnectionSecurity> CaptureHTTPSecurity(CURL* handle){
  curl_tlssessioninfo* session=nullptr;
  if(curl_easy_getinfo(handle,CURLINFO_TLS_SSL_PTR,&session)!=CURLE_OK||!session||session->backend!=CURLSSLBACKEND_OPENSSL||!session->internals)return std::nullopt;
  auto ssl=static_cast<SSL*>(session->internals);auto certificate=SSL_get1_peer_certificate(ssl);if(!certificate)return std::nullopt;
  HTTPConnectionSecurity result;result.protocol=SSL_get_version(ssl);result.issuer=HTTPCommonName(X509_get_issuer_name(certificate));result.subject_name=HTTPCommonName(X509_get_subject_name(certificate));
  const auto from=HTTPASN1Time(X509_get0_notBefore(certificate)),to=HTTPASN1Time(X509_get0_notAfter(certificate));X509_free(certificate);
  if(!from||!to)return std::nullopt;
  result.valid_from=*from;result.valid_to=*to;return result;
}
inline double HTTPCurlTime(CURL* handle,CURLINFO field){curl_off_t value=0;return curl_easy_getinfo(handle,field,&value)==CURLE_OK?static_cast<double>(value)/1000.0:-1;}
inline HTTPConnectionTiming CaptureHTTPTiming(CURL* handle,double start,bool secure){
  HTTPConnectionTiming result;result.start_time=start;long connections=0;curl_easy_getinfo(handle,CURLINFO_NUM_CONNECTS,&connections);
  const double ready=HTTPCurlTime(handle,CURLINFO_PRETRANSFER_TIME_T);
  if(connections>0){result.domain_lookup_start=0;result.domain_lookup_end=HTTPCurlTime(handle,CURLINFO_NAMELOOKUP_TIME_T);result.connect_start=result.domain_lookup_end;result.secure_connection_start=secure?HTTPCurlTime(handle,CURLINFO_CONNECT_TIME_T):-1;result.connect_end=ready;}
  // libcurl's protocol-ready boundary is the actual connection handoff to the
  // request sender, and represents both connection readiness and request start.
  result.request_start=ready;result.response_start=HTTPCurlTime(handle,CURLINFO_STARTTRANSFER_TIME_T);result.response_end=HTTPCurlTime(handle,CURLINFO_TOTAL_TIME_T);return result;
}
}
