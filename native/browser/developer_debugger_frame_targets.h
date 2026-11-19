// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
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
  void Open(int owner,const Pin&actual,Dict params,double deadline,Controls::Reply reply){
    if(closed_||!hooks_.create||!hooks_.current||!hooks_.current(actual)||!actual.document||!actual.attachment||actual.frame.empty()||next_==UINT64_MAX){reply(false,"","Native frame debugger actual current pin is unavailable");return;}
    // Same-frame reopening is forbidden through actual physical retirement,
    // including after navigation invalidated the old immutable document pin.
    for(const auto&[id,record]:records_)if(record->pin.frame==actual.frame&&record->control->Retained()){
      reply(false,"","Native frame debugger previous owner is still physically retained");return;
    }
    Prune(&actual.frame);
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
    reply(false,"","Native frame debugger session token is unknown or stale");
  }
  void Cancel(int owner){ForEach([&](const std::shared_ptr<Record>&record){record->control->Cancel(owner);});}
  void Changed(const Pin&pin){ForEach([&](const std::shared_ptr<Record>&record){if(record->pin==pin)record->control->Changed();});Prune();}
  void Tick(double now){ForEach([&](const std::shared_ptr<Record>&record){record->control->Tick(now);});Prune();}
  bool NeedsTick()const{for(const auto&[id,record]:records_)if(record->control->NeedsTick())return true;return false;}
  // The actual initial root renderer death also destroys its same-process
  // child realms. Independent actual OOPIF agent sessions are not included.
  void RootRealmDestroyed(uint64_t actual_source_epoch){ForEach([&](const std::shared_ptr<Record>&record){if(record->pin.channel.session.empty())record->control->RealmDestroyed(actual_source_epoch);});}
  // Invoke only AFTER real exact session arbiter/callback drain. A logical
  // frame removal, navigation, cancel or realm loss must never call this.
  void ChannelClosed(const FrameDebuggerScopes::Channel&channel,uint64_t actual_attachment){
    ForEach([&](const std::shared_ptr<Record>&record){if(record->pin.channel==channel&&record->pin.attachment==actual_attachment)record->control->Closed();});
  }
  void Closed(){if(closed_)return;closed_=true;ForEach([&](const std::shared_ptr<Record>&record){record->control->Closed();});}
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
  // A completed controller ends with its document: once it is no longer
  // retained, its record is released when its pin stops being current or its
  // frame reopens. Retained controllers are never touched.
  void Prune(const std::string*reopened=nullptr){std::erase_if(records_,[&](const auto&entry){const auto&record=*entry.second;return !record.control->Retained()&&((reopened&&record.pin.frame==*reopened)||!hooks_.current(record.pin));});}

  // Visits the records admitted before the fanout began (serials are
  // monotonic), re-finding each by key so callbacks may admit new records.
  // All record nodes already exist: the fanout allocates nothing.
  template<class F>void ForEach(F&&visit)const{const auto last=next_;for(auto it=records_.begin();it!=records_.end()&&it->first<=last;){const auto id=it->first;const auto record=it->second;visit(record);it=records_.upper_bound(id);}}
  Hooks hooks_;std::map<uint64_t,std::shared_ptr<Record>>records_;uint64_t next_=0;bool closed_=false;
};
}
