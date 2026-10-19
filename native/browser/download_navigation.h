// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace suitecut {
// CEF's IO response callback precedes navigation abort delivery. Keep its real
// classification visible to the UI without relying on task ordering or timers.
// These request identities classify navigation only; they never assign bytes
// or files to a CEF DownloadItem.
class DownloadNavigationRequests {
 public:
  enum class Classification { pending, document, download };
  static bool ResponseDownloads(bool native_download,bool navigation,const std::string& disposition,const std::string& mime){
    if(native_download)return true;
    if(!navigation)return false;
    auto token=disposition.substr(0,disposition.find(';'));
    const auto begin=token.find_first_not_of(" \t");
    if(begin==std::string::npos)token.clear();
    else token=token.substr(begin,token.find_last_not_of(" \t")-begin+1);
    std::transform(token.begin(),token.end(),token.begin(),[](unsigned char value){return static_cast<char>(std::tolower(value));});
    auto type=mime;std::transform(type.begin(),type.end(),type.begin(),[](unsigned char value){return static_cast<char>(std::tolower(value));});
    return token=="attachment"||type=="application/octet-stream";
  }
  struct Entry {
    std::uint64_t generation=0,native_id=0;
    int page=0;
    bool main=false,terminal=false;
    std::string frame,method;
    std::vector<std::string> urls;
    Classification classification=Classification::pending;
  };
  struct Match {
    bool present=false,ambiguous=false,terminal=false;
    Classification classification=Classification::pending;
    std::uint64_t generation=0,native_id=0;
  };
  static constexpr size_t kCapacity=512,kRedirectCapacity=32;
  std::shared_ptr<Entry> Begin(int page,std::uint64_t native_id,const std::string& frame,bool main,const std::string& method,const std::string& url){
    std::lock_guard lock(mutex_);
    if(entries_.size()>=kCapacity){
      const auto old=std::find_if(entries_.begin(),entries_.end(),[](const auto& entry){return entry.second->terminal;});
      if(old==entries_.end())return nullptr;
      entries_.erase(old);
    }
    if(next_==UINT64_MAX||frame.size()>128||method.size()>32)return nullptr;
    auto entry=std::make_shared<Entry>();entry->generation=++next_;entry->native_id=native_id;entry->page=page;entry->frame=frame;entry->main=main;entry->method=method;
    AddURL(*entry,url);entries_[entry->generation]=entry;return entry;
  }
  void Redirect(const std::shared_ptr<Entry>& entry,const std::string& url){
    if(!entry)return;
    std::lock_guard lock(mutex_);AddURL(*entry,url);
  }
  void Response(const std::shared_ptr<Entry>& entry,const std::string& url,bool download){
    if(!entry)return;
    std::lock_guard lock(mutex_);AddURL(*entry,url);entry->classification=download?Classification::download:Classification::document;
  }
  void End(const std::shared_ptr<Entry>& entry){
    if(!entry)return;
    std::lock_guard lock(mutex_);entry->terminal=true;
    if(entry->classification==Classification::pending)entry->classification=Classification::document;
  }
  std::uint64_t Sequence()const{std::lock_guard lock(mutex_);return next_;}
  Match Lookup(int page,bool main,const std::string& url,std::uint64_t after)const{
    std::lock_guard lock(mutex_);std::shared_ptr<Entry> latest;std::set<std::string> frames;
    for(const auto& [generation,entry]:entries_){
      if(generation<=after||entry->page!=page||entry->main!=main||std::find(entry->urls.begin(),entry->urls.end(),url)==entry->urls.end())continue;
      frames.insert(entry->frame);latest=entry;
    }
    if(!latest)return {};
    return {true,!main&&frames.size()>1,latest->terminal,latest->classification,latest->generation,latest->native_id};
  }
  void PageClosed(int page){std::lock_guard lock(mutex_);std::erase_if(entries_,[page](const auto& entry){return entry.second->page==page;});}
  void Clear(){std::lock_guard lock(mutex_);entries_.clear();}
  size_t Size()const{std::lock_guard lock(mutex_);return entries_.size();}
 private:
  static void AddURL(Entry& entry,const std::string& url){
    if(url.empty()||url.size()>8192||entry.urls.size()>=kRedirectCapacity||std::find(entry.urls.begin(),entry.urls.end(),url)!=entry.urls.end())return;
    entry.urls.push_back(url);
  }
  mutable std::mutex mutex_;
  std::map<std::uint64_t,std::shared_ptr<Entry>> entries_;
  std::uint64_t next_=0;
};
} // namespace suitecut
