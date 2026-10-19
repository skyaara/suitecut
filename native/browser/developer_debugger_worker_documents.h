// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
namespace suitecut {
// Source-owned dedicated-worker metadata ledger. Inputs must come from
// the accepted native attachment and original Runtime packets. No command,
// context ID, Inspector ACK, domain lease or author lifetime is manufactured.
class DeveloperDebuggerWorkerDocuments {
 public:
  struct Host{std::string source;uint64_t observer=0;bool operator==(const Host&)const=default;};
  struct Attachment{
    Host host;std::string target,session,parent_session;uint64_t generation=0;
    bool operator==(const Attachment&)const=default;
  };
  struct Pin{
    Attachment attachment;int context=0;std::string unique_context;
    // Native observation serial only, never a Chromium document/loader ID.
    uint64_t observation=0;bool operator==(const Pin&)const=default;
  };
  static constexpr size_t kRecords=128,kBytes=1048576;
  explicit DeveloperDebuggerWorkerDocuments(Host actual):host_(std::move(actual)){
    unavailable_=!Text(host_.source,128)||!host_.observer;
  }
  DeveloperDebuggerWorkerDocuments(const DeveloperDebuggerWorkerDocuments&)=delete;
  DeveloperDebuggerWorkerDocuments&operator=(const DeveloperDebuggerWorkerDocuments&)=delete;
  bool Attached(Attachment actual,const std::string&actual_kind,std::string&error){
    if(!Available()||actual.host!=host_||actual_kind!="worker"||!Text(actual.target,128)||!Text(actual.session,128)||
       actual.parent_session.size()>128||actual.parent_session.find('\0')!=std::string::npos||!actual.generation||actual.generation<=last_generation_){
      error="Native dedicated-worker debugger requires an actual same-Source attachment receipt";return false;
    }
    if(records_.size()>=kRecords||records_.contains(actual.session)){
      error="Native dedicated-worker debugger attachment is duplicate or metadata capacity is full";return false;
    }
    for(const auto&[session,record]:records_)if(record.attachment.target==actual.target){
      error="Native dedicated-worker debugger previous target attachment has not retired";return false;
    }
    const size_t bytes=AttachmentBytes(actual);
    if(bytes>kBytes-bytes_){error="Native dedicated-worker debugger metadata payload exceeds its bound";return false;}
    // All fallible allocations precede metadata publication/accounting.
    std::map<std::string,Record>staged;
    auto session=actual.session;
    staged.emplace(std::move(session),Record{std::move(actual),0,{},0,bytes});
    const auto generation=staged.begin()->second.attachment.generation;
    records_.insert(staged.extract(staged.begin()));bytes_+=bytes;last_generation_=generation;return true;
  }
  bool ContextCreated(const Attachment&actual,int context,std::string unique,std::string&error){
    auto record=Find(actual);
    if(!record||context<=0||!Text(unique,256)||observations_==UINT64_MAX){
      error="Native dedicated-worker debugger requires exact actual Runtime unique-context metadata";return false;
    }
    if(record->context==context&&record->unique==unique)return true;
    const auto old=record->unique.size(),next=unique.size();
    if(next>kBytes-(bytes_-old)){error="Native dedicated-worker debugger Runtime metadata payload exceeds its bound";return false;}
    // Passed-by-value unique is already owned before publication. Swapping is
    // non-allocating and the old immutable pin cannot become current again.
    record->unique.swap(unique);record->context=context;record->observation=++observations_;
    record->bytes=record->bytes-old+next;bytes_=bytes_-old+next;return true;
  }
  bool Resolve(const Attachment&actual,Pin&output)const{
    const auto record=Find(actual);if(!record||!record->context||record->unique.empty()||!record->observation)return false;
    Pin staged{record->attachment,record->context,record->unique,record->observation};output=std::move(staged);return true;
  }
  bool Current(const Pin&pin)const{
    const auto record=Find(pin.attachment);return record&&record->context>0&&record->context==pin.context&&
      !record->unique.empty()&&record->unique==pin.unique_context&&record->observation==pin.observation;
  }
  // These methods invalidate metadata only. Existing accepted Inspector calls
  // and per-session arbiter ownership must remain in their physical ledgers.
  bool ContextDestroyed(const Attachment&actual,int actual_context){
    auto record=Find(actual);if(!record||record->context!=actual_context||actual_context<=0)return false;
    Invalidate(*record);return true;
  }
  bool ContextsCleared(const Attachment&actual){auto record=Find(actual);if(!record)return false;Invalidate(*record);return true;}
  bool Detached(const Attachment&actual){
    auto record=Find(actual);if(!record)return false;
    bytes_-=record->bytes;records_.erase(actual.session);return true;
  }
  // Exact Source-local channel drain without copying strings during teardown.
  bool ChannelDrained(const std::string&actual_source,uint64_t actual_observer,const std::string&session,uint64_t actual_generation)noexcept{
    if(actual_source!=host_.source||actual_observer!=host_.observer||!actual_generation)return false;
    const auto found=records_.find(session);if(found==records_.end()||found->second.attachment.generation!=actual_generation)return false;
    bytes_-=found->second.bytes;records_.erase(found);return true;
  }
  void Unavailable()noexcept{unavailable_=true;}
  void ObserverClosed(const std::string&actual_source,uint64_t actual_observer)noexcept{if(actual_source!=host_.source||actual_observer!=host_.observer)return;unavailable_=true;records_.clear();bytes_=0;}
  void ObserverClosed(const Host&actual)noexcept{ObserverClosed(actual.source,actual.observer);}
  bool Available()const noexcept{return !unavailable_;}
  size_t Records()const noexcept{return records_.size();}
  size_t Payload()const noexcept{return bytes_;}
 private:
  struct Record{Attachment attachment;int context=0;std::string unique;uint64_t observation=0;size_t bytes=0;};
  static bool Text(const std::string&text,size_t max){return !text.empty()&&text.size()<=max&&text.find('\0')==std::string::npos;}
  static size_t AttachmentBytes(const Attachment&actual){return sizeof(Record)+actual.host.source.size()+actual.target.size()+actual.session.size()*2+actual.parent_session.size();}
  Record*Find(const Attachment&actual){if(!Available())return nullptr;const auto found=records_.find(actual.session);return found!=records_.end()&&found->second.attachment==actual?&found->second:nullptr;}
  const Record*Find(const Attachment&actual)const{if(!Available())return nullptr;const auto found=records_.find(actual.session);return found!=records_.end()&&found->second.attachment==actual?&found->second:nullptr;}
  void Invalidate(Record&record)noexcept{bytes_-=record.unique.size();record.bytes-=record.unique.size();record.unique.clear();record.context=0;record.observation=0;}
  Host host_;std::map<std::string,Record>records_;uint64_t last_generation_=0,observations_=0;size_t bytes_=0;bool unavailable_=false;
};
}
