// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

namespace suitecut::detail {
namespace credential_origin_detail {
[[noreturn]] inline void invalid() {
  throw std::invalid_argument("Credential login URL has an unsupported or invalid HTTP origin");
}
inline std::string lowercase(std::string_view input) {
  std::string result(input);
  for (auto& c:result) if(c>='A'&&c<='Z') c=static_cast<char>(c-'A'+'a');
  return result;
}
inline bool decimal(std::string_view input) {
  if(input.empty()) return false;
  for(auto c:input) if(c<'0'||c>'9') return false;
  return true;
}
inline std::string ipv6(std::string_view host) {
  std::array<unsigned char,16> address{};
  const std::string text(host);
#if defined(_WIN32)
  if(InetPtonA(AF_INET6,text.c_str(),address.data())!=1) invalid();
#else
  if(::inet_pton(AF_INET6,text.c_str(),address.data())!=1) invalid();
#endif
  std::array<std::uint16_t,8> words{};
  for(std::size_t i=0;i<words.size();++i)
    words[i]=static_cast<std::uint16_t>((address[2*i]<<8)|address[2*i+1]);
  std::size_t best_start=words.size(),best_size=1;
  for(std::size_t i=0;i<words.size();) {
    if(words[i]) {++i;continue;}
    const auto start=i;
    while(i<words.size()&&!words[i]) ++i;
    if(i-start>best_size) {best_start=start;best_size=i-start;}
  }
  // URL origins serialize IPv4-mapped IPv6 as hexadecimal pieces as well.
  std::string result;
  for(std::size_t i=0;i<words.size();) {
    if(i==best_start) {result+="::";i+=best_size;continue;}
    if(!result.empty()&&result.back()!=':') result+=':';
    std::array<char,4> digits{};
    const auto converted=std::to_chars(digits.data(),digits.data()+digits.size(),words[i],16);
    if(converted.ec!=std::errc{}) invalid();
    result.append(digits.data(),converted.ptr);++i;
  }
  return "["+result+"]";
}
inline std::string dns_or_ipv4(std::string_view raw) {
  auto host=lowercase(raw);
  if(host.empty()||host.size()>254) invalid();
  const bool trailing_dot=host.back()=='.';
  const std::string_view labels_text(host.data(),host.size()-(trailing_dot?1:0));
  if(labels_text.empty()||labels_text.size()>253) invalid();
  std::vector<std::string_view> labels;
  std::size_t offset=0;
  for(;;) {
    const auto end=labels_text.find('.',offset);
    auto label=labels_text.substr(offset,end==std::string_view::npos?end:end-offset);
    if(label.empty()||label.size()>63||label.front()=='-'||label.back()=='-') invalid();
    for(auto c:label) if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')) invalid();
    labels.push_back(label);
    if(end==std::string_view::npos) break;
    offset=end+1;
  }
  // Browsers reinterpret numeric-ending hosts as IPv4, including historical
  // octal/hex/short forms. Accept canonical dotted decimal only.
  if(decimal(labels.back())||labels.back().starts_with("0x")) {
    if(labels.size()!=4||trailing_dot) invalid();
    for(const auto label:labels) {
      if(!decimal(label)||label.size()>3||(label.size()>1&&label.front()=='0')) invalid();
      unsigned value=0;
      for(auto c:label)value=value*10+static_cast<unsigned>(c-'0');
      if(value>255) invalid();
    }
  }
  return host;
}
} // namespace credential_origin_detail

// A conservative host-side origin parser for credential login. DNS hosts must
// be ASCII (international names may be supplied as punycode), IPv4 must be
// canonical dotted decimal, and IPv6 must be bracketed. Unsupported forms fail
// without including the URL or its credential-bearing components in errors.
inline std::string strict_origin(const std::string& url) {
  using namespace credential_origin_detail;
  if(url.empty()||url.size()>8192) invalid();
  for(const unsigned char c:url) if(c<=0x20||c==0x7f||c=='\\') invalid();
  const auto colon=url.find(':');
  if(colon==std::string::npos) invalid();
  const auto scheme=lowercase(std::string_view(url).substr(0,colon));
  if((scheme!="http"&&scheme!="https")||url.compare(colon,3,"://")!=0) invalid();
  const auto start=colon+3;
  const auto end=url.find_first_of("/?#",start);
  const auto authority=std::string_view(url).substr(start,end==std::string::npos?end:end-start);
  if(authority.empty()) invalid();
  for(const unsigned char c:authority) if(c>=0x80||c=='@'||c=='%') invalid();
  std::string host;
  std::string_view port;
  bool explicit_port=false;
  if(authority.front()=='[') {
    const auto close=authority.find(']');
    if(close==std::string_view::npos||close==1) invalid();
    host=ipv6(authority.substr(1,close-1));
    if(close+1<authority.size()) {
      if(authority[close+1]!=':') invalid();
      port=authority.substr(close+2);explicit_port=true;
    }
  } else {
    const auto split=authority.find(':');
    if(split!=std::string_view::npos) {
      if(authority.find(':',split+1)!=std::string_view::npos) invalid();
      port=authority.substr(split+1);explicit_port=true;
    }
    host=dns_or_ipv4(authority.substr(0,split));
  }
  std::string result=scheme+"://"+host;
  if(explicit_port) {
    if(!decimal(port)) invalid();
    unsigned number=0;
    for(auto c:port) {number=number*10+static_cast<unsigned>(c-'0');if(number>65535)invalid();}
    if(number!=(scheme=="http"?80u:443u)) result+=':'+std::to_string(number);
  }
  return result;
}
} // namespace suitecut::detail
