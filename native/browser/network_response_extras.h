// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_parser.h"

namespace suitecut {
// ExtraInfo has a physical ID shared by every redirect hop and no URL/hop ID.
// Match its receipt FIFO only to hops explicitly marked by genuine protocol
// redirectHasExtraInfo/hasExtraInfo. A late receipt can never become the next
// hop's headers merely because that request is currently in the map. Request
// and response receipts each use their own bounded instance and native stream.
class NativeNetworkResponseExtras {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Apply=std::function<void(const std::string&,Dict)>;
  using Missing=std::function<void(const std::string&,const std::string&)>;
  NativeNetworkResponseExtras(Apply apply,Missing missing,std::function<void(const std::string&)>failure,std::string kind="response",size_t byte_limit=8388608)
      :kind_(std::move(kind)),byte_limit_(byte_limit),apply_(std::move(apply)),missing_(std::move(missing)),failure_(std::move(failure)){}
  ~NativeNetworkResponseExtras(){Clear();}
  bool Expect(const std::string&key,const std::string&logical,bool expected,double deadline){
    if(!Valid(key)||logical.empty()||logical.size()>256)return Fail("Invalid native "+kind_+" ExtraInfo hop identity");
    const auto canonical=Resolve(key);if(canonical!=key)return Expect(canonical,logical,expected,deadline);
    if(!expected)return true;
    auto found=chains_.find(key);
    if(found!=chains_.end()&&found->second.seen.contains(logical))return true;
    const size_t cost=logical.size()*(expected?2:1);
    if(hops_>=4096||cost>byte_limit_-bytes_||(!chains_.contains(key)&&key.size()>byte_limit_-bytes_-cost))return Fail("Native "+kind_+" ExtraInfo hop capacity exceeded");
    if(!Ensure(key,deadline))return false;
    auto&chain=chains_.at(key);chain.seen.insert(logical);++hops_;chain.bytes+=cost;bytes_+=cost;
    if(expected)chain.expected.push_back(logical);
    Drain(key);return true;
  }
  bool Receive(const std::string&key,Dict headers,double deadline){
    if(!Valid(key)||!headers)return Fail("Invalid native "+kind_+" ExtraInfo receipt");
    const auto canonical=Resolve(key);if(canonical!=key)return Receive(canonical,headers,deadline);
    auto wrapper=CefValue::Create();wrapper->SetDictionary(headers->Copy(false));const auto cost=CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).ToString().size();
    if(cost>1048576||receipts_>=4096||cost>byte_limit_-bytes_||(!chains_.contains(key)&&key.size()>byte_limit_-bytes_-cost))return Fail("Native "+kind_+" ExtraInfo receipt capacity exceeded");
    if(!Ensure(key,deadline))return false;
    auto&chain=chains_.at(key);chain.receipts.push_back({headers->Copy(false),cost});++receipts_;chain.bytes+=cost;bytes_+=cost;Drain(key);return true;
  }
  void Complete(const std::string&key){const auto canonical=Resolve(key);const auto found=chains_.find(canonical);if(found==chains_.end()){DropAliases(canonical);return;}found->second.complete=true;Retire(canonical);}
  // Only the existing native exact-ID handoff can create these bounded aliases.
  // ExtraInfo can stay on the original network observer after response/body
  // ownership has migrated, including after the active request has finished.
  bool Move(const std::string&old_key,const std::string&new_key,double deadline=0){
    if(closed_)return false;
    if(old_key==new_key)return true;
    if(!Valid(old_key)||!Valid(new_key)||Resolve(old_key)!=old_key||chains_.contains(new_key))return Fail("Conflicting native "+kind_+" ExtraInfo session handoff");
    const auto chain=chains_.find(old_key);const auto previous=aliases_.find(new_key);
    if(previous!=aliases_.end()&&previous->second.target!=old_key)return Fail("Conflicting native "+kind_+" ExtraInfo alias destination");
    for(const auto&[key,alias]:aliases_)if(alias.target==new_key)return Fail("Conflicting native "+kind_+" ExtraInfo canonical destination");
    if(chain!=chains_.end())deadline=chain->second.deadline;
    for(const auto&[key,alias]:aliases_)if(alias.target==old_key)deadline=deadline?std::min(deadline,alias.deadline):alias.deadline;
    if(!std::isfinite(deadline)||deadline<=0)return Fail("Invalid native "+kind_+" ExtraInfo handoff deadline");
    size_t next=bytes_;
    if(chain!=chains_.end())next=next-old_key.size()+new_key.size();
    for(const auto&[key,alias]:aliases_)if(alias.target==old_key&&key!=new_key)next=next-alias.target.size()+new_key.size();
    if(previous!=aliases_.end())next-=previous->first.size()+previous->second.target.size();
    const auto count=aliases_.size()-(previous!=aliases_.end()?1:0)+1;next+=old_key.size()+new_key.size();
    if(count>4096||next>byte_limit_)return Fail("Native "+kind_+" ExtraInfo handoff metadata capacity exceeded");
    // All admission is checked before mutating either the FIFO or alias graph.
    if(previous!=aliases_.end())aliases_.erase(previous);
    for(auto&[key,alias]:aliases_)if(alias.target==old_key){std::string target(new_key);alias.target.swap(target);}
    aliases_.emplace(old_key,Alias{new_key,deadline});
    if(chain!=chains_.end()){auto moved=chains_.extract(chain);std::string actual(new_key);moved.key().swap(actual);moved.mapped().bytes=moved.mapped().bytes-old_key.size()+new_key.size();chains_.insert(std::move(moved));}
    bytes_=next;return true;
  }
  void SessionClosed(const std::string&session){std::vector<std::string>keys;for(const auto&[key,chain]:chains_)if(key.starts_with(session+"\n"))keys.push_back(key);const auto alive=alive_;for(const auto&key:keys){Drop(key,"Actual "+kind_+" ExtraInfo target closed");if(!*alive)return;}keys.clear();for(const auto&[key,alias]:aliases_)if(alias.target.starts_with(session+"\n"))keys.push_back(key);for(const auto&key:keys)EraseAlias(key);}
  void Tick(double now){std::vector<std::string>expired;for(const auto&[key,chain]:chains_)if(now>=chain.deadline)expired.push_back(key);const auto alive=alive_;for(const auto&key:expired){Drop(key,"Native "+kind_+" ExtraInfo retention deadline exceeded");if(!*alive)return;}expired.clear();for(const auto&[key,alias]:aliases_)if(now>=alias.deadline)expired.push_back(key);for(const auto&key:expired)EraseAlias(key);}
  void Clear(){if(closed_)return;closed_=true;*alive_=false;chains_.clear();aliases_.clear();bytes_=hops_=receipts_=0;}
  bool NeedsTick()const{return !chains_.empty()||!aliases_.empty();}
  size_t bytes()const{return bytes_;}size_t hops()const{return hops_;}size_t receipts()const{return receipts_;}size_t aliases()const{return aliases_.size();}
 private:
  struct Receipt{Dict headers;size_t cost;};
  struct Chain{std::deque<std::string>expected;std::deque<Receipt>receipts;std::set<std::string>seen;size_t bytes=0;double deadline=0;bool complete=false;};
  struct Alias{std::string target;double deadline;};
  std::string Resolve(const std::string&key)const{const auto found=aliases_.find(key);return found==aliases_.end()?key:found->second.target;}
  void EraseAlias(const std::string&key){const auto found=aliases_.find(key);if(found==aliases_.end())return;bytes_-=found->first.size()+found->second.target.size();aliases_.erase(found);}
  void DropAliases(const std::string&target){std::vector<std::string>keys;for(const auto&[key,alias]:aliases_)if(alias.target==target)keys.push_back(key);for(const auto&key:keys)EraseAlias(key);}
  static bool Valid(const std::string&key){return !key.empty()&&key.size()<=385;}
  bool Fail(const std::string&error){auto failure=failure_;failure(error);return false;}
  bool Ensure(const std::string&key,double deadline){
    if(closed_)return false;
    if(!std::isfinite(deadline)||deadline<=0)return Fail("Invalid native "+kind_+" ExtraInfo retention deadline");
    if(chains_.contains(key))return true;
    if(chains_.size()>=4096||key.size()>byte_limit_-bytes_)return Fail("Native "+kind_+" ExtraInfo channel capacity exceeded");
    Chain chain;chain.deadline=deadline;chain.bytes=key.size();bytes_+=key.size();chains_.emplace(key,std::move(chain));return true;
  }
  void Drain(const std::string&key){
    const auto alive=alive_;
    while(*alive){const auto found=chains_.find(key);if(found==chains_.end()||found->second.expected.empty()||found->second.receipts.empty())break;
      auto&chain=found->second;auto logical=std::move(chain.expected.front());chain.expected.pop_front();auto receipt=std::move(chain.receipts.front());chain.receipts.pop_front();const auto cost=logical.size()+receipt.cost;chain.bytes-=cost;bytes_-=cost;--receipts_;
      auto apply=apply_;apply(logical,receipt.headers);
    }
    if(*alive)Retire(key);
  }
  void Retire(const std::string&key){const auto found=chains_.find(key);if(found==chains_.end()||!found->second.complete||!found->second.expected.empty()||!found->second.receipts.empty())return;bytes_-=found->second.bytes;hops_-=found->second.seen.size();chains_.erase(found);DropAliases(key);}
  void Drop(const std::string&key,const std::string&error){const auto found=chains_.find(key);if(found==chains_.end())return;auto expected=std::move(found->second.expected);bytes_-=found->second.bytes;hops_-=found->second.seen.size();receipts_-=found->second.receipts.size();chains_.erase(found);DropAliases(key);const auto alive=alive_;auto missing=missing_;for(const auto&logical:expected){missing(logical,error);if(!*alive)return;}}
  const std::string kind_;const size_t byte_limit_;
  Apply apply_;Missing missing_;std::function<void(const std::string&)>failure_;std::map<std::string,Chain>chains_;std::map<std::string,Alias>aliases_;
  size_t bytes_=0,hops_=0,receipts_=0;bool closed_=false;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
};
} // namespace suitecut
