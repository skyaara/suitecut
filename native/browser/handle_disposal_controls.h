// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
namespace suitecut {
// Actual retained handle groups stay owned until release ACK or channel death.
// Logical cancellation never cancels this bounded physical cleanup queue.
class HandleDisposalControls {
 public:
  struct Handle {int id;std::string group,session;};
  using Ack=std::function<void(bool,bool,const std::string&)>;
  using Release=std::function<void(int,const Handle&,Ack)>;
  using Retire=std::function<void(const Handle&)>;
  using Done=std::function<void(int,uint64_t,bool,const std::string&)>;
  using Fail=std::function<void(const std::string&)>;
  HandleDisposalControls(Release release,Retire retire,Done done,Fail fail):release_(std::move(release)),retire_(std::move(retire)),done_(std::move(done)),fail_(std::move(fail)){}
  ~HandleDisposalControls(){life_->alive=false;}
  bool Begin(int owner,uint64_t generation,std::vector<Handle> handles,double now){
    if(closed_||owner<=0||!generation||handles.empty()||handles.size()>1024||jobs_.size()>=64||owned_.size()+handles.size()>1024||jobs_.contains(generation))return false;
    std::set<int> unique;
    for(const auto& handle:handles)if(handle.id<=0||handle.group.empty()||handle.group.size()>256||handle.session.size()>256||owned_.contains(handle.id)||!unique.insert(handle.id).second)return false;
    auto job=std::make_shared<Job>();job->owner=owner;job->generation=generation;job->handles=std::move(handles);job->deadline=now+30000;
    owned_.insert(unique.begin(),unique.end());jobs_.emplace(generation,job);Next(job);return true;
  }
  void Tick(double now){if(closed_)return;for(const auto&[generation,job]:jobs_)if(now>=job->deadline&&!job->expired){job->expired=true;fail_("Native handle disposal physical settlement deadline exceeded");return;}}
  void Close(){closed_=true;jobs_.clear();owned_.clear();physical_=0;}
  size_t Size()const{return jobs_.size();}size_t Physical()const{return physical_;}size_t Handles()const{return owned_.size();}size_t Failures()const{return failures_;}
 private:
  struct Life{bool alive=true;};
  struct Job{int owner=0;uint64_t generation=0;std::vector<Handle> handles;size_t next=0;bool pending=false,expired=false;double deadline=0;std::string error;};
  void Next(const std::shared_ptr<Job>& job){
    if(closed_||job->pending)return;
    if(job->next==job->handles.size()){
      const auto error=job->error;const auto found=jobs_.find(job->generation);
      if(found==jobs_.end()||found->second!=job)return;
      jobs_.erase(found);done_(job->owner,job->generation,error.empty(),error);return;
    }
    const auto handle=job->handles[job->next];job->pending=true;++physical_;const auto life=life_;
    release_(job->owner,handle,[this,life,job,handle](bool ok,bool destroyed,const std::string& error){
      if(!life->alive||closed_||!job->pending)return;
      job->pending=false;--physical_;owned_.erase(handle.id);++job->next;
      if(ok||destroyed)retire_(handle);
      else{++failures_;if(job->error.empty())job->error=error.empty()?"Native handle disposal failed":error.substr(0,1024);}
      Next(job);
    });
  }
  bool closed_=false;size_t physical_=0,failures_=0;std::map<uint64_t,std::shared_ptr<Job>> jobs_;std::set<int> owned_;std::shared_ptr<Life> life_=std::make_shared<Life>();Release release_;Retire retire_;Done done_;Fail fail_;
};
}
