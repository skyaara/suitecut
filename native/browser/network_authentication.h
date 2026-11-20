// SPDX-License-Identifier: MIT
#pragma once
#include <set>
#include <limits>
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
// Native ownership of genuine Fetch.authRequired challenges. Profiles
// provide credentials directly; no username or password crosses SDK events.
class NativeNetworkAuthentication {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Result=std::function<void(bool,const std::string&)>;
  using Provider=std::function<Dict(Dict)>;
  // A provider may answer {"response":"Ask"}: then |asker| gets the challenge
  // and a function that answers it later, with no deadline of SuiteCut's own
  // (a person's prompt).
  using Asker=std::function<void(Dict input,std::function<void(Dict decision)> answer)>;
  using Send=std::function<void(const std::string&,const std::string&,Dict,Result)>;
  NativeNetworkAuthentication(Send send,std::function<void(const std::string&)>failure,std::function<void()>wake)
      :send_(std::move(send)),failure_(std::move(failure)),wake_(std::move(wake)){}
  ~NativeNetworkAuthentication(){Clear();}
  void Set(Provider provider){provider_=std::move(provider);}
  void SetAsker(Asker asker){asker_=std::move(asker);}
  // Reports a challenge or correlation this control gives up on. A request it
  // can't own is answered with CancelAuth, so Chromium never waits on it.
  std::function<void(const std::string&)> degraded;
  bool enabled()const{return bool(provider_)&&!failed_;}
  void Paused(const std::string&fetch_session,const std::string&fetch,const std::string&network_session,const std::string&network,double now){
    if(disposed_||failed_||!provider_||fetch.empty()||network.empty())return;
    const auto key=fetch_session+"\n"+fetch;
    links_[key]=Link{network_session,network,now+30000};wake_();
  }
  void NetworkFinished(const std::string&session,const std::string&network){
    std::vector<std::string>done;for(const auto&[key,link]:links_)if(link.session==session&&link.network==network)done.push_back(key);
    for(const auto&key:done){links_.erase(key);const auto pending=pending_.find(key);if(pending!=pending_.end()){pending->second.completed=true;continue;}EraseAttempt(key);}
  }
  void NetworkMoved(const std::string&network,const std::string&parent,const std::string&child){for(auto&[key,link]:links_)if(link.session==parent&&link.network==network)link.session=child;}
  void Event(Dict params,const std::string& session,double now){
    if(disposed_||failed_||!provider_)return;
    const auto id=params?params->GetString("requestId").ToString():std::string{};
    auto request=params?params->GetDictionary("request"):nullptr;auto challenge=params?params->GetDictionary("authChallenge"):nullptr;
    if(id.empty()||!request||!challenge||(challenge->GetString("source")!="Server"&&challenge->GetString("source")!="Proxy")){if(!id.empty())Cancel(session,id);Drop("Invalid native authentication challenge");return;}
    const auto key=session+"\n"+id;
    auto found=attempts_.find(key);
    if(found==attempts_.end())found=attempts_.emplace(key,Attempt{0,now+30000}).first;
    if(pending_.contains(key)){Drop("Native authentication challenge is already pending");return;}
    auto input=CefDictionaryValue::Create();auto actual_request=CefDictionaryValue::Create();actual_request->SetString("url",request->GetString("url"));input->SetDictionary("request",actual_request);
    auto actual_challenge=CefDictionaryValue::Create();for(const auto* field:{"source","origin","realm","scheme"})actual_challenge->SetString(field,challenge->GetString(field));input->SetDictionary("authChallenge",actual_challenge);input->SetString("requestId",id);
    // A person answers as often as the server asks, whenever they answer.
    Dict decision=asker_?provider_(input):nullptr;
    if(decision&&decision->GetType("response")==VTYPE_STRING&&decision->GetString("response")=="Ask"){
      if(asked_.contains(key)){Drop("Native authentication challenge is already asked");return;}
      asked_.insert(key);
      asker_(input,[this,alive=alive_,session,id,key](Dict answer){
        if(!*alive||!asked_.erase(key))return;
        Respond(session,id,key,answer,0);
      });
      return;
    }
    if(++found->second.count>3||now>=found->second.deadline){decision=CefDictionaryValue::Create();decision->SetString("response","CancelAuth");}
    else if(!decision)decision=provider_(input);
    Respond(session,id,key,decision,found->second.deadline);
  }
  void Respond(const std::string&session,const std::string&id,const std::string&key,Dict decision,double deadline){
    if(!Valid(decision)){Cancel(session,id);Drop("Invalid native credential decision");return;}
    const auto generation=++serial_;pending_.emplace(key,Pending{generation,deadline>0?deadline:std::numeric_limits<double>::infinity(),false,false});
    auto response=CefDictionaryValue::Create();response->SetString("requestId",id);response->SetDictionary("authChallengeResponse",decision->Copy(false));
    send_(session,"Fetch.continueWithAuth",response,[this,alive=alive_,key,generation](bool ok,const std::string&error){
      if(!*alive)return;
      const auto found=pending_.find(key);if(found==pending_.end()||found->second.generation!=generation)return;
      if(found->second.acknowledged)return;
      found->second.acknowledged=true;
      // Chromium rejects a response for a request that was cancelled or
      // navigated away; that request is simply over.
      if(!ok){pending_.erase(found);EraseAttempt(key);Drop("Native authentication response was rejected");return;}
      const bool completed=found->second.completed;pending_.erase(found);if(completed)EraseAttempt(key);
    });
    if(!disposed_)wake_();
  }
  void SessionDetached(const std::string&session){
    const auto prefix=session+"\n";
    for(auto it=pending_.begin();it!=pending_.end();)if(it->first.starts_with(prefix))it=pending_.erase(it);else++it;
    for(auto it=links_.begin();it!=links_.end();)if(it->first.starts_with(prefix)||it->second.session==session){const auto key=it->first;it=links_.erase(it);pending_.erase(key);EraseAttempt(key);}else++it;
    for(auto it=attempts_.begin();it!=attempts_.end();)if(it->first.starts_with(prefix))it=attempts_.erase(it);else++it;
    for(auto it=asked_.begin();it!=asked_.end();)if(it->starts_with(prefix))it=asked_.erase(it);else++it;
  }
  void Tick(double now){
    if(disposed_||failed_)return;
    for(auto it=links_.begin();it!=links_.end();)if(now>=it->second.deadline&&!pending_.contains(it->first))it=links_.erase(it);else++it;
    size_t expired=0;for(auto it=pending_.begin();it!=pending_.end();)if(now>=it->second.deadline){it=pending_.erase(it);++expired;}else++it;
    if(expired)Drop("Native authentication response deadline exceeded");
    for(auto it=attempts_.begin();it!=attempts_.end();)if(now>=it->second.deadline&&!pending_.contains(it->first))it=attempts_.erase(it);else++it;
  }
  // A link only correlates a request in case a challenge arrives; it expires on
  // its own. A viewer handover treats links as resident, not as pending work.
  bool NeedsTick(bool viewer=false)const{return!pending_.empty()||!attempts_.empty()||(!viewer&&!links_.empty());}
  bool failed()const{return failed_;}
  size_t pending()const{return pending_.size();}
  size_t attempts()const{return attempts_.size();}
  size_t correlations()const{return links_.size();}
  void Clear(){if(disposed_)return;disposed_=true;*alive_=false;provider_={};asker_={};pending_.clear();attempts_.clear();links_.clear();asked_.clear();}
 private:
  struct Attempt{unsigned count;double deadline;};struct Pending{uint64_t generation;double deadline;bool completed,acknowledged;};struct Link{std::string session,network;double deadline;};
  void EraseAttempt(const std::string&key){attempts_.erase(key);}
  static bool Valid(Dict decision){
    if(!decision||decision->GetType("response")!=VTYPE_STRING)return false;
    const auto response=decision->GetString("response").ToString();
    // Default hands the challenge to CEF's GetAuthCredentials (a person's prompt).
    if(response=="CancelAuth"||response=="Default")return true;
    return response=="ProvideCredentials"&&decision->GetType("username")==VTYPE_STRING&&decision->GetType("password")==VTYPE_STRING;
  }
  void Fail(const std::string&error){if(disposed_||failed_)return;failed_=true;failure_(error);}
  void Drop(const std::string&error){if(degraded)degraded(error);}
  void Cancel(const std::string&session,const std::string&id){auto response=CefDictionaryValue::Create();response->SetString("requestId",id);auto decision=CefDictionaryValue::Create();decision->SetString("response","CancelAuth");response->SetDictionary("authChallengeResponse",decision);send_(session,"Fetch.continueWithAuth",response,[](bool,const std::string&){});}
  Send send_;std::function<void(const std::string&)>failure_;std::function<void()>wake_;Provider provider_;Asker asker_;std::set<std::string>asked_;
  bool disposed_=false,failed_=false;uint64_t serial_=0;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
  std::map<std::string,Attempt>attempts_;std::map<std::string,Pending>pending_;std::map<std::string,Link>links_;
};
}
