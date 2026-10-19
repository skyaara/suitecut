// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "route_callback_receipt.h"
#include "har_work_receipts.h"
namespace suitecut {
// The coordinator reserves this immutable observer before either route family
// changes. It never owns a handler/read/Fetch operation and cannot settle one.
class NativeContextUnrouteWaits {
 public:
  using Reply=std::function<void(bool,const std::string&)>;
  enum class Family {Handler,HAR};
  static constexpr size_t kCapacity=32,kSnapshotCapacity=256,kBytes=131072;
  explicit NativeContextUnrouteWaits(std::function<double()>now):now_(std::move(now)){}
  ~NativeContextUnrouteWaits(){Clear();}
  bool Reserve(int operation,int context,uint64_t generation,bool wait,NativeRouteCallbackSnapshot handlers,NativeHARWorkSnapshot har,double deadline,Reply reply){
    if(closed_||operation<1||context<1||!generation||!reply||!std::isfinite(deadline)||deadline<0||(deadline&&now_()>=deadline)||entries_.contains(operation)||entries_.size()>=kCapacity||handlers.size()+har.size()>kSnapshotCapacity)return false;
    if(handlers.capacity()>kBytes/sizeof(NativeRouteCallbackSnapshot::value_type)||har.capacity()>kBytes/sizeof(NativeHARWorkSnapshot::value_type))return false;
    const size_t bytes=handlers.capacity()*sizeof(NativeRouteCallbackSnapshot::value_type)+har.capacity()*sizeof(NativeHARWorkSnapshot::value_type);
    if(bytes>kBytes-bytes_)return false;
    for(const auto&receipt:handlers)if(!receipt)return false;
    for(const auto&receipt:har)if(!receipt)return false;
    entries_.emplace(operation,Entry{context,generation,wait,std::move(handlers),std::move(har),deadline,bytes,std::move(reply),false,false});bytes_+=bytes;return true;
  }
  bool Applied(int operation,uint64_t generation,Family family,bool ok,const std::string&error){
    auto found=entries_.find(operation);if(found==entries_.end()||found->second.generation!=generation)return false;
    auto& entry=found->second;auto& applied=family==Family::Handler?entry.handlers_applied:entry.har_applied;
    if(applied)return false;
    if(!ok){auto reply=Remove(found);reply(false,error.empty()?"Native context unrouteAll target removal failed":error);return true;}
    applied=true;const auto status=Inspect(entry,now_());if(status.kind!=Status::Pending){auto reply=Remove(found);reply(status.kind==Status::Success,status.error);}return true;
  }
  void Tick(){
    std::vector<std::pair<Reply,Status>>ready;const auto now=now_();
    for(auto it=entries_.begin();it!=entries_.end();){const auto status=Inspect(it->second,now);if(status.kind==Status::Pending){++it;continue;}auto current=it++;ready.emplace_back(Remove(current),status);}
    for(auto&[reply,status]:ready)reply(status.kind==Status::Success,status.error);
  }
  void Cancel(int operation,uint64_t generation){const auto found=entries_.find(operation);if(found!=entries_.end()&&found->second.generation==generation)Remove(found);}
  void Closed(int context){std::vector<Reply>ready;for(auto it=entries_.begin();it!=entries_.end();)if(it->second.context==context){auto current=it++;ready.push_back(Remove(current));}else ++it;for(auto&reply:ready)reply(false,"Actual native context closed during unrouteAll");}
  void Clear(){if(closed_)return;closed_=true;std::vector<Reply>ready;for(auto&[operation,entry]:entries_){(void)operation;ready.push_back(std::move(entry.reply));}entries_.clear();bytes_=0;for(auto&reply:ready)reply(false,"Native context unrouteAll observer closed");}
  bool Contains(int operation)const{return entries_.contains(operation);}
  size_t pending()const{return entries_.size();}
  size_t bytes()const{return bytes_;}
 private:
  struct Entry {int context;uint64_t generation;bool wait;NativeRouteCallbackSnapshot handlers;NativeHARWorkSnapshot har;double deadline;size_t bytes;Reply reply;bool handlers_applied,har_applied;};
  struct Status {enum Kind{Pending,Success,Failure}kind;std::string error;};
  using Iterator=std::map<int,Entry>::iterator;
  Reply Remove(Iterator found){bytes_-=found->second.bytes;auto reply=std::move(found->second.reply);entries_.erase(found);return reply;}
  static Status Inspect(const Entry&entry,double now){
    if(entry.deadline&&now>=entry.deadline)return {Status::Failure,"Native context unrouteAll original deadline exceeded"};
    if(!entry.handlers_applied||!entry.har_applied)return {Status::Pending,{}};
    if(!entry.wait)return {Status::Success,{}};
    bool pending=false;
    for(const auto&receipt:entry.handlers){if(receipt->unavailable())return {Status::Failure,"Native handler observer unavailable; callback settlement not proven"};if(!receipt->settled())pending=true;}
    for(const auto&receipt:entry.har){if(receipt->unavailable())return {Status::Failure,"Native HAR observer unavailable; read/Fetch settlement not proven"};if(receipt->failed())return {Status::Failure,receipt->error().empty()?"Native HAR read/Fetch work failed":receipt->error()};if(!receipt->settled())pending=true;}
    return {pending?Status::Pending:Status::Success,{}};
  }
  std::function<double()>now_;bool closed_=false;size_t bytes_=0;std::map<int,Entry>entries_;
};
}
