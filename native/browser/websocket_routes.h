// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <charconv>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <tuple>
#include <vector>
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "network_glob.h"
#include "route_callback_leases.h"
#include "websocket_payloads.h"

namespace suitecut {
// UI actor policy for real renderer targets. SDK subscriptions are resident;
// issued host callbacks and accepted protocol calls remain physical work.
class NativeWebSocketRoutes {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Result=std::function<void(bool,const std::string&)>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  using Send=std::function<void(const std::string&,Dict,double,Result)>;
  using Emit=std::function<bool(Dict,std::vector<uint8_t>)>;
  using Fatal=std::function<void(const std::string&)>;
  NativeWebSocketRoutes(std::function<double()> now,std::function<void()> wake,Emit emit):now_(std::move(now)),wake_(std::move(wake)),emit_(std::move(emit)),payloads_(now_){}
  ~NativeWebSocketRoutes(){*alive_=false;for(auto&[key,r]:registrations_)GlobalRouteCallbackLeases().Close(r->scope);}
  bool HasPolicy(int context,int page)const{for(const auto&[key,r]:registrations_)if(r->active&&r->context==context&&(r->page==0||r->page==page))return true;return false;}
  bool Attach(int context,int page,const std::string&owner,Send send,Fatal fatal,Result ready){
    if(context<1||page<0||owner.empty()||owner.size()>256||targets_.contains(owner)||targets_.size()>=2048||serial_==UINT64_MAX||!send||!fatal||!ready)return false;
    auto target=std::make_shared<Target>();target->context=context;target->page=page;target->owner=owner;target->token=std::to_string(++serial_);target->send=std::move(send);target->fatal=std::move(fatal);
    targets_[owner]=target;tokens_[target->token]=target;target->waiters.push_back(std::move(ready));Reconcile(target);return true;
  }
  void Detach(const std::string&owner){
    const auto found=targets_.find(owner);if(found==targets_.end())return;auto target=found->second;targets_.erase(found);tokens_.erase(target->token);target->live=false;
    auto waiting=std::move(target->waiters);for(const auto&done:waiting)done(false,"Actual native WebSocket target channel closed");
    for(auto&[id,socket]:target->sockets)for(const auto&entry:socket.invocations)GlobalRouteCallbackLeases().Revoke(std::get<2>(entry),std::get<0>(entry));
    target->sockets.clear();
  }
  void DetachPrefix(const std::string&prefix){std::vector<std::string>owners;for(const auto&[owner,target]:targets_)if(owner.starts_with(prefix))owners.push_back(owner);for(const auto&owner:owners)Detach(owner);}
  void Closed(int context){
    payloads_.Closed(context);
    std::vector<std::string>owners;for(const auto&[owner,target]:targets_)if(target->context==context)owners.push_back(owner);for(const auto&owner:owners)Detach(owner);
    for(auto it=registrations_.begin();it!=registrations_.end();)if(it->second->context==context){it->second->active=false;GlobalRouteCallbackLeases().Close(it->second->scope);it=registrations_.erase(it);}else ++it;
  }
  void PageClosed(int context,int page){
    payloads_.Closed(context,page);
    for(auto it=registrations_.begin();it!=registrations_.end();)if(it->second->context==context&&it->second->page==page){it->second->active=false;GlobalRouteCallbackLeases().Close(it->second->scope);it=registrations_.erase(it);}else ++it;
  }
  size_t Physical()const{return physical_;}
  bool NeedsTick()const{return !mutations_.empty()||!removals_.empty()||payloads_.Pending();}
  bool Owns(const std::string&owner)const{const auto it=targets_.find(owner);return it!=targets_.end()&&(it->second->enabled||it->second->configuring);}
  void Cancel(int id){
    const auto found=mutations_.find(id);if(found!=mutations_.end()){
      auto r=found->second;mutations_.erase(found);r->active=false;registrations_.erase(Key(r->context,r->page,r->id));GlobalRouteCallbackLeases().Close(r->scope);RetireRegistration(r);ReconcileScope(r->context,r->page);
    }
    removals_.erase(id);
  }
  void Tick(){
    payloads_.Tick();
    std::vector<int>expired;for(const auto&[id,r]:mutations_)if(r->deadline&&now_()>=r->deadline)expired.push_back(id);
    for(const auto id:expired)Cancel(id);
    std::vector<std::pair<int,bool>>done;
    for(const auto&[id,r]:removals_){bool settled=true;for(const auto&receipt:r.receipts)if(receipt&&!receipt->settled())settled=false;if(settled&&!r.controls)done.push_back({id,r.error.empty()});else if(r.deadline&&now_()>=r.deadline)done.push_back({id,false});}
    for(const auto&[id,ok]:done){auto it=removals_.find(id);if(it==removals_.end())continue;auto reply=std::move(it->second.reply);removals_.erase(it);reply(ok,nullptr,ok?"":"Native WebSocket removal cleanup failed or its original deadline exceeded");}
  }
  void Command(int id,int context,int page,Dict input,double deadline,Reply reply){
    if(context<1||page<0||!input||(deadline&&now_()>=deadline)){reply(false,nullptr,"Native WebSocket policy scope is closed or expired");return;}
    const auto action=input->GetString("action").ToString();
    if(action=="routeRegister"){Install(id,context,page,input,deadline,std::move(reply));return;}
    if(action=="routeUnregister"||action=="routeUnregisterAll"){Remove(id,context,page,input,deadline,std::move(reply));return;}
    if(action=="routePayloadBegin"||action=="routePayloadAppend"||action=="routePayloadCommit"||action=="routePayloadAbort"){
      payloads_.Command(context,page,input,deadline,[this,context,page](Dict metadata){return PayloadOwner(context,page,metadata);},
        [this,context,page](Dict metadata,double original,Reply done){
          const auto found=tokens_.find(metadata->GetString("routingToken"));auto target=found==tokens_.end()?nullptr:found->second.lock();
          if(!target||!PayloadOwner(context,page,metadata)){done(false,nullptr,"Native WebSocket staged payload owner changed");return;}
          SendDecision(target,metadata->GetString("requestId"),metadata->GetString("operation"),metadata,original,std::move(done));
        },std::move(reply));wake_();return;
    }
    if(action!="routePolicyCommand"){reply(false,nullptr,"Invalid native WebSocket policy action");return;}
    const auto token=input->GetString("routingToken").ToString(),request=input->GetString("requestId").ToString(),registration=input->GetString("registrationId").ToString();
    const auto entry=tokens_.find(token);auto target=entry==tokens_.end()?nullptr:entry->second.lock();
    if(!target||!target->live||target->retiring||target->context!=context||input->GetString("targetId").ToString()!=target->owner){reply(false,nullptr,"Native WebSocket target attachment token is stale");return;}
    const auto found=target->sockets.find(request);
    if(found==target->sockets.end()||!found->second.registration||!found->second.registration->active||found->second.registration->id!=registration||found->second.registration->page!=page){reply(false,nullptr,"Native WebSocket channel registration ownership differs");return;}
    const auto operation=input->GetString("operation").ToString();
    if(operation=="match"){
      if(!found->second.matching||(!input->HasKey("invalid")&&input->GetType("matched")!=VTYPE_BOOL)){reply(false,nullptr,"Native WebSocket URL match decision is stale or invalid");return;}
      auto&socket=found->second;socket.matching=false;
      if(input->GetBool("invalid")){SendDecision(target,request,"abort",nullptr,deadline,std::move(reply));return;}
      if(input->GetBool("matched")&&socket.registration->active){socket.selected=true;Publish(target,socket,"created",nullptr);}
      else Select(target,socket);
      reply(true,nullptr,"");return;
    }
    if(!found->second.selected){reply(false,nullptr,"Native WebSocket channel initial URL selection is pending");return;}
    SendDecision(target,request,operation,input,deadline,std::move(reply));
  }
  bool Event(const std::string&owner,Dict event){
    const auto found=targets_.find(owner);if(found==targets_.end()||!event)return false;auto target=found->second;
    if(!target->enabled&&!target->configuring)return false;
    const auto request=event->GetString("requestId").ToString(),stage=event->GetString("stage").ToString();
    if(request.empty()||request.size()>128||(stage!="created"&&stage!="message"&&stage!="closed")){Fail(target,"Invalid native routed WebSocket identity");return true;}
    if(stage=="created"){
      const auto url=event->GetString("url").ToString();
      if(target->sockets.size()>=128||target->sockets.contains(request)||url.size()>8192||!(url.starts_with("ws://")||url.starts_with("wss://"))){Fail(target,"Native WebSocket channel ownership capacity or URL rejected");return true;}
      Socket socket;socket.request=request;socket.url=url;socket.deadline=now_()+30000;
      for(const auto&[key,r]:registrations_)if(r->active&&r->context==target->context&&(r->page==0||r->page==target->page)&&(r->host||NativeURLGlobMatches(r->pattern,url)))socket.candidates.push_back(r);
      std::sort(socket.candidates.begin(),socket.candidates.end(),[](const auto&a,const auto&b){return a->page!=b->page?a->page>b->page:a->order>b->order;});
      auto it=target->sockets.emplace(request,std::move(socket)).first;Select(target,it->second);return true;
    }
    const auto item=target->sockets.find(request);if(item==target->sockets.end())return true;auto&socket=item->second;
    if(stage=="message"){
      if(!socket.selected){Fail(target,"Native WebSocket message arrived before route selection");return true;}
      const auto payload=event->GetString("payloadBase64").ToString(),message=event->GetString("messageId").ToString();const int opcode=event->GetInt("opcode");
      if(message.empty()||message.size()>32||payload.size()>2796204||(opcode!=1&&opcode!=2)||event->GetType("outgoing")!=VTYPE_BOOL){Fail(target,"Invalid native WebSocket routed message");return true;}
      Publish(target,socket,"message",event);return true;
    }
    if(socket.registration){for(const auto&entry:socket.invocations)GlobalRouteCallbackLeases().Revoke(std::get<2>(entry),std::get<0>(entry));socket.invocations.clear();}
    if(socket.registration&&socket.selected&&!socket.continued)Publish(target,socket,"closed",event);
    target->sockets.erase(item);Reconcile(target);return true;
  }
 private:
  struct Registration {std::string id,pattern;int context=0,page=0;bool host=false,active=true;uint64_t scope=0,order=0;double deadline=0;};
  struct Socket {std::string request,url;double deadline=0;std::vector<std::shared_ptr<Registration>>candidates;size_t next=0;std::shared_ptr<Registration>registration;bool matching=false,selected=false,continued=false;std::vector<std::tuple<std::string,uint64_t,uint64_t>>invocations;};
  struct Target {int context=0,page=0;std::string owner,token;Send send;Fatal fatal;bool live=true,enabled=false,configuring=false,retiring=false;std::vector<Result>waiters;std::map<std::string,Socket>sockets;};
  struct Removal {std::vector<std::shared_ptr<const NativeRouteCallbackReceipt>>receipts;double deadline=0;Reply reply;uint64_t serial=0;size_t controls=0;std::string error;};
  bool PayloadOwner(int context,int page,Dict input)const{
    const auto entry=tokens_.find(input->GetString("routingToken").ToString());auto target=entry==tokens_.end()?nullptr:entry->second.lock();
    if(!target||!target->live||target->retiring||target->context!=context||target->owner!=input->GetString("targetId").ToString())return false;
    const auto found=target->sockets.find(input->GetString("requestId"));
    return found!=target->sockets.end()&&found->second.selected&&found->second.registration&&found->second.registration->active&&found->second.registration->page==page&&found->second.registration->id==input->GetString("registrationId").ToString();
  }
  static std::string Key(int context,int page,const std::string&id){return std::to_string(context)+":"+std::to_string(page)+":"+id;}
  static bool Revision(Dict r){return r&&r->GetInt("revision")==1&&r->GetInt("maxSockets")==128&&r->GetInt("maxPendingMessages")==64&&r->GetInt("maxPendingBytes")==2097152;}
  bool Wants(const std::shared_ptr<Target>&target)const{return HasPolicy(target->context,target->page)||!target->sockets.empty();}
  void Fail(const std::shared_ptr<Target>&target,const std::string&error){if(!target->live||target->retiring)return;target->retiring=true;auto waiting=std::move(target->waiters);for(auto&done:waiting)done(false,error);target->fatal(error);}
  void Reconcile(const std::shared_ptr<Target>&target){
    if(!target->live||target->retiring||target->configuring)return;
    const bool wanted=Wants(target);
    if(target->enabled==wanted){auto waiting=std::move(target->waiters);for(auto&done:waiting)done(true,"");return;}
    target->configuring=true;auto params=CefDictionaryValue::Create();params->SetBool("enabled",wanted);++physical_;wake_();auto alive=alive_;
    target->send("Network.suiteCutWebSocketConfigureV1",params,now_()+30000,[this,alive,target,wanted](bool ok,const std::string&json){
      if(!*alive)return;
      --physical_;target->configuring=false;if(!target->live)return;
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto r=parsed?parsed->GetDictionary():nullptr;
      if(!ok||!Revision(r)){
        const bool unsupported=!ok&&r&&r->GetInt("code")==-32601;
        auto waiting=std::move(target->waiters);
        for(auto&done:waiting)done(false,unsupported?"Native WebSocket routing requires the verified SuiteCut Network extension revision 1 in a custom CEF build":"Native WebSocket target configuration failed");
        const bool not_dispatched=!ok&&(json=="Explicit WebSocket control and URL policy cannot own the same root target"||json=="Native network command capacity exceeded"||json=="Native network original command deadline exceeded"||json=="Native network command dispatch failed"||json=="Native context worker control capacity exceeded"||json=="CEF rejected native context worker command");
        if(!wanted||(!unsupported&&ok)){Fail(target,"Native WebSocket configuration or cleanup could not be confirmed");return;}
        // An actual method-not-found receipt proves stock CEF did not enable.
        if(!unsupported&&!not_dispatched)Fail(target,"Native WebSocket configuration transport state is uncertain");
        return;
      }
      target->enabled=wanted;
      if(!wanted){
        // The actual agent disable ACK retires routing ownership, including a
        // channel observed while disable was in flight; it does not prove
        // that an underlying continued transport has closed.
        for(auto&[request,socket]:target->sockets)if(socket.registration)for(const auto&entry:socket.invocations)GlobalRouteCallbackLeases().Revoke(std::get<2>(entry),std::get<0>(entry));
        target->sockets.clear();
      }
      Reconcile(target);
    });
  }
  void ReconcileScope(int context,int page){std::vector<std::shared_ptr<Target>>targets;for(const auto&[owner,t]:targets_)if(t->context==context&&(page==0||t->page==page))targets.push_back(t);for(const auto&t:targets)Reconcile(t);}
  static std::string ResolvePattern(const std::string&pattern,const std::string&base){
    if(base.empty()||pattern.empty()||pattern.starts_with("*")||pattern.find("://")!=std::string::npos)return pattern;
    std::string actual=base;
    if(actual.starts_with("https://"))actual.replace(0,8,"wss://");
    else if(actual.starts_with("http://"))actual.replace(0,7,"ws://");
    CefURLParts parts;if(!CefParseURL(actual,parts)||CefString(&parts.host).empty())return {};
    std::string marker="__suitecut_ws_glob__";while(pattern.find(marker)!=std::string::npos)marker+="_";
    std::string protected_pattern;std::vector<std::pair<std::string,char>>tokens;
    for(char ch:pattern){if(ch=='*'||ch=='{'||ch=='}'||ch==','||ch=='\\'){
        const auto token=marker+std::to_string(tokens.size())+"__";tokens.push_back({token,ch});protected_pattern+=token;
      }else protected_pattern+=ch;}
    CefString resolved;if(!CefResolveURL(actual,protected_pattern,resolved))return {};
    actual=resolved.ToString();for(const auto&[token,ch]:tokens){size_t offset=0;while((offset=actual.find(token,offset))!=std::string::npos){actual.replace(offset,token.size(),1,ch);++offset;}}
    return actual;
  }
  void Install(int id,int context,int page,Dict input,double deadline,Reply reply){
    const auto registration=input->GetString("registrationId").ToString(),authored_pattern=input->GetString("url").ToString();
    const auto pattern=ResolvePattern(authored_pattern,input->GetString("baseURL").ToString());const bool host=input->GetBool("hostMatcher");std::vector<std::string>expanded;
    if(input->GetType("registrationId")!=VTYPE_STRING||input->GetType("url")!=VTYPE_STRING||(input->HasKey("hostMatcher")&&input->GetType("hostMatcher")!=VTYPE_BOOL)||pattern.empty()||registration.empty()||registration.size()>128||registration.find('\0')!=std::string::npos||pattern.size()>8192||pattern.find('\0')!=std::string::npos||!NativeURLGlobExpand(pattern,expanded)||(host&&pattern!="**/*")||registrations_.size()>=4096||registrations_.contains(Key(context,page,registration))||mutations_.size()>=32||mutations_.contains(id)||serial_==UINT64_MAX){reply(false,nullptr,"Invalid or excessive native WebSocket registration");return;}
    const auto scope=GlobalRouteCallbackLeases().Scope();if(!scope){reply(false,nullptr,"Native WebSocket callback scope exhausted");return;}
    auto r=std::make_shared<Registration>();r->id=registration;r->pattern=pattern;r->context=context;r->page=page;r->host=host;r->scope=*scope;r->order=++serial_;r->deadline=deadline;
    registrations_[Key(context,page,registration)]=r;mutations_[id]=r;
    std::vector<std::shared_ptr<Target>>targets;for(const auto&[owner,t]:targets_)if(t->context==context&&(page==0||t->page==page))targets.push_back(t);
    if(targets.empty()){Cancel(id);reply(false,nullptr,"Actual native WebSocket policy target is not ready");return;}
    auto remaining=std::make_shared<size_t>(targets.size());auto failed=std::make_shared<std::string>();auto alive=alive_;
    for(const auto&t:targets){t->waiters.push_back([this,alive,id,r,remaining,failed,reply](bool ok,const std::string&error){
      if(!*alive)return;
      if(!ok&&failed->empty())*failed=error;
      if(--*remaining)return;
      if(!mutations_.contains(id)||!r->active){reply(false,nullptr,"Native WebSocket registration was canceled before actual configuration acknowledgement");return;}
      mutations_.erase(id);
      if(!failed->empty()||(r->deadline&&now_()>=r->deadline)){r->active=false;registrations_.erase(Key(r->context,r->page,r->id));GlobalRouteCallbackLeases().Close(r->scope);RetireRegistration(r);ReconcileScope(r->context,r->page);reply(false,nullptr,failed->empty()?"Native WebSocket registration original deadline exceeded":*failed);return;}
      reply(true,nullptr,"");
    });Reconcile(t);}wake_();
  }
  void RetireRegistration(const std::shared_ptr<Registration>&r){
    std::vector<std::pair<std::shared_ptr<Target>,std::string>>abort,match;
    for(const auto&[owner,target]:targets_)for(auto&[request,socket]:target->sockets)if(socket.registration==r){if(socket.matching){socket.matching=false;match.push_back({target,request});}else if(socket.selected)abort.push_back({target,request});}
    for(const auto&[target,request]:match){const auto found=target->sockets.find(request);if(found!=target->sockets.end())Select(target,found->second);}
    for(const auto&[target,request]:abort)Automatic(target,request,"abort",now_()+30000);
  }
  void Automatic(const std::shared_ptr<Target>&target,const std::string&request,const std::string&operation,double deadline){
    SendDecision(target,request,operation,nullptr,deadline,[this,target,request](bool ok,Dict,const std::string&error){if(!ok&&target->live&&target->sockets.contains(request))Fail(target,"Native WebSocket automatic decision failed: "+error);});
  }
  void Remove(int id,int context,int page,Dict input,double deadline,Reply reply){
    if(removals_.size()>=32||removals_.contains(id)||serial_==UINT64_MAX){reply(false,nullptr,"Native WebSocket removal capacity exceeded");return;}
    auto ids=input->GetList("registrationIds");std::set<std::string>selected;
    if(ids){if(ids->GetSize()>128){reply(false,nullptr,"Native WebSocket removal exceeds capacity");return;}for(size_t i=0;i<ids->GetSize();++i){if(ids->GetType(i)!=VTYPE_STRING){reply(false,nullptr,"Invalid native WebSocket removal identity");return;}selected.insert(ids->GetString(i));}}
    else if(input->HasKey("registrationId"))selected.insert(input->GetString("registrationId"));
    else if(input->GetString("action")!="routeUnregisterAll"){reply(false,nullptr,"Native WebSocket removal requires explicit registration identities");return;}
    const auto behavior=input->GetString("behavior").ToString();if(!behavior.empty()&&behavior!="default"&&behavior!="wait"&&behavior!="ignoreErrors"){reply(false,nullptr,"Invalid native WebSocket removal behavior");return;}
    Removal removal;removal.deadline=deadline;removal.reply=reply;removal.serial=++serial_;
    std::set<std::string>removed;
    if(ids&&ids->GetSize()==0&&input->GetString("action")!="routeUnregisterAll"){reply(true,nullptr,"");return;}
    for(auto it=registrations_.begin();it!=registrations_.end();){auto r=it->second;if(r->context!=context||r->page!=page||(!selected.empty()&&!selected.contains(r->id))){++it;continue;}
      auto callbacks=GlobalRouteCallbackLeases().CapturePage(r->scope);if(behavior=="wait")removal.receipts.insert(removal.receipts.end(),callbacks.receipts.begin(),callbacks.receipts.end());r->active=false;removed.insert(r->id);GlobalRouteCallbackLeases().Close(r->scope);it=registrations_.erase(it);
    }
    // Removed policy cannot issue new host callbacks after the removal ACK.
    // The vendor extension cannot release an already selected mock/interceptor;
    // explicitly abort these controlled channels. Running host closures still
    // own their global Finish receipts; close/abort acceptance is not closure.
    std::vector<std::pair<std::shared_ptr<Target>,std::string>>abort;
    std::vector<std::pair<std::shared_ptr<Target>,std::string>>match;
    for(const auto&[owner,target]:targets_)if(target->context==context)for(auto&[request,socket]:target->sockets)
      if(socket.registration&&socket.registration->page==page&&removed.contains(socket.registration->id)){
        if(socket.matching){socket.matching=false;match.push_back({target,request});}
        else if(socket.selected&&!socket.continued)abort.push_back({target,request});
      }
    for(const auto&[target,request]:match){const auto found=target->sockets.find(request);if(found!=target->sockets.end())Select(target,found->second);}
    removal.controls=abort.size();const auto removal_serial=removal.serial;removals_[id]=std::move(removal);
    for(const auto&[target,request]:abort)SendDecision(target,request,"abort",nullptr,deadline,[this,target,request,id,removal_serial](bool ok,Dict,const std::string&error){
      const auto found=removals_.find(id);
      if(found!=removals_.end()&&found->second.serial==removal_serial){if(found->second.controls)--found->second.controls;if(!ok)found->second.error=error;}
      if(!ok&&target->live&&target->sockets.contains(request))Fail(target,"Native WebSocket removal abort failed: "+error);
      Tick();
    });
    ReconcileScope(context,page);wake_();Tick();
  }

  void Select(const std::shared_ptr<Target>&target,Socket&socket){
    while(socket.next<socket.candidates.size()){
      auto r=socket.candidates[socket.next++];if(!r->active)continue;socket.registration=r;
      if(r->host){socket.matching=true;Publish(target,socket,"match",nullptr);return;}
      socket.selected=true;Publish(target,socket,"created",nullptr);return;
    }
    socket.registration.reset();const auto request=socket.request;
    Automatic(target,request,"continue",socket.deadline);
  }
  void Publish(const std::shared_ptr<Target>&target,Socket&socket,const std::string&stage,Dict actual){
    if(!socket.registration||!socket.registration->active||socket.continued)return;
    if(event_serial_==UINT64_MAX){Fail(target,"Native WebSocket callback identity exhausted");return;}
    const auto invocation="websocket:"+target->token+":"+std::to_string(++event_serial_);
    const auto lease=GlobalRouteCallbackLeases().Issue(socket.registration->scope,invocation);
    if(!lease){Fail(target,"Native WebSocket physical host callback capacity exceeded");return;}
    auto event=CefDictionaryValue::Create();event->SetString("scope",socket.registration->page?"page":"context");event->SetInt("contextId",target->context);event->SetInt("pageId",target->page);event->SetString("targetId",target->owner);
    event->SetString("registrationId",socket.registration->id);event->SetString("routingToken",target->token);event->SetString("requestId",socket.request);event->SetString("stage",stage);event->SetString("url",socket.url);
    event->SetString("messageId",actual?actual->GetString("messageId"):CefString(""));event->SetBool("outgoing",actual&&actual->GetBool("outgoing"));event->SetInt("opcode",actual?actual->GetInt("opcode"):0);std::vector<uint8_t>payload;
    const auto encoded=actual?actual->GetString("payloadBase64").ToString():std::string{};
    if(!encoded.empty()){
      auto binary=CefBase64Decode(encoded);
      if(!binary||binary->GetSize()>2097152){GlobalRouteCallbackLeases().Revoke(socket.registration->scope,invocation);Fail(target,"Invalid native WebSocket payload encoding");return;}
      payload.resize(binary->GetSize());binary->GetData(payload.data(),payload.size(),0);
      if(CefBase64Encode(payload.data(),payload.size())!=encoded){GlobalRouteCallbackLeases().Revoke(socket.registration->scope,invocation);Fail(target,"Noncanonical native WebSocket payload encoding");return;}
    }
    auto callback=CefDictionaryValue::Create();callback->SetString("invocation",invocation);callback->SetString("token",std::to_string(*lease));callback->SetString("scope",std::to_string(socket.registration->scope));event->SetDictionary("callback",callback);
    if(!emit_(event,std::move(payload))){GlobalRouteCallbackLeases().Revoke(socket.registration->scope,invocation);Fail(target,"Native WebSocket callback publication capacity exceeded");return;}
    // Retained identities are bounded by the global outstanding callback cap;
    // settled callback IDs need not accumulate for a long-lived socket.
    std::erase_if(socket.invocations,[&](const auto&entry){return !GlobalRouteCallbackLeases().Receipt(std::get<1>(entry),std::get<2>(entry),std::get<0>(entry));});
    socket.invocations.push_back({invocation,*lease,socket.registration->scope});
  }
  void SendDecision(const std::shared_ptr<Target>&target,const std::string&request,const std::string&operation,Dict input,double deadline,Reply reply){
    if(operation!="continue"&&operation!="connect"&&operation!="mock"&&operation!="forward"&&operation!="replace"&&operation!="drop"&&operation!="client"&&operation!="server"&&operation!="close"&&operation!="abort"){reply(false,nullptr,"Invalid native WebSocket channel operation");return;}
    const auto message=input?input->GetString("messageId").ToString():"",payload=input?input->GetString("payloadBase64").ToString():"",reason=input?input->GetString("reason").ToString():"";
    if(message.size()>32||payload.size()>2796204||reason.size()>123){reply(false,nullptr,"Native WebSocket channel command exceeds limits");return;}
    auto params=CefDictionaryValue::Create();params->SetString("requestId",request);params->SetString("action",operation);params->SetString("messageId",message);params->SetString("payloadBase64",payload);params->SetInt("opcode",input?input->GetInt("opcode"):0);params->SetInt("code",input?input->GetInt("code"):0);params->SetString("reason",reason);++physical_;wake_();auto alive=alive_;
    target->send("Network.suiteCutWebSocketCommandV1",params,deadline?deadline:now_()+30000,[this,alive,target,request,operation,deadline,reply=std::move(reply)](bool ok,const std::string&json){
      if(!*alive)return;
      --physical_;
      if(ok&&operation=="continue"){target->sockets.erase(request);if(target->live)Reconcile(target);}
      if(!target->live){reply(false,nullptr,"Actual native WebSocket target channel closed");return;}
      if(deadline&&now_()>=deadline){reply(false,nullptr,"Native WebSocket decision original deadline exceeded");return;}
      auto parsed=CefParseJSON(json,JSON_PARSER_RFC);auto r=parsed?parsed->GetDictionary():nullptr;reply(ok,nullptr,ok?"":r&&r->GetType("message")==VTYPE_STRING?r->GetString("message").ToString():json);
    });
  }
  std::function<double()>now_;std::function<void()>wake_;Emit emit_;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
  NativeWebSocketPayloads payloads_;
  uint64_t serial_=0,event_serial_=0;size_t physical_=0;
  std::map<std::string,std::shared_ptr<Registration>>registrations_;std::map<int,std::shared_ptr<Registration>>mutations_;std::map<int,Removal>removals_;
  std::map<std::string,std::shared_ptr<Target>>targets_;std::map<std::string,std::weak_ptr<Target>>tokens_;
};
} // namespace suitecut
