// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <array>
#include "developer_debugger_controls.h"
namespace suitecut {
// One Source's captured frame controllers. Records carry actual immutable
// observer/session/attachment/document identities; request frame text selects
// only the initial observation and never supplies any Inspector authority.
class DeveloperDebuggerFrameTargets {
 public:
  using Controls=DeveloperDebuggerControls;
  using Pin=FrameDebuggerScopes::Pin;
  using Dict=Controls::Dict;
  struct Hooks {
    // Creates only a native controller. It must send nothing until Handle(open).
    std::function<std::unique_ptr<Controls>(const Pin&)> create;
    std::function<bool(const Pin&)> current;
  };
  explicit DeveloperDebuggerFrameTargets(Hooks hooks):hooks_(std::move(hooks)){}
  DeveloperDebuggerFrameTargets(const DeveloperDebuggerFrameTargets&)=delete;
  DeveloperDebuggerFrameTargets&operator=(const DeveloperDebuggerFrameTargets&)=delete;
  static constexpr size_t kPhysical=8,kRecords=32;
  void Open(int owner,const Pin&actual,Dict params,double deadline,Controls::Reply reply){
    if(closed_||!hooks_.create||!hooks_.current||!hooks_.current(actual)||!actual.document||!actual.attachment||actual.frame.empty()||actual.frame.size()>256||next_==UINT64_MAX){reply(false,"","Native frame debugger actual current pin is unavailable");return;}
    if(Live()>=kPhysical){reply(false,"","Native frame debugger live controller capacity exceeded");return;}
    // Same-frame reopening is forbidden through actual physical retirement,
    // including after navigation invalidated the old immutable document pin.
    for(const auto&[id,record]:records_)if(record->pin.frame==actual.frame&&record->control->Retained()){
      reply(false,"","Native frame debugger previous owner is still physically retained");return;
    }
    // Only completed metadata may be evicted. Its later unknown token must
    // fail explicitly; eviction is never a physical callback/lease retirement.
    while(records_.size()>=kRecords){
      const auto old=std::find_if(records_.begin(),records_.end(),[](const auto&entry){return !entry.second->control->Retained();});
      if(old==records_.end()){reply(false,"","Native frame debugger retained record capacity exceeded");return;}
      records_.erase(old);
    }
    std::map<uint64_t,std::shared_ptr<Record>>staged;
    auto control=hooks_.create(actual);
    if(!control){reply(false,"","Native frame debugger controller construction failed");return;}
    const auto serial=next_+1;
    auto record=std::make_shared<Record>(Record{actual,std::move(control)});
    staged.emplace(serial,record);
    records_.insert(staged.extract(staged.begin()));next_=serial;
    record->control->Handle(owner,params,deadline,std::move(reply));
  }
  void Handle(int owner,const std::string&requested_frame,Dict params,double deadline,Controls::Reply reply){
    if(closed_||!params||params->GetType("session")!=VTYPE_STRING){reply(false,"","Native frame debugger requires its exact native session token");return;}
    const auto token=params->GetString("session").ToString();
    std::shared_ptr<Record>selected;
    for(const auto&[id,record]:records_)if(record->control->OwnsSession(token)){selected=record;break;}
    if(selected){
      if(requested_frame!=selected->pin.frame){reply(false,"","Native frame debugger token belongs to another captured frame");return;}
      selected->control->Handle(owner,params,deadline,std::move(reply));return;
    }
    reply(false,"","Native frame debugger session token is unknown, stale or its completed receipt was evicted");
  }
  void Cancel(int owner){for(const auto&record:Snapshot())record->control->Cancel(owner);}
  void Changed(const Pin&pin){for(const auto&record:Snapshot())if(record->pin==pin)record->control->Changed();}
  void Tick(double now){for(const auto&record:Snapshot())record->control->Tick(now);}
  bool NeedsTick()const{for(const auto&[id,record]:records_)if(record->control->NeedsTick())return true;return false;}
  // The actual initial root renderer death also destroys its same-process
  // child realms. Independent actual OOPIF agent sessions are not included.
  void RootRealmDestroyed(uint64_t actual_source_epoch){for(const auto&record:Snapshot())if(record->pin.channel.session.empty())record->control->RealmDestroyed(actual_source_epoch);}
  // Invoke only AFTER real exact session arbiter/callback drain. A logical
  // frame removal, navigation, cancel or realm loss must never call this.
  void ChannelClosed(const FrameDebuggerScopes::Channel&channel,uint64_t actual_attachment){
    for(const auto&record:Snapshot())if(record->pin.channel==channel&&record->pin.attachment==actual_attachment)record->control->Closed();
  }
  void Closed(){if(closed_)return;closed_=true;for(const auto&record:Snapshot())record->control->Closed();}
  size_t Live()const{return std::count_if(records_.begin(),records_.end(),[](const auto&entry){return entry.second->control->Retained();});}
  bool AcceptedWorkReady()const{if(closed_)return false;for(const auto&[id,record]:records_){(void)id;if(record->control->PhysicalCount())return false;}return true;}
  size_t Records()const{return records_.size();}
  void Diagnostics(Dict output)const{
    for(const char*key:{"nativeDeveloperDebuggerSessions","nativeDeveloperDebuggerWaiters","nativeDeveloperDebuggerPhysical","nativeDeveloperDebuggerBreakpoints","nativeDeveloperDebuggerEvaluations","nativeDeveloperDebuggerScriptCursors"}){
      int total=output->HasKey(key)?output->GetInt(key):0;
      for(const auto&[id,record]:records_){auto selected=CefDictionaryValue::Create();record->control->Diagnostics(selected);total+=selected->GetInt(key);}
      output->SetInt(key,total);
    }
    if(!records_.empty()){
      auto selected=CefDictionaryValue::Create();records_.begin()->second->control->Diagnostics(selected);
      for(const char*key:{"nativeDeveloperDebuggerProcessSessions","nativeDeveloperDebuggerProcessScriptCursors","nativeDeveloperDebuggerProcessBreakpoints","nativeDeveloperDebuggerProcessEvaluations"})output->SetInt(key,selected->GetInt(key));
    }
    output->SetInt("nativeFrameDeveloperDebuggerRecords",int(Records()));
    output->SetInt("nativeFrameDeveloperDebuggerLive",int(Live()));
  }
 private:
  struct Record{Pin pin;std::unique_ptr<Controls>control;};
  struct RecordSnapshot{
    std::array<std::shared_ptr<Record>,kRecords>records;size_t count=0;
    const std::shared_ptr<Record>*begin()const noexcept{return records.data();}
    const std::shared_ptr<Record>*end()const noexcept{return records.data()+count;}
  };
  // All record nodes already exist. Actual close/retirement fanout must not
  // allocate a vector after admission or depend on free heap for its snapshot.
  RecordSnapshot Snapshot()const noexcept{RecordSnapshot out;for(const auto&[id,record]:records_)out.records[out.count++]=record;return out;}
  Hooks hooks_;std::map<uint64_t,std::shared_ptr<Record>>records_;uint64_t next_=0;bool closed_=false;
};
}
