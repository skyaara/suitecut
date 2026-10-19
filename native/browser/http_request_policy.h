// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include <vector>
#include "curl_transport.h"
#include "http_extra_ca.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
namespace suitecut {
struct NativeHTTPRequestPolicy {
  using Dict=CefRefPtr<CefDictionaryValue>;
  struct Credential{std::string username,password,origin;bool always=false;};
  std::string user_agent,proxy_server,proxy_bypass,proxy_username,proxy_password;
  std::vector<Credential> credentials;
  std::shared_ptr<const NativeHTTPExtraCA> extra_ca;
  bool ignore_https_errors=false;
  static std::string Origin(const std::string&url){CefURLParts parts;if(!CefParseURL(url,parts))return {};const auto scheme=CefString(&parts.scheme).ToString(),host=CefString(&parts.host).ToString(),port=CefString(&parts.port).ToString();if((scheme!="http"&&scheme!="https")||host.empty())return {};return scheme+"://"+host+":"+(port.empty()?(scheme=="https"?"443":"80"):port);}
  static bool Text(Dict input,const char*name,std::string&out,size_t limit,std::string&error){if(!input->HasKey(name))return true;if(input->GetType(name)!=VTYPE_STRING){error=std::string("Native API ")+name+" must be a string";return false;}out=input->GetString(name).ToString();if(out.size()>limit||out.find('\0')!=std::string::npos){error=std::string("Invalid native API ")+name;return false;}return true;}
  static bool Parse(Dict input,NativeHTTPRequestPolicy&value,std::string&error){
    if(!input){error="Native API policy must be a dictionary";return false;}
    auto parsed=value;
    if(input->HasKey("ignoreHTTPSErrors")){if(input->GetType("ignoreHTTPSErrors")!=VTYPE_BOOL){error="Native API ignoreHTTPSErrors must be boolean";return false;}parsed.ignore_https_errors=input->GetBool("ignoreHTTPSErrors");}
    if(!Text(input,"userAgent",parsed.user_agent,8192,error))return false;
    if(input->HasKey("extraCACertificates")){
      const auto list=input->GetList("extraCACertificates");
      if(!list||list->GetSize()>NativeHTTPExtraCA::kMaxCertificates){error="Native API extraCACertificates must be an array of at most16 PEM strings";return false;}
      std::vector<std::string> certificates;size_t bytes=0;
      for(size_t i=0;i<list->GetSize();++i){
        if(list->GetType(i)!=VTYPE_STRING){error="Native API extraCACertificates requires PEM strings";return false;}
        // Inspect UTF16 length before converting attacker-controlled data.
        const auto text=list->GetString(i);if(text.length()>NativeHTTPExtraCA::kMaxInputBytes){error="Native API extra CA input exceeds 32 KiB";return false;}
        auto pem=text.ToString();if(pem.size()>NativeHTTPExtraCA::kMaxInputBytes-bytes){error="Native API extra CA input exceeds 32 KiB";return false;}
        bytes+=pem.size();certificates.push_back(std::move(pem));
      }
      if(certificates.empty())parsed.extra_ca.reset();
      else{parsed.extra_ca=NativeHTTPExtraCA::Parse(certificates,error);if(!parsed.extra_ca)return false;}
    }
    if(input->HasKey("proxy")){
      // An explicit proxy is a complete replacement. Never forward an inherited
      // proxy's credentials or bypass policy to a different server.
      parsed.proxy_server.clear();parsed.proxy_bypass.clear();parsed.proxy_username.clear();parsed.proxy_password.clear();
      const auto proxy=input->GetDictionary("proxy");if(!proxy||proxy->GetType("server")!=VTYPE_STRING){error="Native API proxy requires a server";return false;}
      if(!Text(proxy,"server",parsed.proxy_server,4096,error)||!Text(proxy,"bypass",parsed.proxy_bypass,4096,error)||!Text(proxy,"username",parsed.proxy_username,8192,error)||!Text(proxy,"password",parsed.proxy_password,8192,error))return false;
      if(parsed.proxy_server.find("://")==std::string::npos)parsed.proxy_server="http://"+parsed.proxy_server;
      auto url=curl_url();char*scheme=nullptr;char*host=nullptr;const bool valid=url&&curl_url_set(url,CURLUPART_URL,parsed.proxy_server.c_str(),CURLU_NON_SUPPORT_SCHEME)==CURLUE_OK&&curl_url_get(url,CURLUPART_SCHEME,&scheme,0)==CURLUE_OK&&curl_url_get(url,CURLUPART_HOST,&host,0)==CURLUE_OK&&host&&*host;
      const std::string protocol=scheme?scheme:"";curl_free(scheme);curl_free(host);if(url)curl_url_cleanup(url);
      if(!valid||(protocol!="http"&&protocol!="https"&&protocol!="socks4"&&protocol!="socks5"&&protocol!="socks5h")||parsed.proxy_server.find_first_of("\r\n")!=std::string::npos||parsed.proxy_bypass.find_first_of("\r\n")!=std::string::npos){error="Invalid native API proxy";return false;}
    }
    if(input->HasKey("credentials")||input->HasKey("username")){
      std::vector<Credential> records;size_t bytes=0;
      const auto add=[&](Dict entry){if(!entry||entry->GetType("username")!=VTYPE_STRING||entry->GetType("password")!=VTYPE_STRING){error="Native API credentials require username and password";return false;}Credential record;if(!Text(entry,"username",record.username,8192,error)||!Text(entry,"password",record.password,8192,error)||!Text(entry,"origin",record.origin,8192,error))return false;
        if(!record.origin.empty()){record.origin=Origin(record.origin);if(record.origin.empty()){error="Native API credential origin must be HTTP(S)";return false;}}
        if(entry->HasKey("send")){const auto send=entry->GetString("send").ToString();if(entry->GetType("send")!=VTYPE_STRING||(send!="always"&&send!="unauthorized")){error="Invalid native API credential send mode";return false;}record.always=send=="always";}
        bytes+=record.username.size()+record.password.size()+record.origin.size();if(records.size()>=64||bytes>65536){error="Native API credential capacity exceeded";return false;}records.push_back(std::move(record));return true;
      };
      if(input->HasKey("credentials")){const auto list=input->GetList("credentials");if(!list||list->GetSize()>64){error="Native API credentials must be an array of at most64 entries";return false;}for(size_t i=0;i<list->GetSize();++i)if(!add(list->GetDictionary(i)))return false;}
      else if(!add(input))return false;
      parsed.credentials=std::move(records);
    }
    value=std::move(parsed);return true;
  }
  void Apply(NativeCurlTransport::Request&request)const{
    request.extra_ca=extra_ca;request.ignore_https_errors=ignore_https_errors;request.proxy_server=proxy_server;request.proxy_bypass=proxy_bypass;request.proxy_username=proxy_username;request.proxy_password=proxy_password;
    const auto origin=Origin(request.url);const Credential*chosen=nullptr;for(const auto&record:credentials)if(record.origin==origin){chosen=&record;break;}if(!chosen)for(const auto&record:credentials)if(record.origin.empty()){chosen=&record;break;}
    if(chosen){request.credentials=true;request.preemptive_credentials=chosen->always;request.username=chosen->username;request.password=chosen->password;}
  }
};
}
