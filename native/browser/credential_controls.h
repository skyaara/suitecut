// SPDX-License-Identifier: MIT
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "credential_page.h"
#include "credential_store.h"
namespace suitecut {
class CredentialControls:public std::enable_shared_from_this<CredentialControls>{
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;using Reply=std::function<void(bool,Dict,const std::string&)>;
  struct Hooks{
    std::function<bool(int)>open;
    std::function<std::vector<int>(int)>pages;
    std::function<void(int,int,std::shared_ptr<const CredentialPage::Config>,CredentialPage::Result)>apply;
    std::function<void(int,int)>cancel;
    std::function<bool(int,std::shared_ptr<const CredentialPage::Config>)>adopt_observed;
    std::function<bool(int,std::shared_ptr<const CredentialPage::Config>)>publish;
    std::function<bool(int,int,std::shared_ptr<const CredentialPage::Config>)>publish_owned;
    std::function<void(int,const std::string&)>fatal;
  };
  explicit CredentialControls(Hooks hooks):hooks_(std::move(hooks)){}
  void ContextCreated(int context);
  void ContextClosed(int context);
  void Handle(int owner,int context,const std::string&action,Dict params,Reply reply);
  bool ValidateState(CefRefPtr<CefListValue>input,std::string&error)const;
  void Restore(int owner,int context,CefRefPtr<CefListValue>input,bool installed,Reply reply);
  Dict Capture(int context)const;
  std::shared_ptr<const CredentialPage::Config>Config(int context)const;
  bool Observed(int context,int page,const VirtualCredential&credential,std::string&error);
  void Cancel(int owner);
  void Clear();
  void Diagnostics(Dict output)const;
  bool AcceptedWorkReady()const noexcept{return work_.empty();}
 private:
  struct Work;
  struct Profile{CredentialStore store;bool installed=false;uint64_t version=0;std::deque<std::shared_ptr<Work>>queue;};
  static Dict Record(const VirtualCredential&credential);
  static bool Parse(Dict value,VirtualCredential&credential,std::string&error);
  static size_t Bytes(const std::vector<VirtualCredential>&items);
  void Enqueue(std::shared_ptr<Work>work);
  void Pump(int context);
  void Start(std::shared_ptr<Work>work);
  void Dispatch(std::shared_ptr<Work>work,bool rollback);
  void Settled(std::shared_ptr<Work>work,int page,bool ok,const std::string&error);
  void Finish(std::shared_ptr<Work>work,bool ok,Dict result,const std::string&error);
  Hooks hooks_;std::map<int,Profile>profiles_;std::map<int,std::shared_ptr<Work>>work_;size_t retained_bytes_=0;int next_observation_=-1;bool closed_=false;
};
}
