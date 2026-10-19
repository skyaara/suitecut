// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <initializer_list>
#include <memory>
#include <limits>
#include <string>
#include <vector>
#include "include/cef_parser.h"
#include "include/cef_values.h"
namespace suitecut {
// Exact optional backend receipts; ordinary CDP success is never promoted to
// renderer delivery. Accepted native closures survive logical cancellation.
class NativeWidgetMouse : public std::enable_shared_from_this<NativeWidgetMouse> {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Reply=std::function<void(bool,const std::string&)>;
  using Send=std::function<void(const std::string&,const std::string&,Dict,Reply,double)>;
  struct Pin {std::string frame,loader,session,identity;uint64_t generation=0;
    bool operator==(const Pin&)const=default;};
  struct Hooks {Send send;std::function<bool(const Pin&)>current;std::function<void(const std::string&)>fatal;std::function<double()>now;};
  explicit NativeWidgetMouse(Hooks hooks):hooks_(std::move(hooks)){}
  void Probe(Reply done){
    if(probed_){done(available_,available_?"":"Original-widget receipts require the custom CEF backend");return;}
    if(pending_){done(false,"Native widget probe is already pending");return;}
    pending_=true;
    hooks_.send("","Input.getMouseWidgetReceiptCapabilities",CefDictionaryValue::Create(),
      [self=shared_from_this(),done=std::move(done)](bool ok,const std::string&json){
        self->pending_=false;self->probed_=true;const auto value=Parse(json);
        self->available_=!self->closed_&&ok&&Keys(value,{"schemaVersion","protocol","maxLeases","maxPending"})&&
          value->GetType("schemaVersion")==VTYPE_INT&&value->GetInt("schemaVersion")==1&&
          value->GetType("protocol")==VTYPE_STRING&&value->GetString("protocol")=="suitecut-widget-mouse-v1"&&
          value->GetType("maxLeases")==VTYPE_INT&&value->GetInt("maxLeases")==64&&
          value->GetType("maxPending")==VTYPE_INT&&value->GetInt("maxPending")==64;
        done(self->available_,self->available_?"":"Original-widget receipts require the custom CEF backend");
      },hooks_.now()+30000);
  }
  bool available()const{return available_&&!closed_;}
  bool physical()const{return pending_;}
  bool held()const{return buttons_!=0;}
  bool owns(int owner)const{return owner_==owner;}
  void Dispatch(int owner,const Pin&pin,Dict event,double deadline,Reply done){
    if(!available()||pending_||finishing_||!event||!hooks_.current(pin)){
      done(false,!available()?"Original-widget receipts require the custom CEF backend":"Native widget mouse owner is unavailable or busy");return;}
    if(!lease_.empty()&&pin_!=pin){Fail("Original mouse document changed while a native widget was leased");done(false,"Original mouse document changed");return;}
    owner_=owner;pin_=pin;event_=event->Copy(false);deadline_=deadline;dispatch_done_=std::move(done);
    if(!lease_.empty()){SendEvent();return;}
    // Ordinary clicks can leave the legacy drag interceptor enabled. Retire
    // that policy through its actual ACK before admitting raw widget input.
    auto intercept=CefDictionaryValue::Create();intercept->SetBool("enabled",false);pending_=true;
    hooks_.send(pin.session,"Input.setInterceptDrags",intercept,[self=shared_from_this()](bool ok,const std::string&){
      self->pending_=false;
      if(self->closed_){self->DispatchDone(false,"Native widget channel closed");self->FinishDone(false,"Native widget channel closed");return;}
      if(!ok){self->Fail("Raw original-widget input could not retire the legacy drag interceptor");self->DispatchDone(false,"Native drag interceptor policy failed");return;}
      if(self->finishing_){self->DispatchDone(false,"Native widget operation canceled");self->PumpFinish();return;}
      self->Acquire();
    },deadline);
  }

  // Called before public completion. A canceled down/press is released only
  // through the same original lease, after its real pending receipt arrives.
  void Finish(int owner,bool cancel,Reply done){
    if(owner_!=owner){done(true,"");return;}
    if(finishing_){done(false,"Native widget completion is already pending");return;}
    finishing_=true;cancel_=cancel;finish_done_=std::move(done);cleanup_deadline_=hooks_.now()+30000;
    PumpFinish();
  }
  void ChannelClosed(){closed_=true;available_=false;lease_.clear();document_.clear();buttons_=0;
    // Actual Source Inspector closure owns callback retirement. Do not clear a
    // pending flag/closure until its real transport callback is retired.
    if(!pending_){DispatchDone(false,"Native widget channel closed");FinishDone(false,"Native widget channel closed");}}
 private:
  static Dict Parse(const std::string&json){if(json.size()>65536)return nullptr;const auto value=CefParseJSON(json,JSON_PARSER_RFC);return value&&value->GetType()==VTYPE_DICTIONARY?value->GetDictionary():nullptr;}
  static bool Keys(Dict value,std::initializer_list<const char*>names){if(!value||value->GetSize()!=names.size())return false;for(const auto*name:names)if(!value->HasKey(name))return false;return true;}
  void Fail(const std::string&error){available_=false;if(!closed_)hooks_.fatal(error);}
  void DispatchDone(bool ok,const std::string&error){auto done=std::move(dispatch_done_);event_=nullptr;if(done)done(ok,error);}
  void FinishDone(bool ok,const std::string&error){finishing_=false;cancel_=false;auto done=std::move(finish_done_);if(done)done(ok,error);}
  bool Current(){return !closed_&&hooks_.current(pin_);}
  void Acquire(){
    auto params=CefDictionaryValue::Create();params->SetString("frameId",pin_.frame);params->SetString("loaderId",pin_.loader);
    pending_=true;
    hooks_.send(pin_.session,"Input.acquireMouseWidget",params,[self=shared_from_this()](bool ok,const std::string&json){
      self->pending_=false;if(self->closed_){self->DispatchDone(false,"Native widget channel closed");self->FinishDone(false,"Native widget channel closed");return;}
      const auto value=Parse(json);
      if(!ok||!Keys(value,{"lease","documentToken"})||value->GetType("lease")!=VTYPE_STRING||value->GetType("documentToken")!=VTYPE_STRING||
         value->GetString("lease").empty()||value->GetString("lease").length()>128||value->GetString("documentToken").empty()||value->GetString("documentToken").length()>128){
        self->Fail("Native widget lease acquisition failed or returned malformed identity");self->DispatchDone(false,"Native widget lease acquisition failed");return;}
      self->lease_=value->GetString("lease");self->document_=value->GetString("documentToken");self->sequence_=0;
      if(self->finishing_){self->DispatchDone(false,"Native widget operation canceled");self->PumpFinish();return;}
      self->SendEvent();
    },deadline_);
  }
  void SendEvent(){
    if(!Current()||lease_.empty()){Fail("Original mouse document is no longer current");DispatchDone(false,"Original mouse document is no longer current");return;}
    if(finishing_){DispatchDone(false,"Native widget operation canceled");PumpFinish();return;}
    if(sequence_==std::numeric_limits<int>::max()){Fail("Native widget sequence exhausted");DispatchDone(false,"Native widget sequence exhausted");return;}
    auto params=CefDictionaryValue::Create();params->SetString("lease",lease_);params->SetInt("sequence",++sequence_);
    params->SetString("type",event_->GetString("type"));params->SetString("button",event_->HasKey("button")?event_->GetString("button").ToString():"none");
    for(const auto*key:{"x","y"})params->SetDouble(key,event_->GetType(key)==VTYPE_INT?event_->GetInt(key):event_->GetDouble(key));
    params->SetInt("modifiers",event_->GetInt("modifiers"));params->SetInt("clickCount",event_->GetInt("clickCount"));
    last_x_=params->GetDouble("x");last_y_=params->GetDouble("y");
    const auto type=params->GetString("type").ToString(),button=params->GetString("button").ToString();
    const int bit=button=="left"?1:button=="right"?2:button=="middle"?4:0;
    const int next=type=="mousePressed"?buttons_|bit:type=="mouseReleased"?buttons_&~bit:buttons_;
    SendReceipt(params,type,next,false);
  }
  void SendReceipt(Dict params,const std::string&type,int next,bool cleanup){
    pending_=true;const int sequence=sequence_;
    hooks_.send(pin_.session,"Input.dispatchMouseEventToWidget",params,
      [self=shared_from_this(),sequence,type,next,cleanup](bool ok,const std::string&json){
        self->pending_=false;const auto result=Parse(json);const auto value=result?result->GetDictionary("receipt"):nullptr;
        bool valid=ok&&Keys(result,{"receipt"})&&Keys(value,{"lease","sequence","documentToken","outcome","ackObserved","ackSource","ackState"})&&
          value->GetType("lease")==VTYPE_STRING&&value->GetString("lease")==self->lease_&&value->GetType("sequence")==VTYPE_INT&&value->GetInt("sequence")==sequence&&
          value->GetType("documentToken")==VTYPE_STRING&&value->GetString("documentToken")==self->document_&&value->GetType("outcome")==VTYPE_STRING&&
          value->GetType("ackObserved")==VTYPE_BOOL&&value->GetType("ackSource")==VTYPE_INT&&value->GetType("ackState")==VTYPE_INT;
        const auto outcome=valid?value->GetString("outcome").ToString():"";
        const int source=valid?value->GetInt("ackSource"):-1,state=valid?value->GetInt("ackState"):-1;
        const bool renderer=valid&&outcome=="rendererAck"&&value->GetBool("ackObserved")&&(source==1||source==2)&&state>=0&&state<=6;
        const bool nonblocking=valid&&outcome=="browserAck"&&value->GetBool("ackObserved")&&source==3&&state>=0&&state<=6&&type=="mouseMoved";
        if(renderer)self->buttons_=next;
        if(self->closed_){self->DispatchDone(false,"Native widget channel closed");self->FinishDone(false,"Native widget channel closed");return;}
        if((!renderer&&!nonblocking)||!self->Current()){
          self->Fail("Original-widget mouse receipt did not establish delivery to the original live document");
          self->DispatchDone(false,"Original-widget mouse delivery failed");return;
        }
        if(cleanup){self->PumpFinish();return;}
        self->DispatchDone(true,"");if(self->finishing_)self->PumpFinish();
      },cleanup?cleanup_deadline_:deadline_);
  }
  void PumpFinish(){
    if(!finishing_||pending_)return;
    if(closed_){FinishDone(false,"Native widget channel closed");return;}
    if(lease_.empty()){FinishDone(true,"");return;}
    if(!Current()){Fail("Original mouse widget cannot be released after document loss");return;}
    if(buttons_){
      if(!cancel_){FinishDone(true,"");return;}
      const std::string button=buttons_&1?"left":buttons_&2?"right":"middle";const int bit=buttons_&1?1:buttons_&2?2:4;
      if(sequence_==std::numeric_limits<int>::max()){Fail("Native widget sequence exhausted during release");return;}
      auto params=CefDictionaryValue::Create();params->SetString("lease",lease_);params->SetInt("sequence",++sequence_);params->SetString("type","mouseReleased");params->SetString("button",button);
      params->SetDouble("x",last_x_);params->SetDouble("y",last_y_);params->SetInt("clickCount",1);params->SetInt("modifiers",0);
      SendReceipt(params,"mouseReleased",buttons_&~bit,true);return;
    }
    auto params=CefDictionaryValue::Create();params->SetString("lease",lease_);pending_=true;
    hooks_.send(pin_.session,"Input.releaseMouseWidget",params,[self=shared_from_this()](bool ok,const std::string&json){
      self->pending_=false;if(self->closed_){self->FinishDone(false,"Native widget channel closed");return;}
      const auto value=Parse(json);if(!ok||!Keys(value,{})){self->Fail("Native original-widget lease release was not acknowledged");return;}
      self->lease_.clear();self->document_.clear();self->sequence_=0;self->FinishDone(true,"");
    },cleanup_deadline_);
  }
  Hooks hooks_;Pin pin_;std::string lease_,document_;int owner_=0,sequence_=0,buttons_=0;
  bool probed_=false,available_=false,pending_=false,closed_=false,finishing_=false,cancel_=false;
  double deadline_=0,cleanup_deadline_=0,last_x_=0,last_y_=0;Dict event_;Reply dispatch_done_,finish_done_;
};
} // namespace suitecut
