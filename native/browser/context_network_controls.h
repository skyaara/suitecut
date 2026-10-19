// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>
#include "include/cef_parser.h"

namespace suitecut {
// A native context journal contains genuine per-Source network metadata. Page
// creation announcements do not govern observation; initial popup requests can
// be journaled before its public page handshake.
class NativeContextNetworkControls {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  using Emit=std::function<void(Dict)>;
  explicit NativeContextNetworkControls(Emit emit):emit_(std::move(emit)){}
  bool Observe(int context,int page,Dict network) {
    if(context<1 || page<1 || !network || !Allowed(network->GetString("event").ToString()) || sequence_==9007199254740991ULL)return false;
    auto record=CefDictionaryValue::Create();record->SetInt("contextId",context);record->SetInt("pageId",page);record->SetDouble("sequence",static_cast<double>(sequence_+1));record->SetDictionary("network",network->Copy(false));
    return Append(record);
  }
  bool ObserveWorker(int context,Dict worker,Dict network) {
    if(context<1||!worker||!network||!Allowed(network->GetString("event").ToString())||sequence_==9007199254740991ULL)return false;
    const auto kind=worker->GetString("type").ToString();
    if(worker->GetType("id")!=VTYPE_STRING||worker->GetString("id").empty()||worker->GetString("id").length()>128||worker->GetType("url")!=VTYPE_STRING||worker->GetString("url").length()>8192||(kind!="worker"&&kind!="shared_worker"&&kind!="service_worker"))return false;
    auto record=CefDictionaryValue::Create();record->SetInt("contextId",context);record->SetDictionary("worker",worker->Copy(false));record->SetDouble("sequence",double(sequence_+1));record->SetDictionary("network",network->Copy(false));return Append(record);
  }
  bool Wait(int owner,int context,const std::string& event,double after,double deadline,Reply reply) {
    if(owner<1 || context<1 || !Allowed(event) || !reply || !std::isfinite(after) || after<0 || std::floor(after)!=after || after>static_cast<double>(sequence_) || !std::isfinite(deadline) || deadline<0 || waiters_.size()>=128 || waiters_.contains(owner))return false;
    if(!journal_.empty() && after<journal_.front().value->GetDouble("sequence")-1){reply(false,nullptr,"Native context network journal capacity exceeded before registration");return true;}
    Waiter waiter{context,event,after,deadline,std::move(reply)};
    for(const auto& record:journal_)if(Matches(waiter,record.value)){waiter.reply(true,record.value->Copy(false),"");return true;}
    waiters_.emplace(owner,std::move(waiter));return true;
  }
  void Cancel(int owner) {waiters_.erase(owner);}
  void Tick(double now) {
    std::vector<Reply> expired;
    for(auto it=waiters_.begin();it!=waiters_.end();) {
      if(it->second.deadline>0 && now>=it->second.deadline){expired.push_back(std::move(it->second.reply));it=waiters_.erase(it);}else ++it;
    }
    for(auto& reply:expired)reply(false,nullptr,"Native context network event deadline exceeded");
  }
  void Closed(int context) {
    std::vector<Reply> closing;
    for(auto it=waiters_.begin();it!=waiters_.end();) {
      if(it->second.context==context){closing.push_back(std::move(it->second.reply));it=waiters_.erase(it);}else ++it;
    }
    for(auto it=journal_.begin();it!=journal_.end();) {
      if(it->value->GetInt("contextId")==context){journal_bytes_-=it->bytes;it=journal_.erase(it);}else ++it;
    }
    for(auto& reply:closing)reply(false,nullptr,"Native browser context is closed");
  }
  uint64_t sequence() const {return sequence_;}
  size_t waiters() const {return waiters_.size();}
  size_t bytes() const {return journal_bytes_;}
  size_t records() const {return journal_.size();}
  void Clear() {waiters_.clear();journal_.clear();journal_bytes_=0;}
 private:
  bool Append(Dict record) {
    auto wrapped=CefValue::Create();wrapped->SetDictionary(record);const auto bytes=CefWriteJSON(wrapped,JSON_WRITER_DEFAULT).ToString().size();
    if(bytes>1048576)return false;
    ++sequence_;
    while(!journal_.empty() && (journal_.size()>=512 || bytes>8388608-journal_bytes_)){journal_bytes_-=journal_.front().bytes;journal_.pop_front();}
    journal_.push_back({record,bytes});journal_bytes_+=bytes;
    std::vector<Reply> completed;
    for(auto it=waiters_.begin();it!=waiters_.end();) {
      if(Matches(it->second,record)){completed.push_back(std::move(it->second.reply));it=waiters_.erase(it);}else ++it;
    }
    emit_(record);for(auto& reply:completed)reply(true,record->Copy(false),"");return true;
  }
  struct Record {Dict value;size_t bytes;};
  struct Waiter {int context;std::string event;double after,deadline;Reply reply;};
  static bool Allowed(const std::string& event) {return event=="request" || event=="response" || event=="requestfinished" || event=="requestfailed";}
  static bool Matches(const Waiter& waiter,Dict record) {return record->GetInt("contextId")==waiter.context && record->GetDouble("sequence")>waiter.after && record->GetDictionary("network")->GetString("event")==waiter.event;}
  Emit emit_;std::map<int,Waiter> waiters_;std::deque<Record> journal_;size_t journal_bytes_=0;uint64_t sequence_=0;
};
}  // namespace suitecut
