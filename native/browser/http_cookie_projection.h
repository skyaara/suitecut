// SPDX-License-Identifier: MIT
#pragma once
#include <arpa/inet.h>
#include <ctime>
#include <string>
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "http_cookie.h"
namespace suitecut {
inline bool HTTPLoopback(const std::string&host){if(host=="localhost"||host.ends_with(".localhost"))return true;in_addr ipv4{};if(inet_pton(AF_INET,host.c_str(),&ipv4)==1)return (ntohl(ipv4.s_addr)>>24)==127;auto address=host;if(address.size()>1&&address.front()=='['&&address.back()==']')address=address.substr(1,address.size()-2);in6_addr ipv6{};return inet_pton(AF_INET6,address.c_str(),&ipv6)==1&&IN6_IS_ADDR_LOOPBACK(&ipv6);}
// A native API request has no browser top-level partition key. Preserve all
// partitioned browser cookies in its actual profile, without flattening them
// into this unpartitioned wire request.
inline std::string HTTPJarHeader(CefRefPtr<CefListValue>cookies,const std::string&url,double now){
  CefURLParts parts;if(!cookies||!CefParseURL(url,parts))return {};const auto host=CookieLower(CefString(&parts.host).ToString()),scheme=CefString(&parts.scheme).ToString();auto path=CefString(&parts.path).ToString();if(path.empty())path="/";std::string result;
  for(size_t i=0;i<cookies->GetSize();++i){const auto cookie=cookies->GetDictionary(i);if(!cookie||cookie->HasKey("partitionKey"))continue;auto domain=CookieLower(cookie->GetString("domain").ToString());const bool scoped=domain.starts_with('.');if(scoped)domain.erase(0,1);if(domain!=host&&!(scoped&&host.size()>domain.size()&&host.ends_with("."+domain)))continue;
    const auto cookie_path=cookie->GetString("path").ToString();if(!path.starts_with(cookie_path)||!(path.size()==cookie_path.size()||cookie_path.ends_with('/')||path[cookie_path.size()]=='/'))continue;
    const auto expiry=cookie->GetType("expires")==VTYPE_INT?cookie->GetInt("expires"):cookie->GetDouble("expires");if(expiry!=-1&&expiry<=now)continue;if(cookie->GetBool("secure")&&scheme!="https"&&!HTTPLoopback(host))continue;
    const auto name=cookie->GetString("name").ToString(),value=cookie->GetString("value").ToString();if(!result.empty())result+="; ";result+=name+"="+value;
  }return result;
}
}
