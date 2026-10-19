// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_values.h"
#include "route_registry.h"

namespace suitecut {
// Fetch can precede Network.requestWillBeSent. Keep the genuine pause until
// that event supplies Chromium's typed initiator and exact parent request ID.
// This owner never invents a Route or consumes a registration for preflight.
class NativeNetworkPreflights {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Result=std::function<void(bool,const std::string&)>;
  using Send=std::function<void(const std::string&,const std::string&,Dict,Result,double)>;
  struct Classification {bool known=false,automatic=false,authorized=false;std::string method;bool rejected=false;};
  using Classify=std::function<Classification(const std::string&,const std::string&,const NativeRouteRegistry::Plan&)>;
  using Resume=std::function<void(Dict,const std::string&,NativeRouteRegistry::Plan,double)>;
  NativeNetworkPreflights(Send send,Resume resume,std::function<void(const std::string&)> failure,
                          std::function<void()> wake,std::function<double()> now,std::function<void()> settled={})
      :state_(std::make_shared<State>()) {
    state_->send=std::move(send);state_->resume=std::move(resume);state_->failure=std::move(failure);
    state_->wake=std::move(wake);state_->now=std::move(now);state_->settled=std::move(settled);
  }
  ~NativeNetworkPreflights(){Clear();}
  // Admission owns a detached bounded copy. On rejection the caller still owns
  // the actual Fetch pause and must abort it or close the native channel.
  static std::optional<size_t> RetainedBytes(Dict data){size_t bytes=256,nodes=0;if(!MeasureDictionary(data,bytes,nodes,0))return {};return bytes;}
  bool Hold(Dict data,const std::string&session,NativeRouteRegistry::Plan plan,double deadline) {
    auto state=state_;if(state->closed||state->failed||!data)return false;
    const auto fetch=data->GetString("requestId").ToString(),network=data->GetString("networkId").ToString();
    if(fetch.empty()||fetch.size()>256||network.empty()||network.size()>256||session.size()>128||!std::isfinite(deadline)||deadline<=state->now())return false;
    const auto key=session+"\n"+fetch;if(state->entries.contains(key))return false;
    size_t bytes=256+key.size()+network.size()+session.size();size_t nodes=0;
    if(!MeasureDictionary(data,bytes,nodes,0))return false;
    if(plan.candidates.size()>256)return false;
    for(const auto&id:plan.candidates)if(id.size()>128)return false;
    if(plan.bytes()>kEntryBytes-bytes)return false;
    bytes+=plan.bytes();
    auto budget=state->budget;
    if(budget->entries>=kEntries||global_entries_>=kGlobalEntries||bytes>kBytes-budget->bytes||bytes>kGlobalBytes-global_bytes_)return false;
    auto entry=std::make_shared<Entry>();entry->budget=budget;entry->bytes=bytes;
    ++budget->entries;budget->bytes+=bytes;++global_entries_;global_bytes_+=bytes;
    entry->fetch=fetch;entry->network=network;entry->session=session;entry->plan=std::move(plan);entry->deadline=deadline;entry->data=data->Copy(false);
    state->entries.emplace(key,std::move(entry));state->wake();return true;
  }
  void Resolve(const Classify& classify) {
    auto state=state_;if(state->closed||state->failed)return;
    // Classification/resume can synchronously close this owner. Restart from
    // live state after every delivery; no iterator survives a callback.
    while(!state->closed&&!state->failed) {
      std::string key;std::shared_ptr<Entry> entry;Classification classification;
      const std::vector<std::pair<std::string,std::shared_ptr<Entry>>> snapshot(state->entries.begin(),state->entries.end());
      for(const auto&[candidate,current]:snapshot)if(!current->dispatched&&!current->delivering){
        auto result=classify(current->network,current->session,current->plan);
        if(state->closed||state->failed)return;
        const auto live=state->entries.find(candidate);if(live==state->entries.end()||live->second!=current)continue;
        if(result.known){key=candidate;entry=current;classification=std::move(result);break;}
      }
      if(!entry)return;
      if(state->now()>=entry->deadline){Fail(state,"Native automatic preflight classification deadline expired");return;}
      if(!classification.automatic) {
        entry->delivering=true;state->entries.erase(key);
        auto data=std::move(entry->data);auto plan=std::move(entry->plan);
        auto resume=state->resume;resume(std::move(data),entry->session,std::move(plan),entry->deadline);
        continue;
      }
      auto params=CefDictionaryValue::Create();params->SetString("requestId",entry->fetch);
      const auto response=!classification.rejected&&classification.authorized?Response(entry->data,classification.method):nullptr;
      const std::string method=classification.rejected?"Fetch.failRequest":response?"Fetch.fulfillRequest":"Fetch.continueRequest";
      if(classification.rejected)params->SetString("errorReason","Aborted");
      if(response){params=response;params->SetString("requestId",entry->fetch);}
      entry->dispatched=true;
      // Retain the pause and its metadata until this exact physical receipt
      // or a genuine channel close, including a logical deadline failure.
      auto send=state->send;send(entry->session,method,params,[state,key,entry](bool ok,const std::string&error){
        if(state->closed)return;
        const auto found=state->entries.find(key);if(found==state->entries.end()||found->second!=entry)return;
        state->entries.erase(found);
        if(!ok){Fail(state,"Native automatic preflight control failed: "+error);return;}
        auto settled=state->settled;if(settled&&!state->failed)settled();
      },entry->deadline);
    }
  }
  void Tick(double now) {
    auto state=state_;if(state->closed||state->failed)return;
    for(const auto&[key,entry]:state->entries)if(now>=entry->deadline){Fail(state,"Native automatic preflight ownership deadline expired");return;}
  }
  // Only genuine Target.detachedFromTarget or real browser OnBeforeClose.
  void SessionClosed(const std::string&session) {
    auto state=state_;for(auto it=state->entries.begin();it!=state->entries.end();)if(it->second->session==session)it=state->entries.erase(it);else ++it;
  }
  void Clear(){auto state=state_;state->closed=true;state->entries.clear();state->resume={};state->failure={};state->send={};state->wake={};state->settled={};}
  bool NeedsTick()const{return !state_->entries.empty();}
  size_t pending()const{return state_->entries.size();}
  size_t bytes()const{return state_->budget->bytes;}
  static size_t global_pending(){return global_entries_;}
  static size_t global_bytes(){return global_bytes_;}
 private:
  static constexpr size_t kEntryBytes=1048576,kBytes=2097152,kGlobalBytes=8388608,kEntries=64,kGlobalEntries=128;
  struct Budget{size_t entries=0,bytes=0;};
  struct Entry {
    std::shared_ptr<Budget>budget;size_t bytes=0;Dict data;std::string fetch,network,session;
    NativeRouteRegistry::Plan plan;double deadline=0;bool dispatched=false,delivering=false;
    ~Entry(){if(budget){--budget->entries;budget->bytes-=bytes;--global_entries_;global_bytes_-=bytes;}}
  };
  struct State {
    Send send;Resume resume;std::function<void(const std::string&)>failure;std::function<void()>wake;std::function<double()>now;std::function<void()>settled;
    std::shared_ptr<Budget>budget=std::make_shared<Budget>();std::map<std::string,std::shared_ptr<Entry>>entries;bool closed=false,failed=false;
  };
  static inline size_t global_entries_=0,global_bytes_=0;
  std::shared_ptr<State>state_;
  static void Fail(const std::shared_ptr<State>&state,const std::string&error){if(state->closed||state->failed)return;state->failed=true;auto failure=state->failure;if(failure)failure(error);}
  static bool Charge(size_t count,size_t&bytes){if(count>kEntryBytes-bytes)return false;bytes+=count;return true;}
  static bool MeasureValue(CefRefPtr<CefValue>value,size_t&bytes,size_t&nodes,size_t depth){
    if(!value||++nodes>8192||depth>16||!Charge(64,bytes))return false;
    switch(value->GetType()){
      case VTYPE_DICTIONARY:return MeasureDictionary(value->GetDictionary(),bytes,nodes,depth+1);
      case VTYPE_LIST:{auto list=value->GetList();if(!list||list->GetSize()>8192)return false;for(size_t i=0;i<list->GetSize();++i)if(!MeasureValue(list->GetValue(i),bytes,nodes,depth+1))return false;return true;}
      case VTYPE_STRING:{const auto text=value->GetString();return text.length()<=kEntryBytes/3&&Charge(text.length()*3,bytes);}
      case VTYPE_BINARY:return false;
      default:return true;
    }
  }
  static bool MeasureDictionary(Dict data,size_t&bytes,size_t&nodes,size_t depth){
    if(!data||depth>16||data->GetSize()>8192)return false;
    CefDictionaryValue::KeyList keys;data->GetKeys(keys);if(keys.size()>8192)return false;
    for(const auto&key:keys)if(key.length()>kEntryBytes/3||!Charge(key.length()*3+64,bytes)||!MeasureValue(data->GetValue(key),bytes,nodes,depth+1))return false;
    return true;
  }
  static std::string Header(Dict headers,const std::string&name){
    if(!headers)return {};
    CefDictionaryValue::KeyList keys;headers->GetKeys(keys);
    for(const auto&key:keys){auto normalized=key.ToString();for(auto&c:normalized)if(c>='A'&&c<='Z')c=char(c-'A'+'a');if(normalized==name&&headers->GetType(key)==VTYPE_STRING)return headers->GetString(key).ToString();}return {};
  }
  static bool Field(const std::string&value,size_t bound){return !value.empty()&&value.size()<=bound&&value.find_first_of("\r\n\0",0,3)==std::string::npos;}
  static Dict Response(Dict data,const std::string&method){
    auto request=data->GetDictionary("request");if(!request||request->GetString("method")!="OPTIONS")return nullptr;auto headers=request->GetDictionary("headers");
    const auto origin=Header(headers,"origin"),requested=Header(headers,"access-control-request-headers");
    if(!Field(origin,8192)||!Field(method,128)||(!requested.empty()&&!Field(requested,65536)))return nullptr;
    auto result=CefDictionaryValue::Create();result->SetInt("responseCode",204);result->SetString("body","");auto output=CefListValue::Create();
    const auto append=[&](const std::string&name,const std::string&value){auto pair=CefDictionaryValue::Create();pair->SetString("name",name);pair->SetString("value",value);output->SetDictionary(output->GetSize(),pair);};
    append("Access-Control-Allow-Origin",origin);append("Access-Control-Allow-Methods",method);append("Access-Control-Allow-Credentials","true");
    if(!requested.empty())append("Access-Control-Allow-Headers",requested);
    append("Access-Control-Max-Age","0");append("Cache-Control","no-store");append("Vary","Origin, Access-Control-Request-Method, Access-Control-Request-Headers");
    result->SetList("responseHeaders",output);return result;
  }
};
} // namespace suitecut
