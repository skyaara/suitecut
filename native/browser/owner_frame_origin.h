// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace suitecut {
// An exact Inspector resolve error is not sufficient evidence of no owner:
// Chromium also uses it for an invalid execution context. A genuinely pinned
// unique context and its actual Document are validated on both sides of the
// resolve before the missing-live-frame alternative is accepted. Native callers
// retain the original operation deadline, group and physical command leases.
class OwnerFrameOrigin:public std::enable_shared_from_this<OwnerFrameOrigin>{
 public:
  enum class Method{UtilityBefore,DescribeBefore,Resolve,UtilityAfter,DescribeAfter,DescribeRetained};
  struct Reply {
   bool ok=false,node=false;int code=0,backend=0,type=0;
   std::string object,error;
  };
  struct Result{bool ok=false,no_owner=false;std::string object,error;};
  using Send=std::function<void(Method,const std::string&,std::function<void(Reply)>)>;
  using Current=std::function<bool()>;using Done=std::function<void(Result)>;
  OwnerFrameOrigin(Send send,Current current,Done done,int backend,int type)
   :send_(std::move(send)),current_(std::move(current)),done_(std::move(done)),backend_(backend),type_(type){}
  void Start(){if(started_)return;started_=true;if(backend_<=0||type_<=0||type_>12){Finish({false,false,{},"Native element owner retained node schema invalid"});return;}Call(Method::UtilityBefore,{});}
 private:
  void Call(Method method,const std::string&object){
   if(finished_)return;
   if(!current_()){Finish({false,false,{},"Native element owner execution context changed"});return;}
   pending_=true;const auto sequence=++sequence_;auto self=shared_from_this();
   send_(method,object,[self,method,sequence](Reply reply){
    if(self->finished_||!self->pending_||self->sequence_!=sequence)return;
    self->pending_=false;
    if(!self->current_()){self->Finish({false,false,{},"Native element owner execution context changed"});return;}
    self->Acknowledged(method,std::move(reply));
   });
  }
  void Acknowledged(Method method,Reply reply){
   if(!reply.ok){
    // Exact source-attested Inspector guard, never a substring/caption match.
    if(method==Method::Resolve&&reply.code==-32000&&reply.error=="Node with given id does not belong to the document"){
     Call(Method::UtilityAfter,{});return;
    }
    if(reply.error.empty())reply.error="Native element owner protocol validation failed";
    if(reply.error.size()>4096)reply.error="Native element owner protocol diagnostic limit exceeded";
    Finish({false,false,{},std::move(reply.error)});return;
   }
   if(method==Method::UtilityBefore||method==Method::UtilityAfter||method==Method::Resolve){
    if(!reply.node||reply.object.empty()||reply.object.size()>4096){Finish({false,false,{},"Native element owner actual DOM wrapper unavailable"});return;}
    if(method==Method::Resolve){Finish({true,false,std::move(reply.object),{}});return;}
    Call(method==Method::UtilityBefore?Method::DescribeBefore:Method::DescribeAfter,reply.object);return;
   }
   if(method==Method::DescribeBefore){
    if(reply.type!=9||reply.backend<=0){Finish({false,false,{},"Native element owner utility Document identity unavailable"});return;}
    utility_backend_=reply.backend;Call(Method::Resolve,{});return;
   }
   if(method==Method::DescribeAfter){
    if(reply.type!=9||reply.backend!=utility_backend_){Finish({false,false,{},"Native element owner utility Document identity changed"});return;}
    Call(Method::DescribeRetained,{});return;
   }
   if(reply.backend!=backend_||reply.type!=type_){Finish({false,false,{},"Native element owner retained node identity changed"});return;}
   Finish({true,true,{},{}});
  }
  void Finish(Result result){if(finished_)return;finished_=true;auto done=std::move(done_);done(std::move(result));}
  Send send_;Current current_;Done done_;int backend_=0,type_=0,utility_backend_=0;
  unsigned sequence_=0;bool started_=false,finished_=false,pending_=false;
};
} // namespace suitecut
