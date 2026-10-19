// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "coverage_controls.h"
#include "include/cef_parser.h"
namespace suitecut {
// Native admission for actual target sessions. A queued command retains its
// original deadline; it is distinct from a dispatched command awaiting real ACK.
class CoverageDispatch : public std::enable_shared_from_this<CoverageDispatch> {
 public:
  using Dict=CoverageControls::Dict;using Reply=CoverageControls::ProtocolReply;
  static constexpr size_t kBackend=16,kQueued=256,kBytes=8*1048576;
  struct Hooks{std::function<double()> now;std::function<bool(const std::string&,const std::string&,Dict,double,Reply)> send;std::function<void()> wake;};
  explicit CoverageDispatch(Hooks hooks):hooks_(std::move(hooks)){}
  bool Send(const std::string& session,const std::string& method,Dict params,double deadline,Reply reply){
    if(closed_||!reply||generation_>=9007199254740991ULL||queue_.size()>=kQueued||calls_.size()>=kQueued+kBackend)return false;
    auto value=CefValue::Create();if(params)value->SetDictionary(params->Copy(false));else value->SetNull();const auto bytes=CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString().size()+method.size()+session.size();
    if(bytes>kBytes-bytes_)return false;
    auto call=std::make_shared<Call>();call->id=++generation_;call->session=session;call->method=method;call->params=params?params->Copy(false):nullptr;call->deadline=deadline;call->reply=std::move(reply);call->bytes=bytes;calls_[call->id]=call;queue_.push_back(call->id);bytes_+=bytes;Pump();Wake();return true;
  }
  void Tick(double now){std::vector<uint64_t> expired;for(const auto& [id,call]:calls_)if(!call->dispatched&&call->deadline>0&&now>=call->deadline)expired.push_back(id);Drop(expired,"Native coverage queued command exceeded its original deadline");Pump();}
  void CancelQueued(const std::string& action,const std::string& error){const bool js=action=="startJS"||action=="stopJS";std::vector<uint64_t> canceled;for(const auto& [id,call]:calls_)if(!call->dispatched&&!CleanupMethod(call->method)&&((call->method.starts_with("Debugger.")||call->method.starts_with("Profiler."))==js))canceled.push_back(id);Drop(canceled,error);}
  // These are actual transport-close boundaries, supplied by Source after an
  // observed detach/OnBeforeClose. No timer may call them to invent retirement.
  void Detached(const std::string& session){std::vector<uint64_t> detached;for(const auto& [id,call]:calls_)if(call->session==session)detached.push_back(id);Drop(detached,"Actual native coverage target session detached");}
  void Closed(){if(closed_)return;closed_=true;std::vector<uint64_t> ids;for(const auto& [id,call]:calls_)ids.push_back(id);Drop(ids,"Actual native coverage page channel closed");queue_.clear();}
  size_t Physical()const{return backend_;}size_t Pending()const{return calls_.size();}size_t Bytes()const{return bytes_;}
 private:
  static bool CleanupMethod(const std::string& method){return method.ends_with(".disable")||method=="Profiler.stopPreciseCoverage"||method=="CSS.stopRuleUsageTracking"||method=="Profiler.stopPreciseCoverageWithNavigationRetention"||method=="CSS.stopRuleUsageTrackingWithNavigationRetention";}
  struct Call{uint64_t id=0;std::string session,method;Dict params;double deadline=0;Reply reply;size_t bytes=0;bool dispatched=false;};
  void Wake(){if(hooks_.wake)hooks_.wake();}
  void Drop(const std::vector<uint64_t>& ids,const std::string& error){const bool prior=std::exchange(pumping_,true);for(auto id:ids)Finish(id,false,nullptr,error);pumping_=prior;Pump();}
  void Finish(uint64_t id,bool ok,Dict result,const std::string& error){const auto found=calls_.find(id);if(found==calls_.end())return;auto call=found->second;calls_.erase(found);bytes_-=call->bytes;if(call->dispatched)--backend_;else std::erase(queue_,id);auto reply=std::move(call->reply);if(reply)reply(ok,result,error);Pump();Wake();}
  void Pump(){
    if(pumping_||closed_)return;
    struct Guard{bool& value;explicit Guard(bool& flag):value(flag){value=true;}~Guard(){value=false;}} guard(pumping_);
    while(!closed_&&backend_<kBackend&&!queue_.empty()){
      const auto id=queue_.front();queue_.pop_front();const auto found=calls_.find(id);if(found==calls_.end())continue;auto call=found->second;
      if(call->deadline>0&&hooks_.now()>=call->deadline){Finish(id,false,nullptr,"Native coverage queued command exceeded its original deadline");continue;}
      call->dispatched=true;++backend_;const auto weak=weak_from_this();
      // An actual late enabled-domain ACK must remain true here. Its controller
      // records that physical ownership before applying the logical deadline.
      const bool accepted=hooks_.send(call->session,call->method,call->params,call->deadline,[weak,id](bool ok,Dict result,const std::string& error){if(const auto self=weak.lock())self->Finish(id,ok,result,error);});
      if(!accepted)Finish(id,false,nullptr,"Actual native coverage target dispatch was rejected");
    }
  }
  Hooks hooks_;std::map<uint64_t,std::shared_ptr<Call>> calls_;std::deque<uint64_t> queue_;uint64_t generation_=0;size_t backend_=0,bytes_=0;bool closed_=false,pumping_=false;
};
} // namespace suitecut
