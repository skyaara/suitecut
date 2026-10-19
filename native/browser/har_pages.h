// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_values.h"
namespace suitecut {
// Only actual CEF page identities, titles and Chromium lifecycle timestamps
// enter this bounded journal. Brokers never acquire a fabricated public page.
class NativeHARPages {
 public:
  struct Page {std::string id,title,loader;double wall=0,start=-1,dom=-1,load=-1;};
  bool Created(int context,const std::string&owner,const std::string&id,double wall){
    if(context<1||owner.empty()||id.empty()||owner.size()>128||id.size()>128||!std::isfinite(wall)||wall<0||live_.contains(owner)||live_.size()>=192)return false;
    Page page{id,"","",wall};if(!Budget(Cost(owner,page)))return false;
    live_.emplace(owner,Live{context,page});bytes_+=Cost(owner,page);
    const auto capture=captures_.find(context);if(capture!=captures_.end())return Add(capture->second,owner,page);
    return true;
  }
  bool Start(int context){
    if(context<1||captures_.contains(context)||captures_.size()>=16)return false;
    Capture capture;
    for(const auto&[owner,live]:live_)if(live.context==context&&!Add(capture,owner,live.page)){Release(capture);return false;}
    captures_.emplace(context,std::move(capture));return true;
  }
  void Seal(int context){const auto capture=captures_.find(context);if(capture!=captures_.end())capture->second.sealed=true;}
  void Stop(int context){const auto capture=captures_.find(context);if(capture!=captures_.end()){Release(capture->second);captures_.erase(capture);}}
  void Detach(const std::string&owner){const auto live=live_.find(owner);if(live!=live_.end()){bytes_-=Cost(owner,live->second.page);live_.erase(live);}}
  bool Title(int context,const std::string&owner,const std::string&title){
    const auto live=live_.find(owner);if(live==live_.end()||live->second.context!=context||title.size()>8192)return false;
    const auto capture=captures_.find(context);
    Page*record=nullptr;if(capture!=captures_.end()&&!capture->second.sealed){const auto found=capture->second.pages.find(owner);if(found!=capture->second.pages.end())record=&found->second;}
    const auto old=Cost(owner,live->second.page)+(record?Cost(owner,*record):0);
    const auto next=Cost(owner,live->second.page)-live->second.page.title.size()+title.size()+(record?Cost(owner,*record)-record->title.size()+title.size():0);
    if(next>old&&!Budget(next-old))return false;
    // Assigning a short string, even by move from an SSO string, can retain
    // the old allocation. Swap fresh storage so refunded bytes are released.
    std::string fresh_live(title),fresh_record(record?title:std::string{});
    live->second.page.title.swap(fresh_live);if(record)record->title.swap(fresh_record);
    bytes_=bytes_-old+next;return true;
  }
  bool Lifecycle(int context,const std::string&owner,const std::string&phase,double timestamp,double wall,const std::string&loader){
    const auto live=live_.find(owner);if(live==live_.end()||live->second.context!=context||loader.empty()||loader.size()>128||!std::isfinite(timestamp)||timestamp<0||!std::isfinite(wall)||wall<0)return false;
    const auto update=[&](Page&page,bool first){
      if(phase=="init"||phase=="request"){if((!first||page.start<0)&&(page.start<0||page.loader!=loader)){page.start=timestamp;page.wall=wall;page.loader=loader;page.dom=page.load=-1;}}
      else if(page.loader==loader&&page.start>=0&&timestamp>=page.start){if(phase=="DOMContentLoaded"&&(page.dom<0||!first))page.dom=timestamp-page.start;else if(phase=="load"&&(page.load<0||!first))page.load=timestamp-page.start;}
    };
    update(live->second.page,false);const auto capture=captures_.find(context);if(capture!=captures_.end()&&!capture->second.sealed){const auto page=capture->second.pages.find(owner);if(page!=capture->second.pages.end())update(page->second,true);}return true;
  }
  std::string Reference(int context,const std::string&owner)const{const auto capture=captures_.find(context);if(capture==captures_.end())return {};const auto page=capture->second.pages.find(owner);return page==capture->second.pages.end()?std::string{}:page->second.id;}
  template<class Date>CefRefPtr<CefListValue> Export(int context,Date date)const{
    auto pages=CefListValue::Create();const auto capture=captures_.find(context);if(capture==captures_.end())return pages;
    for(const auto&owner:capture->second.order){const auto&observed=capture->second.pages.at(owner);auto page=CefDictionaryValue::Create();page->SetString("id",observed.id);page->SetString("title",observed.title);page->SetString("startedDateTime",date(observed.wall));auto timing=CefDictionaryValue::Create();timing->SetDouble("onContentLoad",observed.dom);timing->SetDouble("onLoad",observed.load);page->SetDictionary("pageTimings",timing);pages->SetDictionary(pages->GetSize(),page);}return pages;
  }
  void Clear(){live_.clear();captures_.clear();bytes_=pages_=0;}
  size_t bytes()const{return bytes_;}
  size_t title_capacity()const{size_t count=0;for(const auto&[owner,live]:live_)count+=live.page.title.capacity();for(const auto&[context,capture]:captures_)for(const auto&[owner,page]:capture.pages)count+=page.title.capacity();return count;}
 private:
  struct Live{int context;Page page;};struct Capture{std::map<std::string,Page>pages;std::vector<std::string>order;bool sealed=false;};
  static size_t Cost(const std::string&owner,const Page&page){return owner.size()+page.id.size()+page.title.size()+256;}
  bool Budget(size_t bytes)const{return bytes<=2097152-bytes_;}
  bool Add(Capture&capture,const std::string&owner,const Page&page){if(capture.sealed)return true;if(capture.pages.contains(owner))return true;const auto cost=Cost(owner,page);if(pages_>=1024||!Budget(cost))return false;capture.pages.emplace(owner,page);capture.order.push_back(owner);++pages_;bytes_+=cost;return true;}
  void Release(Capture&capture){for(const auto&[owner,page]:capture.pages)bytes_-=Cost(owner,page);pages_-=capture.pages.size();}
  std::map<std::string,Live>live_;std::map<int,Capture>captures_;size_t bytes_=0,pages_=0;
};
}
