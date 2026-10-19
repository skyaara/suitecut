// SPDX-License-Identifier: MIT
#pragma once
#include "descendant_owner.h"
#include <map>
namespace suitecut::runner {
// One process-wide subreaper, shared by the native cooperative pool. Detached
// adopted children cannot always be assigned to a project. Retain all batch
// leases until every genuine primary waitpid +file/rollback fence has arrived,
// then perform the existing native cleanup without reaping another actor's PID.
class ProjectDescendants {
 public:
  static constexpr size_t kWorkers=8;
  bool CanEnroll()const{return !retiring_;}
  uint64_t Enroll(unsigned index,DedicatedHostProcess& host){
    if(!index||index<=highest_worker_||retiring_||members_.size()>=kWorkers)throw std::runtime_error("Native descendant batch worker admission rejected");
    if(members_.empty()){if(generation_==UINT64_MAX)throw std::overflow_error("Native descendant batch generation exhausted");owner_.Begin(host);++generation_;first_=&host;}
    members_.emplace(index,Member{&host,host.Pid(),false});highest_worker_=index;return generation_;
  }
  bool Arrive(uint64_t generation,unsigned index,DedicatedHostProcess& host,bool files_retired,double now,double grace=1000){
    const auto found=members_.find(index);
    if(generation!=generation_||retiring_||found==members_.end()||found->second.host!=&host||found->second.pid!=host.Pid()||found->second.arrived||!files_retired||!host.Exited())return false;
    found->second.arrived=true;
    if(std::all_of(members_.begin(),members_.end(),[](const auto& entry){return entry.second.arrived;})){
      owner_.BeginRetirement(*first_,now,grace);retiring_=true;physical_end_=now+6000;
    }
    return true;
  }
  bool Arrived(uint64_t generation,unsigned index,DedicatedHostProcess& host)const{
    const auto found=members_.find(index);return generation==generation_&&found!=members_.end()&&found->second.arrived&&found->second.host==&host&&found->second.pid==host.Pid()&&host.Exited().has_value();
  }
  void Tick(double now){
    if(!retiring_)return;
    if(owner_.Tick(now)){
      retired_=generation_;for(const auto& child:owner_.Exits())if(child.signaled||child.code!=0){if(errors_.size()>=256)throw std::runtime_error("Native descendant batch error capacity exceeded");errors_.push_back("Actual native descendant batch "+std::to_string(generation_)+" PID "+std::to_string(child.pid)+(child.signaled?" signal ":" exit ")+std::to_string(child.code)+(child.kill_requested?" after native cleanup SIGKILL":""));}
      members_.clear();first_=nullptr;retiring_=false;
    }else if(now>=physical_end_)throw std::runtime_error("Native descendant batch physical exit remains unconfirmed after SIGKILL");
  }
  bool Pending(uint64_t generation)const{return generation&&generation>retired_;}
  bool Active()const{return !members_.empty();}
  std::vector<std::string> TakeErrors(){auto errors=std::move(errors_);errors_.clear();return errors;}
 private:
  struct Member {DedicatedHostProcess* host;pid_t pid;bool arrived;};
  DescendantOwner owner_;std::map<unsigned,Member> members_;DedicatedHostProcess* first_=nullptr;uint64_t generation_=0,retired_=0;unsigned highest_worker_=0;bool retiring_=false;double physical_end_=0;std::vector<std::string> errors_;
};
}
