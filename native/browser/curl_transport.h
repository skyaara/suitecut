// SPDX-License-Identifier: MIT
#pragma once
#include <curl/curl.h>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include "http_connection_metadata.h"
#include "http_lookup_controls.h"

namespace suitecut {
// UI-thread nonblocking HTTP transport. The embedder owns deadlines, body
// budgets and the browser cookie jar; this class owns actual libcurl handles.
class NativeHTTPExtraCA;
class NativeCurlTransport {
 public:
  using Headers=std::vector<std::pair<std::string,std::string>>;
  struct Request {
    std::string url,method,username,password;
    std::vector<std::string> cookies;
    Headers headers;std::span<const uint8_t> body;
    bool credentials=false,preemptive_credentials=false,preemptive_proxy_credentials=false,ignore_https_errors=false;
    std::string proxy_server,proxy_bypass,proxy_username,proxy_password;
    std::shared_ptr<const NativeHTTPExtraCA> extra_ca;
    struct Lookup {std::string hostname;int port=0;std::vector<HTTPLookupControls::Address> addresses;};
    std::optional<Lookup> lookup;
    // Independent API contexts own their real cookie store. These callbacks
    // never cancel curl synchronously from inside its native callbacks.
    bool authoritative_cookies=false;
    std::function<bool(Headers&,std::string&)> refresh_cookie_header;
    std::function<bool(std::span<const std::string>,std::string&)> receive_cookies;
    // The owning HTTP operation supplies its original generation/deadline.
    // This check never resets that deadline or releases physical ownership.
    std::function<bool()> checkpoint;
  };
  struct Response {int status=0;std::string url,method,status_text,error;Headers headers;std::vector<std::string> set_cookies;CURLcode error_code=CURLE_OK;long os_errno=0;std::string remote_ip;int remote_port=0;std::optional<HTTPConnectionSecurity> security;HTTPConnectionTiming timing;};
  // A null data pointer marks a new response (e.g. authentication retry).
  using Data=std::function<bool(const void*,size_t)>;
  using Done=std::function<void(Response)>;
  NativeCurlTransport();
  ~NativeCurlTransport();
  bool Start(int owner,Request request,Data data,Done done,std::string& error);
  void Cancel(int owner);
  void Tick();
  void Clear();
  void ResetIdle();
  size_t size() const {return active_.size();}
 private:
  struct Transfer;
  static CURLcode ExtraTrust(CURL*,void*,void*);
  static int Progress(void*,curl_off_t,curl_off_t,curl_off_t,curl_off_t);
  static int Observe(CURL*,curl_infotype,char*,size_t,void*);
  static size_t Header(char* bytes,size_t size,size_t count,void* state);
  static size_t Body(char* bytes,size_t size,size_t count,void* state);
  void Finish(CURL* handle,CURLcode result);
  CURLM* multi_=nullptr;bool initialized_=false;
  std::map<int,std::shared_ptr<Transfer>> active_;
  std::map<CURL*,int> handles_;
};
}  // namespace suitecut
