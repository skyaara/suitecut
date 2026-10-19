// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"
namespace suitecut {
// One bounded, genuine secondary Inspector session per admitted Page target.
// No command changes the primary Network agent. This collector admits only
// exact-ID Other/Image resources observed independently by both actual agents.
class NativePrivateResponseSession {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,const std::string&)>;
  using Send=std::function<void(const std::string&,Dict,double,Reply)>;
  using Ready=std::function<void(bool,const std::string&)>;
  NativePrivateResponseSession(Send send,std::function<double()> now,
      std::function<void()> wake,std::function<void(const std::string&)> fatal)
      :send_(std::move(send)),now_(std::move(now)),wake_(std::move(wake)),fatal_(std::move(fatal)){}
  ~NativePrivateResponseSession(){// Destruction is allowed only after its actual Page channel closes.
    *alive_=false;ActualClose();
  }
  bool Start(double deadline,Ready ready){
    if(started_||closed_||!std::isfinite(deadline)||deadline<=now_()||global_sessions_>=8)return false;
    started_=true;++global_sessions_;deadline_=deadline;ready_=std::move(ready);
    const auto alive=alive_;auto send=send_;
    send("Target.getTargetInfo",CefDictionaryValue::Create(),deadline,[this,alive](bool ok,const std::string&json){
      if(!*alive||closed_)return;
      if(detaching_||now_()>=deadline_){ActualClose("Private body initialization deadline exceeded");return;}
      const auto parsed=Parse(ok,json);const auto info=parsed?parsed->GetDictionary("targetInfo"):nullptr;
      if(!info||info->GetString("type")!="page"||!Identity(info,"targetId",target_)){Fail("Private body target identity unavailable");return;}
      auto params=CefDictionaryValue::Create();params->SetString("targetId",target_);params->SetBool("flatten",false);attaching_=true;
      const auto alive=alive_;auto send=send_;
      send("Target.attachToTarget",params,deadline_,[this,alive](bool ok,const std::string&json){
        if(!*alive||closed_)return;
        if(!ok&&(detaching_||now_()>=deadline_)){
          // Source deadline mapping may hide an accepted attach result. Keep
          // the reservation until the genuine parent channel closes; an
          // already-observed attached event still permits exact detach.
          Detach("Private body attachment original deadline exceeded");return;
        }
        const auto result=Parse(ok,json);
        std::string attached;
        if(!result||!Identity(result,"sessionId",attached,128)){Fail("Private body Inspector attachment failed");return;}
        if(!session_.empty()&&session_!=attached){Fail("Private body Inspector attachment identity mismatch");return;}
        session_=std::move(attached);attaching_=false;
        if(detaching_||now_()>=deadline_){Detach("Private body initialization deadline exceeded");return;}
        auto params=CefDictionaryValue::Create();params->SetInt("maxTotalBufferSize",8*1048576);
        params->SetInt("maxResourceBufferSize",2097152);params->SetBool("enableDurableMessages",true);
        if(!Inner("Network.enable",params,deadline_,[this,alive](bool ok,const std::string&){
          if(!*alive||closed_)return;
          if(!ok||now_()>=deadline_){Detach("Private body Network enable failed or expired");return;}
          enabled_=true;auto ready=std::move(ready_);if(ready)ready(true,"");
        }))Detach("Private body command capacity exceeded");
      });
    });
    if(*alive&&!closed_)wake_();
    return true;
  }
  // Called synchronously for genuine primary root-session Network events.
  // Nested frame/worker sessions are intentionally outside this first slice.
  void Primary(const std::string&method,Dict params){Observe(method,params,false);}
  // Returns true only for this collector's real nested channel messages.
  bool Event(const std::string&method,Dict params){
    if(closed_||!params)return false;
    if(method=="Target.attachedToTarget"&&attaching_){
      const auto info=params->GetDictionary("targetInfo");std::string observed;
      if(info&&info->GetString("targetId")==target_&&info->GetString("type")=="page"&&Identity(params,"sessionId",observed,128)){
        if(!session_.empty()&&session_!=observed){Fail("Conflicting private Inspector attachment event");return true;}
        session_=std::move(observed);if(detaching_)Detach(detach_error_);return true;
      }
    }
    if(session_.empty())return false;
    if(method=="Target.detachedFromTarget"&&params->GetString("sessionId")==session_){
      ActualClose("Private body Inspector session detached");return true;
    }
    if(method!="Target.receivedMessageFromTarget"||params->GetString("sessionId")!=session_)return false;
    if(params->GetString("targetId")!=target_||params->GetType("message")!=VTYPE_STRING){Detach("Private body nested target mismatch");return true;}
    const auto message=params->GetString("message").ToString();
    if(message.size()>2798300){Detach("Private body nested message bound exceeded");return true;}
    const auto value=Parse(true,message);if(!value){Detach("Private body nested message malformed");return true;}
    if(value->GetType("id")==VTYPE_INT){
      const auto found=pending_.find(value->GetInt("id"));if(found==pending_.end())return true;
      auto call=std::move(found->second);pending_.erase(found);--global_receipts_;
      const auto result=value->GetDictionary("result");const bool ok=result&&!value->HasKey("error")&&!call.expired&&now_()<call.deadline&&(!call.body||ValidBody(result));
      auto done=std::move(call.reply);
      if(done)done(ok,ok?JSON(result):call.expired||now_()>=call.deadline?"Private body original deadline exceeded":JSON(value));
      return true;
    }
    if(value->GetType("method")==VTYPE_STRING)Observe(value->GetString("method"),value->GetDictionary("params"),true);
    return true;
  }
  // The caller retains its original body-read owner/deadline. A callback is
  // retired only by a real nested reply or actual channel destruction.
  bool Read(const std::string&request,double deadline,Reply reply){
    const auto alive=alive_;Tick(now_());
    if(!*alive)return false;
    if(closed_||!enabled_||detaching_||!std::isfinite(deadline)||deadline<=now_())return false;
    const auto found=records_.find(request);
    if(found==records_.end()||!found->second.primary.seen||found->second.redirected||found->second.failed||!found->second.primary_finished)return false;
    if(found->second.other.seen&&!Match(found->second))return false;
    if(pending_.size()+waiting_.size()>=8||global_receipts_+global_waiting_>=32||next_wait_==2147483647)return false;
    if(Match(found->second)&&found->second.private_finished){
      auto params=CefDictionaryValue::Create();params->SetString("requestId",request);
      return Inner("Network.getResponseBody",params,deadline,std::move(reply));
    }
    // The primary terminal event can precede delivery of the same genuine
    // terminal event on the private channel. Wait only for that exact record,
    // under the caller's original deadline; no command has yet been sent.
    waiting_.emplace(++next_wait_,Wait{request,deadline,std::move(reply)});++global_waiting_;wake_();return true;
  }

  void Tick(double now){
    if(closed_)return;
    SweepMetadata(now);
    bool expired_call=false;for(auto&[id,call]:pending_)if(now>=call.deadline){call.expired=true;expired_call=true;}
    const auto alive=alive_;
    if((!enabled_&&started_&&now>=deadline_)||expired_call)Detach("Private body original deadline exceeded");
    if(*alive)MaybeReads();
  }
  // Invoke at actual parent Page OnBeforeClose, or at actual detach above.
  void ActualClose(const std::string&error="Private body Page channel closed"){
    if(closed_)return;
    closed_=true;enabled_=false;
    if(started_){--global_sessions_;started_=false;}
    global_records_-=records_.size();records_.clear();global_receipts_-=pending_.size();global_waiting_-=waiting_.size();
    auto calls=std::move(pending_);auto waits=std::move(waiting_);auto ready=std::move(ready_);
    if(ready)ready(false,error);
    for(auto&[id,call]:calls)if(call.reply)call.reply(false,call.expired?"Private body original deadline exceeded":error);
    for(auto&[id,wait]:waits)if(wait.reply)wait.reply(false,error);
  }
  bool NeedsTick()const{return !closed_&&((started_&&!enabled_)||!pending_.empty()||!waiting_.empty());}
  size_t physical()const{return pending_.size();}size_t records()const{return records_.size();}size_t waiting()const{return waiting_.size();}
  const std::string& target()const{return target_;}const std::string& session()const{return session_;}
  bool ready()const{return enabled_&&!detaching_&&!closed_;}
  bool parent_close_requested()const{return fatal_requested_;}
  static size_t Physical(){return global_receipts_;}static size_t Waiting(){return global_waiting_;}static size_t Sessions(){return global_sessions_;}static size_t Records(){return global_records_;}
 private:
  struct Side{std::string loader,frame,type;bool seen=false;};
  struct Record{Side primary,other;double deadline=0;bool redirected=false,failed=false,primary_finished=false,private_finished=false;};
  struct Wait{std::string request;double deadline;Reply reply;};
  struct Call{Reply reply;double deadline;bool expired=false,body=false;};
  static bool Identity(Dict value,const std::string&key,std::string&out,size_t max=256){
    if(!value||value->GetType(key)!=VTYPE_STRING)return false;
    auto text=value->GetString(key).ToString();if(text.empty()||text.size()>max)return false;out=std::move(text);return true;
  }
  static Dict Parse(bool ok,const std::string&text){if(!ok)return nullptr;const auto value=CefParseJSON(text,JSON_PARSER_RFC);return value?value->GetDictionary():nullptr;}
  static std::string JSON(Dict value){if(!value)return "{}";auto wrapper=CefValue::Create();wrapper->SetDictionary(value->Copy(false));return CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).ToString();}
  static bool ValidBody(Dict value){
    if(value->GetType("body")!=VTYPE_STRING||value->GetType("base64Encoded")!=VTYPE_BOOL)return false;
    const auto body=value->GetString("body").ToString();
    if(!value->GetBool("base64Encoded"))return body.size()<=2097152;
    if(body.size()>2796204||body.size()%4)return false;
    if(body.empty())return true;
    const auto decoded=CefBase64Decode(body);if(!decoded||decoded->GetSize()>2097152)return false;
    std::vector<unsigned char>bytes(decoded->GetSize());decoded->GetData(bytes.data(),bytes.size(),0);
    return CefBase64Encode(bytes.data(),bytes.size()).ToString()==body;
  }
  static bool Match(const Record&record){return !record.redirected&&!record.failed&&record.primary.seen&&record.other.seen&&record.primary.loader==record.other.loader&&record.primary.frame==record.other.frame&&record.primary.type==record.other.type;}
  void SweepMetadata(double now){
    std::vector<std::string>expired;for(const auto&[key,record]:records_)if(now>=record.deadline)expired.push_back(key);
    for(const auto&key:expired){records_.erase(key);--global_records_;}
  }
  void Observe(const std::string&method,Dict params,bool other){
    if(closed_||!params)return;
    SweepMetadata(now_());
    std::string request;if(!Identity(params,"requestId",request))return;
    auto found=records_.find(request);
    if(method=="Network.requestWillBeSent"){
      // A genuine shared-ID redirect or repeat remains disqualifying even if
      // Chromium changes or omits its later optional resource type.
      if(found!=records_.end()&&(params->HasKey("redirectResponse")||(other?found->second.other.seen:found->second.primary.seen)))found->second.redirected=true;
      const auto type=params->GetString("type").ToString();if(type!="Other"&&type!="Image")return;
      std::string loader,frame;if(!Identity(params,"loaderId",loader)||!Identity(params,"frameId",frame))return;
      if(found==records_.end()){
        if(records_.size()>=128||global_records_>=512)return;
        Record record;record.deadline=now_()+30000;found=records_.emplace(request,std::move(record)).first;++global_records_;
      }
      auto&record=found->second;auto&side=other?record.other:record.primary;
      if(side.seen||params->HasKey("redirectResponse"))record.redirected=true;
      side.loader=std::move(loader);side.frame=std::move(frame);side.type=type;side.seen=true;
    }else if(found!=records_.end()){
      if(method=="Network.loadingFinished"){if(other)found->second.private_finished=true;else found->second.primary_finished=true;}
      else if(method=="Network.loadingFailed")found->second.failed=true;
    }
    MaybeReads();
  }
  void MaybeReads(){
    const auto alive=alive_;
    for(auto it=waiting_.begin();it!=waiting_.end();){
      const auto record=records_.find(it->second.request);const auto now=now_();
      const bool invalid=closed_||detaching_||record==records_.end()||record->second.redirected||record->second.failed||(record->second.other.seen&&!Match(record->second));
      const bool expired=now>=it->second.deadline;
      const bool ready=!invalid&&Match(record->second)&&record->second.private_finished;
      if(!invalid&&!expired&&!ready){++it;continue;}
      auto wait=std::move(it->second);waiting_.erase(it);--global_waiting_;
      if(invalid||expired){if(wait.reply)wait.reply(false,expired?"Private body original deadline exceeded":"Private body matching observation unavailable");}
      else{
        auto params=CefDictionaryValue::Create();params->SetString("requestId",wait.request);
        if(!Inner("Network.getResponseBody",params,wait.deadline,wait.reply)&&wait.reply)wait.reply(false,"Private body command admission unavailable");
      }
      if(!*alive)return;
      // A synchronous callback can mutate the native wait map.
      it=waiting_.begin();
    }
  }
  bool Inner(const std::string&method,Dict params,double deadline,Reply reply){
    if(closed_||session_.empty()||detaching_||pending_.size()+waiting_.size()>=8||global_receipts_+global_waiting_>=32||next_==2147483647)return false;
    const int id=++next_;auto message=CefDictionaryValue::Create();message->SetInt("id",id);message->SetString("method",method);message->SetDictionary("params",params->Copy(false));
    auto envelope=CefDictionaryValue::Create();envelope->SetString("sessionId",session_);envelope->SetString("message",JSON(message));
    pending_.emplace(id,Call{std::move(reply),deadline,false,method=="Network.getResponseBody"});++global_receipts_;
    const auto alive=alive_;auto send=send_;
    send("Target.sendMessageToTarget",envelope,deadline,[this,alive,id](bool ok,const std::string&error){
      if(!*alive||closed_||ok)return;
      // A timely outer rejection proves non-delivery. Deadline-mapped
      // failures are ambiguous: the nested command may already be accepted.
      // A successful outer ACK never retires the genuine inner receipt.
      const auto found=pending_.find(id);if(found==pending_.end())return;
      if(detaching_||now_()>=found->second.deadline){
        found->second.expired=true;Detach("Private body original deadline exceeded");return;
      }
      auto done=std::move(found->second.reply);pending_.erase(found);--global_receipts_;if(done)done(false,error);
    });
    if(*alive&&!closed_)wake_();
    return true;
  }
  void Fail(const std::string&error){if(session_.empty())ActualClose(error);else Detach(error);}
  void Detach(const std::string&error){
    if(closed_)return;
    if(!detaching_){detaching_=true;detach_error_=error.substr(0,512);}
    auto ready=std::move(ready_);const auto alive=alive_;const auto reason=detach_error_;
    if(session_.empty()){
      // A timed-out attach can have been accepted even when Source's deadline
      // result omitted its session ID. Only actual parent close is a fence.
      if(!fatal_requested_){fatal_requested_=true;auto fatal=fatal_;fatal(reason);}
      if(*alive&&ready)ready(false,reason);
      return;
    }
    if(detach_sent_){if(ready)ready(false,reason);return;}
    detach_sent_=true;auto params=CefDictionaryValue::Create();params->SetString("sessionId",session_);
    auto send=send_;
    send("Target.detachFromTarget",params,0,[this,alive,reason](bool ok,const std::string&){
      if(!*alive||closed_)return;
      if(!ok&&!fatal_requested_){fatal_requested_=true;auto fatal=fatal_;fatal(reason);}
      // Actual Target.detachedFromTarget or Page OnBeforeClose owns cleanup.
    });
    if(*alive&&ready)ready(false,reason);
  }

  Send send_;std::function<double()>now_;std::function<void()>wake_;std::function<void(const std::string&)>fatal_;
  bool started_=false,closed_=false,enabled_=false,detaching_=false,attaching_=false,detach_sent_=false,fatal_requested_=false;int next_=0,next_wait_=0;double deadline_=0;
  std::string target_,session_,detach_error_;Ready ready_;std::map<std::string,Record>records_;std::map<int,Call>pending_;std::map<int,Wait>waiting_;
  std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
  inline static size_t global_sessions_=0,global_receipts_=0,global_records_=0,global_waiting_=0;
};
} // namespace suitecut
