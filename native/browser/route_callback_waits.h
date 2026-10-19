// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include "route_callback_receipt.h"
namespace suitecut {
// Waits observe only the exact original snapshot after real policy removal ACK.
// They never finish/cancel a physical lease, and never include future handlers.
class NativeRouteCallbackWaits {
 public:
  using Reply=std::function<void(bool,const std::string&)>;
  explicit NativeRouteCallbackWaits(std::function<double()>now):now_(std::move(now)){}
  ~NativeRouteCallbackWaits(){Clear();}
  NativeRouteCallbackWaits(const NativeRouteCallbackWaits&)=delete;
  NativeRouteCallbackWaits& operator=(const NativeRouteCallbackWaits&)=delete;
  bool Reserve(int operation,int context,uint64_t generation,NativeRouteCallbackSnapshot snapshot,double deadline,Reply reply){
    if(closed_||operation<1||context<1||!generation||!reply||!std::isfinite(deadline)||deadline<0||entries_.size()>=64||entries_.contains(operation)||snapshot.size()>128)return false;
    if(snapshot.capacity()>131072/sizeof(NativeRouteCallbackSnapshot::value_type))return false;
    const auto bytes=snapshot.capacity()*sizeof(NativeRouteCallbackSnapshot::value_type);
    if(bytes>131072-bytes_)return false;
    for(const auto&receipt:snapshot)if(!receipt)return false;
    bytes_+=bytes;entries_.emplace(operation,Entry{context,generation,std::move(snapshot),deadline,bytes,std::move(reply),false});return true;
  }
  bool Applied(int operation,uint64_t generation){
    auto found=entries_.find(operation);if(found==entries_.end()||found->second.generation!=generation)return false;
    found->second.applied=true;const auto status=Inspect(found->second.snapshot,found->second.deadline,now_());
    if(status!=Status::Pending){auto reply=std::move(found->second.reply);bytes_-=found->second.bytes;entries_.erase(found);Complete(std::move(reply),status);}return true;
  }
  void Failed(int operation,uint64_t generation,const std::string&error){
    auto found=entries_.find(operation);if(found==entries_.end()||found->second.generation!=generation)return;
    auto reply=std::move(found->second.reply);bytes_-=found->second.bytes;entries_.erase(found);reply(false,error);
  }
  // Convenience for a snapshot whose actual remove ACK already arrived. The
  // production two-stage adapter reserves capacity before mutating policy.
  bool Wait(int operation,int context,NativeRouteCallbackSnapshot snapshot,double deadline,Reply reply){
    if(!Reserve(operation,context,1,std::move(snapshot),deadline,std::move(reply)))return false;
    Applied(operation,1);return true;
  }
  void Tick(){
    // Remove before delivery. A callback may destroy this controller or reuse a
    // completed operation ID; no saved iterator/ID can touch the replacement.
    std::vector<std::pair<Reply,Status>>done;
    const auto now=now_();for(auto it=entries_.begin();it!=entries_.end();){const auto status=Inspect(it->second.snapshot,it->second.deadline,now);if(status==Status::Pending||(status==Status::Settled&&!it->second.applied)){++it;continue;}bytes_-=it->second.bytes;done.emplace_back(std::move(it->second.reply),status);it=entries_.erase(it);}
    for(auto&[reply,status]:done)Complete(std::move(reply),status);
  }
  void Cancel(int operation){auto found=entries_.find(operation);if(found==entries_.end())return;bytes_-=found->second.bytes;entries_.erase(found);}
  void Closed(int context){
    std::vector<Reply>done;for(auto it=entries_.begin();it!=entries_.end();)if(it->second.context==context){bytes_-=it->second.bytes;done.push_back(std::move(it->second.reply));it=entries_.erase(it);}else ++it;
    for(auto&reply:done)reply(false,"Actual native routing context closed before callback settlement");
  }
  void Clear(){
    if(closed_)return;
    closed_=true;std::vector<Reply>done;for(auto&[operation,entry]:entries_)done.push_back(std::move(entry.reply));entries_.clear();bytes_=0;
    for(auto&reply:done)reply(false,"Native route callback observer disposed before settlement");
  }
  bool Contains(int operation)const{return entries_.contains(operation);}
  size_t pending()const{return entries_.size();}
  size_t bytes()const{return bytes_;}
 private:
  enum class Status{Pending,Settled,Deadline,Unavailable};
  struct Entry{int context;uint64_t generation;NativeRouteCallbackSnapshot snapshot;double deadline;size_t bytes;Reply reply;bool applied;};
  static Status Inspect(const NativeRouteCallbackSnapshot&snapshot,double deadline,double now){
    if(deadline&&now>=deadline)return Status::Deadline;
    bool pending=false;for(const auto&receipt:snapshot){if(receipt->unavailable())return Status::Unavailable;if(!receipt->settled())pending=true;}return pending?Status::Pending:Status::Settled;
  }
  static void Complete(Reply reply,Status status){reply(status==Status::Settled,status==Status::Deadline?"Native unrouteAll original deadline exceeded":status==Status::Unavailable?"Native callback receipt observer unavailable; physical settlement not proven":"");}
  std::function<double()>now_;bool closed_=false;size_t bytes_=0;std::map<int,Entry>entries_;
};
}
