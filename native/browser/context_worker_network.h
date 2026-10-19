// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <charconv>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "include/cef_parser.h"

namespace suitecut {
// Canonical context requests use actual Chromium target + Network request IDs.
// Each observation retains its separate physical Source/broker command alias.
// A page close removes that alias; it never invents a replacement page/frame.
class NativeContextWorkerNetwork {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Bytes=std::vector<uint8_t>;
  using Reply=std::function<void(bool,Dict,const std::string&)>;
  using Command=std::function<void(int,Dict,double,Reply)>;
  using Body=std::function<void(const std::string&,const std::string&,double,std::function<void(bool,Bytes,const std::string&)>)>;
  using Cancel=std::function<void(int)>;
  using Emit=std::function<void(int,Dict,Dict)>;
  using RouteEmit=std::function<void(int,const std::string&,Dict,Dict)>;
  void SetRouteEmitter(RouteEmit emit){route_emit_=std::move(emit);}
  explicit NativeContextWorkerNetwork(Emit emit):emit_(std::move(emit)){}
  ~NativeContextWorkerNetwork(){Clear();*alive_=false;}
  bool Attach(int context,const std::string& owner,bool broker,Command command,Body body,Cancel cancel){
    if(closed_||context<1||owner.empty()||owner.size()>128||owners_.contains(owner)||owners_.size()>=192||!command||!body||!cancel)return false;
    owners_.emplace(owner,Owner{context,broker,std::move(command),std::move(body),std::move(cancel)});return true;
  }
  bool Observe(int context,const std::string& owner,Dict worker,Dict network){
    if(closed_||!worker||!network)return false;
    const auto source=owners_.find(owner);const auto target=worker->GetString("id").ToString(),kind=worker->GetString("type").ToString();
    if(source==owners_.end()||source->second.context!=context||target.empty()||target.size()>128||(kind!="worker"&&kind!="shared_worker"&&kind!="service_worker"))return false;
    const auto event=network->GetString("event").ToString();
    if(event=="routematch"){
      auto matcher=network->GetDictionary("matcher");if(!route_emit_||!matcher||matcher->GetString("id").empty()||matcher->GetString("id").length()>128||matcher->GetString("registrationId").empty()||matcher->GetString("registrationId").length()>128||matcher->GetString("url").empty()||matcher->GetString("url").length()>8192||matcher->GetString("callbackScope").empty()||matcher->GetString("callbackScope").length()>20||matcher->GetString("callbackToken").empty()||matcher->GetString("callbackToken").length()>20||sequence_>=9007199254740991ULL)return false;
      auto projected=network->Copy(false);projected->SetDouble("sequence",double(++sequence_));route_emit_(context,owner,worker->Copy(false),projected);return true;
    }
    const unsigned bit=Bit(event);if(!bit&&event!="requestupdated"&&event!="route")return true;
    auto request=network->GetDictionary("request");if(!request)return false;
    const auto native=request->GetString("requestId").ToString(),local=request->GetString("id").ToString(),session=request->GetString("session").ToString();
    if(native.empty()||native.size()>256||local.empty()||local.size()>256||session.size()>128)return false;
    const auto alias_key=AliasKey(owner,local);auto alias=aliases_.find(alias_key);std::shared_ptr<Entry> entry;
    if(alias!=aliases_.end()){const auto found=entries_.find(alias->second);if(found!=entries_.end())entry=found->second;}
    if(!entry){
      uint64_t redirect=0;std::string previous;
      const auto old=request->GetString("redirectedFrom").ToString();
      if(!old.empty()){
        const auto parent=aliases_.find(AliasKey(owner,old));if(parent==aliases_.end())return false;
        const auto found=entries_.find(parent->second);if(found==entries_.end())return false;
        redirect=found->second->redirect+1;previous=found->second->id;
      }
      const auto key=std::to_string(context)+"\n"+target+"\n"+native+"\n"+std::to_string(redirect);
      const auto known=keys_.find(key);
      if(known!=keys_.end())entry=entries_.at(known->second);
      else {
        // A real Fetch invocation may precede its Network creation packet.
        // This reserves canonical identity only; it emits no request event.
        if(event!="request"&&event!="route")return true;
        if(serial_>=9007199254740991ULL||!Reserve(0))return false;
        entry=std::make_shared<Entry>();entry->context=context;entry->id="worker-request:"+std::to_string(++serial_);entry->key=key;entry->worker=worker->Copy(false);entry->redirect=redirect;entry->previous=previous;
        entries_.emplace(entry->id,entry);keys_[key]=entry->id;order_.push_back(entry->id);
      }
      if(aliases_.size()>=16384)return false;
      aliases_[alias_key]=entry->id;
    }
    if(entry->context!=context||entry->worker->GetString("id")!=target)return false;
    entry->aliases[owner]=Alias{local,session,native};
    auto projected=network->Copy(false);auto value=projected->GetDictionary("request");value->SetString("id",entry->id);if(value->HasKey("postDataBodyId"))value->SetString("postDataOwner",owner);
    if(!entry->previous.empty())value->SetString("redirectedFrom",entry->previous);else value->Remove("redirectedFrom");
    // Attribution remains genuine worker ownership even if a page also observes
    // this actual worker target. No Page ID is placed in this projection.
    value->SetBool("serviceWorker",kind=="service_worker");
    std::shared_ptr<RouteOwner>route_owner=entry->route_owner;
    if(event=="route"){
      auto actual=network->GetDictionary("route");if(!actual)return false;
      const auto scope=actual->GetString("callbackScope").ToString(),invocation=actual->GetString("id").ToString();
      if(scope.empty()||scope.size()>20||invocation.empty()||invocation.size()>128)return false;
      route_owner=std::make_shared<RouteOwner>(RouteOwner{owner,scope,invocation});
    }
    const auto cost=JSON(projected).size()+JSON(worker).size()+512+(route_owner?route_owner->bytes():0);
    if(cost>33554432||!Reserve(cost>entry->cost?cost-entry->cost:0,entry->id))return false;
    bytes_=bytes_-entry->cost+cost;entry->cost=cost;entry->latest=projected;entry->route_owner=std::move(route_owner);
    if(event=="requestfinished"||event=="requestfailed")entry->finished=true;
    if(event=="route"){
      if(!route_emit_||sequence_>=9007199254740991ULL)return false;
      projected->SetDouble("sequence",double(++sequence_));route_emit_(context,owner,entry->worker->Copy(false),projected);return true;
    }
    if(!bit||(entry->published&bit))return true;
    if(sequence_>=9007199254740991ULL)return false;
    entry->published|=bit;projected->SetDouble("sequence",double(++sequence_));
    emit_(context,entry->worker->Copy(false),projected);return true;
  }
  // This field is overwritten only by the verified native route bridge. Its
  // nonce is local to this actual manager admission domain, not a browser ID.
  static uint64_t RouteOperationGeneration(Dict command){
    if(!command||command->GetType("suitecutRouteOperation")!=VTYPE_STRING)return 0;
    const auto text=command->GetString("suitecutRouteOperation").ToString();if(text.empty()||text.size()>20)return 0;
    uint64_t value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);return parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()?value:0;
  }
  bool HandleRoute(int operation,int context,const std::string&actual,Dict params,double deadline,Reply reply){
    const auto source=owners_.find(actual);if(closed_||operation<1||!params||!reply||!std::isfinite(deadline)||deadline<0||source==owners_.end()||source->second.context!=context||pending_.contains(operation)||pending_.size()>=128||operation_serial_==UINT64_MAX)return false;
    const auto action=params->GetString("action").ToString();
    if(action!="fetch"&&action!="matchRoute"&&action!="continue"&&action!="fallback"&&action!="abort"&&action!="fulfill"&&action!="resetBody"&&action!="bodyChunk")return false;
    if(params->GetType("callbackScope")!=VTYPE_STRING||params->GetString("callbackScope").empty()||params->GetString("callbackScope").length()>20||params->GetType("routeId")!=VTYPE_STRING||params->GetString("routeId").empty()||params->GetString("routeId").length()>128)return false;
    const auto generation=++operation_serial_;pending_.emplace(operation,Pending{context,actual,false,std::move(reply),generation});auto command=source->second.command;
    const auto scope=params->GetString("callbackScope").ToString(),invocation=params->GetString("routeId").ToString();const bool terminal=action!="fetch"&&action!="bodyChunk"&&action!="resetBody"&&action!="matchRoute";
    auto forwarded=params->Copy(false);forwarded->SetString("suitecutRouteOperation",std::to_string(generation));
    command(operation,forwarded,deadline,[this,alive=alive_,operation,generation,actual,scope,invocation,terminal](bool ok,Dict result,const std::string&error){
      if(!*alive)return;
      auto found=pending_.find(operation);if(found==pending_.end()||found->second.generation!=generation)return;
      if(ok&&terminal)for(auto&[id,entry]:entries_)if(entry->route_owner&&entry->route_owner->owner==actual&&entry->route_owner->scope==scope&&entry->route_owner->invocation==invocation)entry->route_owner.reset();
      auto reply=std::move(found->second.reply);pending_.erase(found);if(reply)reply(ok,result,error);
    });return true;
  }
  bool Handle(int owner,int context,const std::string& id,Dict params,double deadline,Reply reply){
    if(closed_||owner<1||!params||!reply||!std::isfinite(deadline)||deadline<0||pending_.contains(owner)||pending_.size()>=128||operation_serial_==UINT64_MAX)return false;
    const auto found=entries_.find(id);if(found==entries_.end()||found->second->context!=context)return false;
    const auto action=params->GetString("action").ToString();const bool body=action=="body";
    if(!body&&action!="requestHeaders"&&action!="responseHeaders"&&action!="response"&&action!="finished")return false;
    const auto actual=Choose(*found->second,action=="requestHeaders");if(actual.empty())return false;
    const auto backend=owners_.at(actual);const auto alias=found->second->aliases.at(actual);
    const auto generation=++operation_serial_;pending_.emplace(owner,Pending{context,actual,body,std::move(reply),generation});
    auto done=[this,alive=alive_,owner,generation](bool ok,Dict value,const std::string&error){
      if(!*alive)return;
      const auto found=pending_.find(owner);if(found==pending_.end()||found->second.generation!=generation)return;
      auto reply=std::move(found->second.reply);pending_.erase(found);
      if(reply)reply(ok,value,error);
    };
    if(body){backend.body(alias.session,alias.native,deadline,[done=std::move(done)](bool ok,Bytes bytes,const std::string&error){
      Dict value;if(ok){value=CefDictionaryValue::Create();value->SetString("body",CefBase64Encode(bytes.data(),bytes.size()));value->SetBool("base64Encoded",true);}done(ok,value,error);
    });}
    else {auto command=params->Copy(false);command->SetString("requestId",alias.local);backend.command(owner,command,deadline,std::move(done));}
    return true;
  }
  void CancelOwner(int id){
    const auto found=pending_.find(id);if(found==pending_.end())return;
    found->second.reply={};if(found->second.body)return; // physical ACK/close still owns the body callback
    const auto source=owners_.find(found->second.owner);Cancel cancel=source==owners_.end()?Cancel{}:source->second.cancel;pending_.erase(found);if(cancel)cancel(id);
  }
  void Detach(const std::string&owner){
    owners_.erase(owner);
    for(auto it=aliases_.begin();it!=aliases_.end();)if(it->first.starts_with(owner+"\n"))it=aliases_.erase(it);else ++it;
    for(auto&[id,entry]:entries_){entry->aliases.erase(owner);if(entry->route_owner&&entry->route_owner->owner==owner)entry->route_owner.reset();}
    std::vector<int> canceled;for(const auto&[id,pending]:pending_)if(pending.owner==owner&&!pending.body)canceled.push_back(id);
    for(const auto id:canceled){const auto found=pending_.find(id);auto reply=std::move(found->second.reply);pending_.erase(found);if(reply)reply(false,nullptr,"Actual worker network observation channel closed");}
  }
  void Closed(int context){
    std::vector<int> canceled;for(const auto&[id,pending]:pending_)if(pending.context==context)canceled.push_back(id);
    for(const auto id:canceled){auto found=pending_.find(id);auto reply=std::move(found->second.reply);CancelOwner(id);if(reply)reply(false,nullptr,"Native browser context is closed");}
    std::vector<std::string> owners;for(const auto&[id,value]:owners_)if(value.context==context)owners.push_back(id);for(const auto&id:owners)Detach(id);
    std::vector<std::string> entries;for(const auto&[id,value]:entries_)if(value->context==context)entries.push_back(id);for(const auto&id:entries)Erase(id);
  }
  void Clear(){
    if(closed_)return;
    closed_=true;std::vector<int> canceled;for(const auto&[id,pending]:pending_)canceled.push_back(id);for(int id:canceled)CancelOwner(id);
    // Logical disposal cannot retire an unacknowledged physical body read.
    // Source/broker real close (or actual ACK) invokes its retained callback.
    owners_.clear();entries_.clear();aliases_.clear();keys_.clear();order_.clear();bytes_=0;
  }
  size_t physical()const{size_t result=0;for(const auto&[id,pending]:pending_)result+=pending.body;return result;}
  size_t retained_bytes()const{return bytes_;}size_t records()const{return entries_.size();}size_t aliases()const{return aliases_.size();}
 private:
  struct Owner{int context;bool broker;Command command;Body body;Cancel cancel;};
  struct Alias{std::string local,session,native;};
  struct RouteOwner{std::string owner,scope,invocation;size_t bytes()const{return sizeof(*this)+owner.capacity()+scope.capacity()+invocation.capacity();}};
  struct Entry{int context=0;std::string id,key,previous;Dict worker,latest;uint64_t redirect=0;unsigned published=0;bool finished=false;size_t cost=0;std::map<std::string,Alias>aliases;std::shared_ptr<RouteOwner>route_owner;};
  struct Pending{int context;std::string owner;bool body;Reply reply;uint64_t generation;};
  static std::string AliasKey(const std::string&owner,const std::string&local){return owner+"\n"+local;}
  static unsigned Bit(const std::string&event){return event=="request"?1:event=="response"?2:event=="requestfinished"?4:event=="requestfailed"?8:0;}
  static std::string JSON(Dict value){auto wrapper=CefValue::Create();wrapper->SetDictionary(value->Copy(false));return CefWriteJSON(wrapper,JSON_WRITER_DEFAULT);}
  std::string Choose(const Entry&entry,bool owned_headers=false)const{
    if(owned_headers&&entry.route_owner&&owners_.contains(entry.route_owner->owner)&&entry.aliases.contains(entry.route_owner->owner))return entry.route_owner->owner;
    std::string chosen;for(const auto&[id,alias]:entry.aliases){const auto found=owners_.find(id);if(found==owners_.end())continue;if(chosen.empty()||found->second.broker)chosen=id;if(found->second.broker)break;}return chosen;
  }
  bool Reserve(size_t additional,const std::string&keep={}){
    while((keep.empty()&&entries_.size()>=4096)||additional>33554432-bytes_){const auto old=std::find_if(order_.begin(),order_.end(),[&](const std::string&id){const auto found=entries_.find(id);return found==entries_.end()||(id!=keep&&found->second->finished);});if(old==order_.end())return false;const auto id=*old;order_.erase(old);Erase(id);}return true;
  }
  void Erase(const std::string&id){const auto found=entries_.find(id);if(found==entries_.end())return;keys_.erase(found->second->key);bytes_-=found->second->cost;entries_.erase(found);for(auto it=aliases_.begin();it!=aliases_.end();)if(it->second==id)it=aliases_.erase(it);else ++it;order_.erase(std::remove(order_.begin(),order_.end(),id),order_.end());}
  Emit emit_;RouteEmit route_emit_;bool closed_=false;uint64_t serial_=0,sequence_=0,operation_serial_=0;size_t bytes_=0;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
  std::map<std::string,Owner>owners_;std::map<std::string,std::shared_ptr<Entry>>entries_;std::map<std::string,std::string>keys_,aliases_;std::deque<std::string>order_;std::map<int,Pending>pending_;
};
} // namespace suitecut
