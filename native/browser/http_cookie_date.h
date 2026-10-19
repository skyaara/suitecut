// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <curl/curl.h>
namespace suitecut {
// libcurl rejects some otherwise parseable historical years. API cookie expiration still
// needs to distinguish a valid ancient date (delete) from no expiration
// (session). Historical dates are never submitted to Chromium's cookie store.
inline std::optional<double> HTTPCookieDate(const std::string& value) {
  const auto actual=curl_getdate(value.c_str(),nullptr);
  if(actual!=-1)return static_cast<double>(actual);
  if(value.size()>1024)return std::nullopt;
  int day=-1,month=-1,year=-1,hour=-1,minute=-1,second=-1;
  auto digit=[](char c){return c>='0'&&c<='9';};
  auto number=[&](std::string_view token,size_t minimum,size_t maximum)->std::optional<int>{
    if(token.size()<minimum||token.size()>maximum)return std::nullopt;
    int result=0;for(char c:token){if(!digit(c))return std::nullopt;result=result*10+(c-'0');}return result;
  };
  auto delimiter=[](unsigned char c){return c==9||(c>=0x20&&c<=0x2f)||(c>=0x3b&&c<=0x40)||(c>=0x5b&&c<=0x60)||(c>=0x7b&&c<=0x7e);};
  size_t cursor=0,tokens=0;
  while(cursor<value.size()){
    while(cursor<value.size()&&delimiter(static_cast<unsigned char>(value[cursor])))++cursor;
    if(cursor==value.size())break;
    const auto begin=cursor;while(cursor<value.size()&&!delimiter(static_cast<unsigned char>(value[cursor])))++cursor;
    if(++tokens>64)return std::nullopt;
    const std::string_view token(value.data()+begin,cursor-begin);
    if(hour<0){const auto first=token.find(':'),last=token.rfind(':');if(first!=std::string_view::npos&&first!=last){const auto h=number(token.substr(0,first),1,2),m=number(token.substr(first+1,last-first-1),1,2),s=number(token.substr(last+1),1,2);if(h&&m&&s){hour=*h;minute=*m;second=*s;continue;}}}
    if(day<0){if(const auto candidate=number(token,1,2)){day=*candidate;continue;}}
    if(month<0&&token.size()==3){char normalized[3];for(size_t i=0;i<3;++i)normalized[i]=static_cast<char>(std::tolower(static_cast<unsigned char>(token[i])));constexpr std::string_view months="janfebmaraprmayjunjulaugsepoctnovdec";const auto index=months.find(std::string_view(normalized,3));if(index!=std::string_view::npos&&index%3==0){month=static_cast<int>(index/3)+1;continue;}}
    if(year<0){if(const auto candidate=number(token,2,4)){year=*candidate;continue;}}
  }
  if(year>=0&&year<=69)year+=2000;else if(year>=70&&year<=99)year+=1900;
  if(year<1||year>=1601||month<1||day<1||hour<0||hour>23||minute<0||minute>59||second<0||second>59)return std::nullopt;
  const std::chrono::year_month_day calendar{std::chrono::year(year),std::chrono::month(static_cast<unsigned>(month)),std::chrono::day(static_cast<unsigned>(day))};
  if(!calendar.ok())return std::nullopt;
  const auto date=std::chrono::sys_days(calendar).time_since_epoch()+std::chrono::hours(hour)+std::chrono::minutes(minute)+std::chrono::seconds(second);
  return std::chrono::duration<double>(date).count();
}
} // namespace suitecut
