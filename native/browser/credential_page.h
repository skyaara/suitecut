// SPDX-License-Identifier: MIT
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "credential_crypto.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
namespace suitecut {
// Actual native WebAuthn virtual devices and credential imports for one CEF page.
// Commands are physically serialized, including cancellation/repair. Target
// bootstrap callbacks only settle after acknowledgments for the latest config.
class CredentialPage:public std::enable_shared_from_this<CredentialPage>{
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Result=std::function<void(bool,const std::string&)>;
  using Send=std::function<void(const std::string&,const std::string&,Dict,Result)>;
  struct Config{uint64_t version=0;bool installed=false;std::vector<VirtualCredential>credentials;};
  CredentialPage(Send send,std::function<void(bool)>ready):send_(std::move(send)),ready_(std::move(ready)){}
  void Apply(int owner,std::shared_ptr<const Config>config,Result done){
    if(closed_||!config){done(false,"Native credential page is closed");return;}
    if(queue_.size()>=64){done(false,"Native credential page operation capacity exceeded");return;}
    desired_=std::move(config);ready_(false);auto job=std::make_shared<Job>();job->owner=owner;job->config=desired_;job->done=std::move(done);
    for(const auto&[id,session]:sessions_)if(session->active)job->targets.push_back(session);
    queue_.push_back(job);Pump();
  }
  void SessionAttached(const std::string&session,std::shared_ptr<const Config>config,Result done){
    if(closed_||!config){done(false,"Native credential page is closed");return;}
    if(!session.empty()){done(false,"Native credentials use the actual main-frame authenticator inherited by descendants");return;}
    if(queue_.size()>=64||(!sessions_.contains(session)&&sessions_.size()>=1)){done(false,"Native credential target capacity exceeded");return;}
    if(!desired_||config->version>desired_->version)desired_=std::move(config);
    auto&state=sessions_[session];if(!state){state=std::make_shared<Session>();state->id=session;}
    auto job=std::make_shared<Job>();job->bootstrap=true;job->config=desired_;job->targets.push_back(state);job->done=std::move(done);queue_.push_back(job);ready_(false);Pump();
  }
  bool Busy()const{return static_cast<bool>(active_)||!queue_.empty();}
  // Only the exact credential attached to a physically pending addCredential
  // command is an import echo. Busy alone cannot distinguish an authored
  // ceremony from an import; dropping every credentialAdded while busy loses
  // genuine browser-created credential material.
  bool OwnsImportEvent(const std::string&id,const VirtualCredential&credential)const{
    const auto found=sessions_.find(id);
    if(found==sessions_.end()||!found->second->active||!found->second->importing)return false;
    const auto&session=*found->second;
    if(session.import_index>=session.importing->credentials.size())return false;
    const auto&expected=session.importing->credentials[session.import_index];
    return expected.id==credential.id&&expected.rp_id==credential.rp_id&&
      expected.user_handle==credential.user_handle&&expected.private_key==credential.private_key&&
      expected.public_key==credential.public_key&&expected.sign_count==credential.sign_count;
  }
  bool AdoptObserved(std::shared_ptr<const Config>config){
    if(closed_||Busy()||!config||!config->installed||sessions_.empty())return false;
    for(const auto&[id,session]:sessions_)if(!session->active||session->authenticator.empty())return false;
    desired_=std::move(config);for(const auto&[id,session]:sessions_)session->version=desired_->version;ready_(true);return true;
  }
  void WhenReady(Result done){
    if(closed_){done(false,"Native credential page is closed");return;}
    if(Ready()){done(true,"");return;}
    if(ready_waiters_.size()>=64){done(false,"Native credential readiness waiter capacity exceeded");return;}
    ready_waiters_.push_back(std::move(done));
  }
  void SessionDetached(const std::string&id){const auto found=sessions_.find(id);if(found==sessions_.end())return;found->second->active=false;sessions_.erase(found);}
  void Cancel(int owner){if(!owner)return;for(const auto&job:queue_)if(job->owner==owner)job->canceled=true;}
  void Clear(){closed_=true;auto waiting=std::move(ready_waiters_);for(auto&done:waiting)done(false,"Native credential page closed");for(const auto&[id,session]:sessions_)session->active=false;sessions_.clear();auto retired=std::move(queue_);active_.reset();for(auto&job:retired)if(job->done)job->done(false,"Native credential page closed");}
  void Diagnostics(Dict output)const{size_t devices=0,domains=0;for(const auto&[id,session]:sessions_){if(!session->authenticator.empty())++devices;if(session->enabled)++domains;}output->SetInt("credentialEnabledDomains",static_cast<int>(domains));output->SetInt("credentialDevices",static_cast<int>(devices));output->SetInt("credentialTargets",static_cast<int>(sessions_.size()));output->SetInt("credentialPageOperations",static_cast<int>(queue_.size()));output->SetInt("credentialPageWaiters",static_cast<int>(ready_waiters_.size()));}
  std::string Authenticator(const std::string&session)const{const auto found=sessions_.find(session);return found==sessions_.end()?"":found->second->authenticator;}
 private:
  struct Session{std::string id,authenticator;uint64_t version=0;bool active=true,enabled=false;std::shared_ptr<const Config>importing;size_t import_index=0;};
  struct Job{int owner=0;bool bootstrap=false,canceled=false;size_t target=0,credential=0;std::shared_ptr<const Config>config;std::vector<std::shared_ptr<Session>>targets;Result done;};
  static Dict Parse(const std::string&json){const auto value=CefParseJSON(json,JSON_PARSER_RFC);return value&&value->GetType()==VTYPE_DICTIONARY?value->GetDictionary():nullptr;}
  static std::string Standard(const std::string&value,size_t limit){std::vector<unsigned char>decoded;if(!CredentialDecodeBase64URL(value,limit,decoded))return "";return CefBase64Encode(decoded.data(),decoded.size());}
  bool Current(const std::shared_ptr<Job>&job)const{return !closed_&&active_==job;}
  void Pump(){if(closed_||active_||queue_.empty())return;active_=queue_.front();Next(active_);}
  void Next(std::shared_ptr<Job>job){
    if(!Current(job))return;
    if(job->bootstrap&&job->config!=desired_){job->config=desired_;job->target=0;job->credential=0;}
    if(job->canceled||(!job->bootstrap&&job->config!=desired_)){Finish(job,false,"Native credential operation canceled");return;}
    if(job->target>=job->targets.size()){Finish(job,true,"");return;}
    const auto session=job->targets[job->target];
    if(!session->active){if(job->bootstrap){Finish(job,false,"Native credential target detached");return;}++job->target;Next(job);return;}
    if(session->version==job->config->version&&((!job->config->installed&&!session->enabled&&session->authenticator.empty())||(job->config->installed&&!session->authenticator.empty()))){++job->target;Next(job);return;}
    if(!job->config->installed){Disable(job,session);return;}
    if(!session->authenticator.empty()){ClearAndImport(job,session);return;}
    auto params=CefDictionaryValue::Create();params->SetBool("enableUI",false);
    send_(session->id,"WebAuthn.enable",params,[weak=weak_from_this(),job,session](bool ok,const std::string&error){
      const auto self=weak.lock();if(!self||!self->Current(job))return;
      if(!session->active){self->Next(job);return;}if(!ok){self->Finish(job,false,error);return;}
      session->enabled=true;
      if(job->canceled||job->config!=self->desired_){self->Next(job);return;}
      auto options=CefDictionaryValue::Create();options->SetString("protocol","ctap2");options->SetString("transport","internal");options->SetBool("hasResidentKey",true);options->SetBool("hasUserVerification",true);options->SetBool("isUserVerified",true);options->SetBool("automaticPresenceSimulation",true);
      auto params=CefDictionaryValue::Create();params->SetDictionary("options",options);
      self->send_(session->id,"WebAuthn.addVirtualAuthenticator",params,[weak,job,session](bool ok,const std::string&json){const auto self=weak.lock();if(!self||!self->Current(job))return;const auto result=Parse(json);if(!session->active){self->Next(job);return;}if(!ok||!result||result->GetType("authenticatorId")!=VTYPE_STRING||result->GetString("authenticatorId").empty()){self->Finish(job,false,"Native virtual authenticator creation failed: "+json);return;}session->authenticator=result->GetString("authenticatorId");if(job->canceled||job->config!=self->desired_){self->Next(job);return;}self->ClearAndImport(job,session);});
    });
  }
  void Disable(std::shared_ptr<Job>job,std::shared_ptr<Session>session){
    if(!session->authenticator.empty()){
      auto params=CefDictionaryValue::Create();params->SetString("authenticatorId",session->authenticator);
      send_(session->id,"WebAuthn.removeVirtualAuthenticator",params,[weak=weak_from_this(),job,session](bool ok,const std::string&error){
        const auto self=weak.lock();if(!self||!self->Current(job))return;
        if(!session->active){self->Next(job);return;}if(!ok){self->Finish(job,false,error);return;}
        session->authenticator.clear();self->Disable(job,session);
      });return;
    }
    if(session->enabled){
      send_(session->id,"WebAuthn.disable",CefDictionaryValue::Create(),[weak=weak_from_this(),job,session](bool ok,const std::string&error){
        const auto self=weak.lock();if(!self||!self->Current(job))return;
        if(!session->active){self->Next(job);return;}if(!ok){self->Finish(job,false,error);return;}
        session->enabled=false;self->Disable(job,session);
      });return;
    }
    session->version=job->config->version;++job->target;Next(job);
  }
  void ClearAndImport(std::shared_ptr<Job>job,std::shared_ptr<Session>session){
    auto params=CefDictionaryValue::Create();params->SetString("authenticatorId",session->authenticator);
    send_(session->id,"WebAuthn.clearCredentials",params,[weak=weak_from_this(),job,session](bool ok,const std::string&error){const auto self=weak.lock();if(!self||!self->Current(job))return;if(!session->active){self->Next(job);return;}if(!ok){self->Finish(job,false,error);return;}job->credential=0;self->Import(job,session);});
  }
  void Import(std::shared_ptr<Job>job,std::shared_ptr<Session>session){
    if(!Current(job))return;
    if(job->canceled||job->config!=desired_||!session->active){Next(job);return;}
    if(job->credential>=job->config->credentials.size()){session->version=job->config->version;++job->target;Next(job);return;}
    session->importing=job->config;session->import_index=job->credential;
    const auto&item=job->config->credentials[job->credential++];auto credential=CefDictionaryValue::Create();credential->SetString("credentialId",Standard(item.id,1024));credential->SetString("privateKey",Standard(item.private_key,16384));credential->SetString("userHandle",Standard(item.user_handle,64));credential->SetString("rpId",item.rp_id);credential->SetBool("isResidentCredential",true);credential->SetDouble("signCount",item.sign_count);
    auto params=CefDictionaryValue::Create();params->SetString("authenticatorId",session->authenticator);params->SetDictionary("credential",credential);
    send_(session->id,"WebAuthn.addCredential",params,[weak=weak_from_this(),job,session](bool ok,const std::string&error){session->importing.reset();const auto self=weak.lock();if(!self||!self->Current(job))return;if(!session->active){self->Next(job);return;}if(!ok){self->Finish(job,false,error);return;}self->Import(job,session);});
  }
  void Finish(std::shared_ptr<Job>job,bool ok,const std::string&error){
    if(!Current(job))return;
    auto done=std::move(job->done);queue_.pop_front();active_.reset();
    if(ok&&Ready()){ready_(true);auto waiting=std::move(ready_waiters_);for(auto&callback:waiting)callback(true,"");}
    if(done)done(ok,error);
    Pump();
  }
  bool Ready()const{if(!desired_||sessions_.empty())return false;for(const auto&[id,session]:sessions_)if(session->active&&(session->version!=desired_->version||((!desired_->installed)!=session->authenticator.empty())||(!desired_->installed&&session->enabled)))return false;return true;}
  std::vector<Result>ready_waiters_;
  Send send_;std::function<void(bool)>ready_;std::map<std::string,std::shared_ptr<Session>>sessions_;std::deque<std::shared_ptr<Job>>queue_;std::shared_ptr<Job>active_;std::shared_ptr<const Config>desired_;bool closed_=false;
};
}
