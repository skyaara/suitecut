// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "trace_journal.h"
namespace suitecut {
// Logical phase scheduling only. Source retains every actual protocol callback
// and its external screenshot lease after these original deadlines expire.
class TracePhaseControls final:public std::enable_shared_from_this<TracePhaseControls>{
 public:
  struct Request{TraceJournal::Lease scope;uint64_t token=0,call=0;std::string phase;double deadline=0;bool screen=false,aria=false;std::string frame{},session{},loader{};};
  struct Result{std::vector<uint8_t> png;std::string aria;};
  using Done=std::function<void()>;
  using Reply=std::function<void(bool,Result,const std::string&)>;
  struct Hooks{
    std::function<double()> now;
    std::function<bool(TraceJournal::Lease)> current;
    // false means the genuine native pipeline is busy; no callback was issued.
    std::function<bool(const Request&,Reply)> read;
    std::function<void(const Request&,Result)> captured;
    std::function<void(TraceJournal::Lease,const std::string&)> error;
    std::function<void()> wake;
  };
  explicit TracePhaseControls(Hooks hooks):hooks_(std::move(hooks)){}
  bool Submit(Request request,Done done={}){
    if(closed_||request.scope.context<1||!request.scope.session||!request.scope.chunk||!request.call||request.call>9007199254740991ULL||(!request.screen&&!request.aria)||!std::isfinite(request.deadline)||request.deadline<=hooks_.now()||(request.phase!="before"&&request.phase!="action"&&request.phase!="after")||next_>=9007199254740991ULL||jobs_.size()>=32)return false;
    request.token=++next_;const auto token=request.token;jobs_.emplace(token,Job{std::move(request),std::move(done)});queue_.push_back(token);if(hooks_.wake)hooks_.wake();Pump();return true;
  }
  bool Pending(int context)const{for(const auto& [token,job]:jobs_)if(job.request.scope.context==context)return true;return false;}
  size_t Size()const{return jobs_.size();}
  bool NeedsTick()const{return !jobs_.empty();}
  void Tick(){
    std::vector<uint64_t> stale,expired;const auto now=hooks_.now();for(const auto& [token,job]:jobs_)if(!hooks_.current(job.request.scope))stale.push_back(token);else if(now>=job.request.deadline)expired.push_back(token);
    for(const auto token:stale){Finish(token,false,{},"");}for(const auto token:expired){Finish(token,false,{},"Native trace phase capture deadline expired");}Pump();
  }
  void Clear(){closed_=true;auto jobs=std::move(jobs_);queue_.clear();for(auto& [token,job]:jobs)if(job.done)job.done();}
 private:
  struct Job{Request request;Done done;bool issued=false;};
  void Finish(uint64_t token,bool ok,Result result,const std::string& error){
    const auto found=jobs_.find(token);if(found==jobs_.end())return;const auto request=found->second.request;std::string failure=error;if(hooks_.now()>=request.deadline){ok=false;failure="Native trace phase capture deadline expired";}auto done=std::move(found->second.done);
    if(hooks_.current(request.scope)){
      if(ok&&((request.screen&&(result.png.empty()||result.png.size()>8*1048576))||(request.aria&&(result.aria.empty()||result.aria.size()>65536)))){ok=false;failure="Native trace phase payload byte limit exceeded";}
      if(ok)hooks_.captured(request,std::move(result));else if(!failure.empty())hooks_.error(request.scope,failure);
    }
    jobs_.erase(token);queue_.erase(std::remove(queue_.begin(),queue_.end(),token),queue_.end());if(done)done();Pump();if(hooks_.wake)hooks_.wake();
  }
  void Pump(){
    if(pumping_){return;}pumping_=true;
    for(const auto token:std::vector<uint64_t>(queue_.begin(),queue_.end())){
      size_t issued=0;for(const auto& [id,job]:jobs_)issued+=job.issued;if(issued>=2)break;
      const auto found=jobs_.find(token);if(found==jobs_.end()||found->second.issued)continue;
      const auto request=found->second.request;if(!hooks_.current(request.scope)||hooks_.now()>=request.deadline)continue;
      found->second.issued=true;const auto weak=weak_from_this();const bool accepted=hooks_.read(request,[weak,token](bool ok,Result result,const std::string& error){if(const auto self=weak.lock())self->Finish(token,ok,std::move(result),error);});
      if(!accepted){const auto pending=jobs_.find(token);if(pending!=jobs_.end())pending->second.issued=false;}
    }
    pumping_=false;
  }
  Hooks hooks_;std::map<uint64_t,Job> jobs_;std::deque<uint64_t> queue_;uint64_t next_=0;bool pumping_=false,closed_=false;
};
} // namespace suitecut
