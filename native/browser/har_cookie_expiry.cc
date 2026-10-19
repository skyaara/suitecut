// SPDX-License-Identifier: MIT
#include "har_cookie_expiry.h"

namespace suitecut {
void NativeHARCookieExpiry::Attribute(std::string_view key,std::string_view value) {
    if(key=="max-age"){
      size_t first=value.starts_with('-')?1:0;
      if(first==value.size())return;
      for(size_t i=first;i<value.size();++i)if(value[i]<'0'||value[i]>'9')return;
      // Syntactically valid overflow still has precedence over Expires. It
      // cannot become an invented representable expiry or an invalid fallback.
      max_age_present_=true;max_age_.reset();int64_t seconds=0;
      const auto parsed=std::from_chars(value.data(),value.data()+value.size(),seconds);
      if(parsed.ec==std::errc{}&&parsed.ptr==value.data()+value.size())max_age_=seconds;
    }else if(key=="expires"){
      if(const auto parsed=ParseUTC(value))absolute_=parsed;
    }
  }

std::optional<std::string> NativeHARCookieExpiry::Resolve(std::optional<double> receipt_wall_ms)const {
    if(!max_age_present_)return absolute_?Format(*absolute_):std::nullopt;
    if(!max_age_||!receipt_wall_ms||!ValidTime(*receipt_wall_ms))return std::nullopt;
    const long double result=static_cast<long double>(*receipt_wall_ms)+static_cast<long double>(*max_age_)*1000;
    if(result<Minimum()||result>Maximum())return std::nullopt;
    return Format(static_cast<double>(result));
  }

std::optional<double> NativeHARCookieExpiry::ParseUTC(std::string_view value) {
    std::optional<std::string> legacy;
    if(value.size()!=29){legacy=LegacyUTC(value);if(!legacy)return std::nullopt;value=*legacy;}
    if(value.size()!=29||value[3]!=','||value[4]!=' '||value[7]!=' '||value[11]!=' '||value[16]!=' '||value[19]!=':'||value[22]!=':'||value[25]!=' '||!ASCIIEqual(value.substr(26),"gmt"))return std::nullopt;
    const auto number=[&](size_t first,size_t count)->std::optional<unsigned>{unsigned result=0;for(size_t i=first;i<first+count;++i){if(value[i]<'0'||value[i]>'9')return std::nullopt;result=result*10+unsigned(value[i]-'0');}return result;};
    const auto day=number(5,2),year=number(12,4),hour=number(17,2),minute=number(20,2),second=number(23,2);
    if(!day||!year||!hour||!minute||!second||*year<1601||*year>9999||*hour>23||*minute>59||*second>59)return std::nullopt;
    constexpr std::string_view months[]={"jan","feb","mar","apr","may","jun","jul","aug","sep","oct","nov","dec"};
    unsigned month=0;for(unsigned i=0;i<12;++i)if(ASCIIEqual(value.substr(8,3),months[i]))month=i+1;
    const std::chrono::year_month_day date{std::chrono::year{int(*year)},std::chrono::month{month},std::chrono::day{*day}};
    if(!date.ok())return std::nullopt;
    constexpr std::string_view weekdays[]={"sun","mon","tue","wed","thu","fri","sat"};
    const std::chrono::sys_days days{date};
    if(!ASCIIEqual(value.substr(0,3),weekdays[std::chrono::weekday{days}.c_encoding()]))return std::nullopt;
    return double((days.time_since_epoch().count()*86400+*hour*3600+*minute*60+*second)*1000);
  }

std::optional<std::string> NativeHARCookieExpiry::Format(double milliseconds) {
    if(!ValidTime(milliseconds))return std::nullopt;
    const std::chrono::sys_time<std::chrono::milliseconds> stamp{std::chrono::milliseconds{static_cast<int64_t>(std::floor(milliseconds))}};
    const auto days=std::chrono::floor<std::chrono::days>(stamp);
    const std::chrono::year_month_day date{days};const std::chrono::hh_mm_ss time{stamp-days};
    char text[32];const auto count=std::snprintf(text,sizeof(text),"%04d-%02u-%02uT%02lld:%02lld:%02lld.%03lldZ",int(date.year()),unsigned(date.month()),unsigned(date.day()),static_cast<long long>(time.hours().count()),static_cast<long long>(time.minutes().count()),static_cast<long long>(time.seconds().count()),static_cast<long long>(time.subseconds().count()));
    return count==24?std::optional<std::string>{std::string(text,24)}:std::nullopt;
  }

bool NativeHARCookieExpiry::ValidTime(double milliseconds){return std::isfinite(milliseconds)&&milliseconds>=Minimum()&&milliseconds<=Maximum();}

std::optional<std::string> NativeHARCookieExpiry::LegacyUTC(std::string_view value) {
    // Normalization is bounded to one29-byte complete date; the existing strict
    // calendar/time/weekday checks below remain the common final authority.
    if(value.size()==24&&value[3]==' '&&value[7]==' '&&value[10]==' '&&value[19]==' '){
      std::string day(value.substr(8,2));if(day[0]==' ')day[0]='0';
      return std::string(value.substr(0,3))+", "+day+" "+std::string(value.substr(4,3))+" "+std::string(value.substr(20,4))+" "+std::string(value.substr(11,8))+" GMT";
    }
    if(value.size()<30||value.size()>33)return std::nullopt;
    const auto comma=value.find(',');
    if(comma==std::string_view::npos||comma<6||comma>9||value.size()!=comma+24||value[comma+1]!=' ')return std::nullopt;
    constexpr std::string_view full[]={"sunday","monday","tuesday","wednesday","thursday","friday","saturday"};
    constexpr std::string_view short_name[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    size_t weekday=7;for(size_t i=0;i<7;++i)if(ASCIIEqual(value.substr(0,comma),full[i]))weekday=i;
    const auto tail=value.substr(comma+2);
    if(weekday==7||tail[2]!='-'||tail[6]!='-'||tail[9]!=' '||tail[18]!=' '||!ASCIIEqual(tail.substr(19),"gmt")||tail[7]<'0'||tail[7]>'9'||tail[8]<'0'||tail[8]>'9')return std::nullopt;
    const unsigned year=unsigned(tail[7]-'0')*10+unsigned(tail[8]-'0');
    return std::string(short_name[weekday])+", "+std::string(tail.substr(0,2))+" "+std::string(tail.substr(3,3))+" "+std::to_string(year+(year>=70?1900:2000))+" "+std::string(tail.substr(10,8))+" GMT";
  }

bool NativeHARCookieExpiry::ASCIIEqual(std::string_view value,std::string_view lower){if(value.size()!=lower.size())return false;for(size_t i=0;i<value.size();++i){const auto c=static_cast<unsigned char>(value[i]);if((c>='A'&&c<='Z'?c-'A'+'a':c)!=static_cast<unsigned char>(lower[i]))return false;}return true;}
}
