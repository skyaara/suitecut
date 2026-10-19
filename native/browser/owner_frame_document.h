// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace suitecut {
// One real peer-document transaction owns its private object group until the
// renderer acknowledges release or its actual channel dies. Origin cancellation
// only stops new reads; it never abandons a possibly-created remote group.
class OwnerFrameDocument:public std::enable_shared_from_this<OwnerFrameDocument>{
 public:
  enum class Method{Isolate,Document,Describe,Release};
  struct Reply{bool ok=false;std::string isolate,object;int backend=0;std::string error;Reply(bool success=false,std::string renderer={},std::string node={},int identity=0,std::string cause={}):ok(success),isolate(std::move(renderer)),object(std::move(node)),backend(identity),error(std::move(cause)){} };
  struct Result{bool ok=false;std::string isolate;int backend=0;std::string error;};
  using Send=std::function<void(Method,const std::string&,std::function<void(Reply)>)>;
  using Current=std::function<bool()>;using Done=std::function<void(Result)>;
  using Fatal=std::function<void(const std::string&)>;
  OwnerFrameDocument(Send send,Current origin,Current peer,Done done,Fatal fatal,std::string wanted,double deadline)
   :send_(std::move(send)),origin_(std::move(origin)),peer_(std::move(peer)),done_(std::move(done)),fatal_(std::move(fatal)),wanted_(std::move(wanted)),deadline_(deadline){}
  void Start(){if(started_)return;started_=true;if(!origin_()||!peer_()){Finish(false);return;}Call(Method::Isolate,{});}
  void ChannelClosed(){if(!finished_)Finish(false);}
  void Tick(double now){if(!finished_&&!failed_&&now>=deadline_){failed_=true;fatal_("Native owner document physical settlement deadline exceeded");}}
  bool Finished()const{return finished_;}bool GroupOwned()const{return group_;}bool Pending()const{return pending_;}
 private:
  void Call(Method method,const std::string&object){
   if(finished_)return;
   if(method!=Method::Release&&(!origin_()||!peer_())){Cleanup(false);return;}
   if(method==Method::Document)group_=true;
   pending_=true;const auto sequence=++sequence_;auto self=shared_from_this();
   send_(method,object,[self,method,sequence](Reply reply){if(self->finished_||!self->pending_||self->sequence_!=sequence)return;self->pending_=false;self->Acknowledged(method,std::move(reply));});
  }
  void Acknowledged(Method method,Reply reply){
   if(method==Method::Release){if(!reply.ok){if(!failed_){failed_=true;fatal_("Native owner document object-group release failed");}return;}group_=false;Finish(success_&&origin_()&&peer_());return;}
   if(!reply.ok||!origin_()||!peer_()){error_=reply.error.empty()?"Native owner document protocol read failed":std::move(reply.error);if(error_.size()>4096)error_="Native owner document protocol diagnostic limit exceeded";Cleanup(false);return;}
   if(method==Method::Isolate){if(reply.isolate.empty()||reply.isolate.size()>256){Finish(false);return;}isolate_=std::move(reply.isolate);if(isolate_!=wanted_){Finish(true);return;}Call(Method::Document,{});return;}
   if(method==Method::Document){if(reply.object.empty()||reply.object.size()>4096){Cleanup(false);return;}Call(Method::Describe,reply.object);return;}
   if(method==Method::Describe){if(reply.backend<=0){Cleanup(false);return;}backend_=reply.backend;Cleanup(true);}
  }
  void Cleanup(bool success){success_=success;if(group_){Call(Method::Release,{});return;}Finish(success);}
  void Finish(bool success){if(finished_)return;const auto keepalive=shared_from_this();finished_=true;pending_=false;group_=false;auto done=std::move(done_);done({success,std::move(isolate_),backend_,std::move(error_)});}
  Send send_;Current origin_,peer_;Done done_;Fatal fatal_;std::string wanted_,isolate_,error_;double deadline_;unsigned sequence_=0;
  int backend_=0;bool started_=false,finished_=false,pending_=false,group_=false,success_=false,failed_=false;
};
} // namespace suitecut
