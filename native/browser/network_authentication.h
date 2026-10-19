// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_values.h"
namespace suitecut {
// Native, bounded ownership of genuine Fetch.authRequired challenges. Profiles
// provide credentials directly; no username or password crosses SDK events.
class NativeNetworkAuthentication {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Result=std::function<void(bool,const std::string&)>;
  using Provider=std::function<Dict(Dict)>;
  using Send=std::function<void(const std::string&,const std::string&,Dict,Result)>;
  NativeNetworkAuthentication(Send send,std::function<void(const std::string&)>failure,std::function<void()>wake)
      :send_(std::move(send)),failure_(std::move(failure)),wake_(std::move(wake)){}
  ~NativeNetworkAuthentication(){Clear();}
  void Set(Provider provider){provider_=std::move(provider);}
  bool enabled()const{return bool(provider_)&&!failed_;}
  void Paused(const std::string&fetch_session,const std::string&fetch,const std::string&network_session,const std::string&network,double now){
    if(disposed_||failed_||!provider_||fetch.empty()||network.empty())return;
    if(fetch.size()>256||network.size()>256||fetch_session.size()>128||network_session.size()>128){Fail("Invalid native authentication request correlation");return;}
    const auto key=fetch_session+"\n"+fetch;
    if(!links_.contains(key)){if(links_.size()>=512||global_links_>=2048){Fail("Native authentication request-correlation capacity exceeded");return;}++global_links_;}
    links_[key]=Link{network_session,network,now+30000};wake_();
  }
  void NetworkFinished(const std::string&session,const std::string&network){
    std::vector<std::string>done;for(const auto&[key,link]:links_)if(link.session==session&&link.network==network)done.push_back(key);
    for(const auto&key:done){links_.erase(key);--global_links_;const auto pending=pending_.find(key);if(pending!=pending_.end()){pending->second.completed=true;continue;}EraseAttempt(key);}
  }
  void NetworkMoved(const std::string&network,const std::string&parent,const std::string&child){for(auto&[key,link]:links_)if(link.session==parent&&link.network==network)link.session=child;}
  void Event(Dict params,const std::string& session,double now){
    if(disposed_||failed_||!provider_)return;
    const auto id=params?params->GetString("requestId").ToString():std::string{};
    auto request=params?params->GetDictionary("request"):nullptr;auto challenge=params?params->GetDictionary("authChallenge"):nullptr;
    if(id.empty()||id.size()>256||session.size()>128||!request||!challenge||request->GetString("url").length()>8192||challenge->GetString("origin").length()>8192||challenge->GetString("realm").length()>1024||challenge->GetString("scheme").length()>64||(challenge->GetString("source")!="Server"&&challenge->GetString("source")!="Proxy")){Fail("Invalid native authentication challenge");return;}
    const auto key=session+"\n"+id;
    auto found=attempts_.find(key);
    if(found==attempts_.end()){
      if(attempts_.size()>=256||global_attempts_>=1024){Fail("Native authentication attempt capacity exceeded");return;}
      found=attempts_.emplace(key,Attempt{0,now+30000}).first;++global_attempts_;
    }
    if(pending_.contains(key)||pending_.size()>=64||global_pending_>=128){Fail("Native authentication challenge ownership capacity exceeded");return;}
    auto decision=CefDictionaryValue::Create();
    if(++found->second.count>3||now>=found->second.deadline)decision->SetString("response","CancelAuth");
    else {
      auto input=CefDictionaryValue::Create();auto actual_request=CefDictionaryValue::Create();actual_request->SetString("url",request->GetString("url"));input->SetDictionary("request",actual_request);
      auto actual_challenge=CefDictionaryValue::Create();for(const auto* field:{"source","origin","realm","scheme"})actual_challenge->SetString(field,challenge->GetString(field));input->SetDictionary("authChallenge",actual_challenge);input->SetString("requestId",id);
      decision=provider_(input);
    }
    if(!Valid(decision)){Fail("Invalid native credential decision");return;}
    const auto generation=++serial_;pending_.emplace(key,Pending{generation,found->second.deadline,false,false});++global_pending_;
    auto response=CefDictionaryValue::Create();response->SetString("requestId",id);response->SetDictionary("authChallengeResponse",decision->Copy(false));
    send_(session,"Fetch.continueWithAuth",response,[this,alive=alive_,key,generation](bool ok,const std::string&error){
      if(!*alive)return;
      const auto found=pending_.find(key);if(found==pending_.end()||found->second.generation!=generation)return;
      if(found->second.acknowledged)return;
      found->second.acknowledged=true;
      if(!ok){Fail("Native authentication response was rejected: "+error);return;}
      const bool completed=found->second.completed;pending_.erase(found);--global_pending_;if(completed)EraseAttempt(key);
    });
    if(!disposed_)wake_();
  }
  void SessionDetached(const std::string&session){
    const auto prefix=session+"\n";
    for(auto it=pending_.begin();it!=pending_.end();)if(it->first.starts_with(prefix)){it=pending_.erase(it);--global_pending_;}else++it;
    for(auto it=links_.begin();it!=links_.end();)if(it->first.starts_with(prefix)||it->second.session==session){const auto key=it->first;it=links_.erase(it);--global_links_;if(auto pending=pending_.find(key);pending!=pending_.end()){pending_.erase(pending);--global_pending_;}EraseAttempt(key);}else++it;
    for(auto it=attempts_.begin();it!=attempts_.end();)if(it->first.starts_with(prefix)){it=attempts_.erase(it);--global_attempts_;}else++it;
  }
  void Tick(double now){
    if(disposed_||failed_)return;
    for(auto it=links_.begin();it!=links_.end();)if(now>=it->second.deadline&&!pending_.contains(it->first)){it=links_.erase(it);--global_links_;}else++it;
    for(const auto&[key,pending]:pending_)if(now>=pending.deadline){Fail("Native authentication response deadline exceeded");return;}
    for(auto it=attempts_.begin();it!=attempts_.end();)if(now>=it->second.deadline&&!pending_.contains(it->first)){it=attempts_.erase(it);--global_attempts_;}else++it;
  }
  bool NeedsTick()const{return!pending_.empty()||!attempts_.empty()||!links_.empty();}
  bool failed()const{return failed_;}
  size_t pending()const{return pending_.size();}
  size_t attempts()const{return attempts_.size();}
  size_t correlations()const{return links_.size();}
  void Clear(){if(disposed_)return;disposed_=true;*alive_=false;provider_={};global_pending_-=pending_.size();global_attempts_-=attempts_.size();global_links_-=links_.size();pending_.clear();attempts_.clear();links_.clear();}
 private:
  struct Attempt{unsigned count;double deadline;};struct Pending{uint64_t generation;double deadline;bool completed,acknowledged;};struct Link{std::string session,network;double deadline;};
  void EraseAttempt(const std::string&key){if(attempts_.erase(key))--global_attempts_;}
  static bool Valid(Dict decision){
    if(!decision||decision->GetType("response")!=VTYPE_STRING)return false;
    const auto response=decision->GetString("response").ToString();
    if(response=="CancelAuth")return true;
    return response=="ProvideCredentials"&&decision->GetType("username")==VTYPE_STRING&&decision->GetType("password")==VTYPE_STRING&&decision->GetString("username").length()<=4096&&decision->GetString("password").length()<=4096;
  }
  void Fail(const std::string&error){if(disposed_||failed_)return;failed_=true;failure_(error);}
  inline static size_t global_pending_=0,global_attempts_=0,global_links_=0;
  Send send_;std::function<void(const std::string&)>failure_;std::function<void()>wake_;Provider provider_;
  bool disposed_=false,failed_=false;uint64_t serial_=0;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
  std::map<std::string,Attempt>attempts_;std::map<std::string,Pending>pending_;std::map<std::string,Link>links_;
};
}
