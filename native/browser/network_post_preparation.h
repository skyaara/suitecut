// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "network_body_publication.h"
#include "har_work_receipts.h"

namespace suitecut {
// Native post lookup and metadata publication have different lifetimes. A
// logical timeout releases metadata honestly; its original archive receipt is
// retained by the physical reader until a genuine ACK or channel destruction.
class NativeNetworkPostPreparation {
 public:
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Bytes=std::vector<uint8_t>;
  using Data=std::function<void(bool,Bytes,bool,const std::string&)>;
  using Receipt=std::function<void()>;
  struct Reservation {std::function<void(bool,const Bytes&,bool)> data;Receipt receipt;};
  using Read=std::function<void(const std::string&,const std::string&,double,Data,Receipt)>;
  using Reserve=std::function<Reservation(const std::string&)>;
  using Apply=std::function<void(const std::string&,Dict,bool,const Bytes&,bool)>;
  NativeNetworkPostPreparation(NativeNetworkBodyPublication&publication,Read read,Reserve reserve,Apply apply)
      :publication_(publication),read_(std::move(read)),reserve_(std::move(reserve)),apply_(std::move(apply)){}
  ~NativeNetworkPostPreparation(){Clear();}
  uint64_t Prepare(const std::string&logical,const std::string&session,const std::string&request,double deadline,bool publish=true){
    if(closed_||!read_||logical.empty()||logical.size()>256||session.size()>128||request.empty()||request.size()>256)return 0;
    const auto found=jobs_.find(logical);if(found!=jobs_.end())return found->second.gate;
    const auto gate=publication_.Prepare(deadline);if(!gate)return 0;
    jobs_.emplace(logical,Job{*gate,session,request,deadline,false,std::make_shared<bool>(true),publish});return *gate;
  }
  uint64_t Gate(const std::string&logical)const{const auto found=jobs_.find(logical);return found==jobs_.end()?0:found->second.gate;}
  // A replay admitted while this real body command is pending retains its
  // immutable archive until the independent physical receipt, not Data().
  bool Retain(const std::string&logical,std::shared_ptr<const void> owner){
    const auto found=jobs_.find(logical);if(found==jobs_.end()||!owner)return false;
    if(found->second.owners->settled)return true;
    auto&owners=found->second.owners->values;
    for(const auto&known:owners)if(known.owner.get()==owner.get())return true;
    if(owners.size()>=16)return false;
    owners.push_back({std::move(owner),{}});return true;
  }
  bool RetainHAR(const std::string&logical,uint64_t scope,NativeHARRouteRegistration::Policy policy){
    const auto found=jobs_.find(logical);if(found==jobs_.end()||!scope||!policy)return false;
    auto retained=found->second.owners;if(retained->settled)return true;
    auto&owners=retained->values;auto known=std::find_if(owners.begin(),owners.end(),[&](const Retention&entry){return entry.owner.get()==policy.get();});
    if(known!=owners.end()&&known->acknowledge)return true;
    if(known==owners.end()&&owners.size()>=16)return false;
    // Reserve storage BEFORE issuing physical work. The subsequent moves of
    // shared_ptr/std::function cannot leave an admitted receipt unrecorded.
    owners.reserve(16);known=std::find_if(owners.begin(),owners.end(),[&](const Retention&entry){return entry.owner.get()==policy.get();});
    auto work=GlobalHARWorkLeases().Issue(scope,found->second.session,policy,NativeHARWorkLeases::Kind::PostRead);if(!work)return false;
    if(known!=owners.end())known->acknowledge=std::move(work->acknowledge);
    else owners.push_back({std::move(policy),std::move(work->acknowledge)});
    return true;
  }
  void Invalidate(const std::string&logical){const auto found=jobs_.find(logical);if(found==jobs_.end())return;const auto job=found->second;*job.valid=false;jobs_.erase(found);auto unavailable=CefDictionaryValue::Create();unavailable->SetBool("postDataUnavailable",true);publication_.Prepared(job.gate,unavailable);}
  // Call only after observing/queuing the genuine initial request. The archive
  // reservation then binds the already-admitted record and original trace lease.
  void Start(const std::string&logical){
    const auto found=jobs_.find(logical);if(closed_||found==jobs_.end()||found->second.started)return;
    found->second.started=true;const auto job=found->second;auto reservation=reserve_?reserve_(logical):Reservation{};
    auto read=read_;
    read(job.session,job.request,job.deadline,
      [this,alive=alive_,logical,valid=job.valid,gate=job.gate,deadline=job.deadline,publish=job.publish,owners=job.owners,data=std::move(reservation.data)](bool ok,Bytes bytes,bool text,const std::string&error){
        if(!owners->observed&&!owners->settled){owners->observed=true;owners->ok=ok;owners->error.assign(error.data(),std::min<size_t>(2048,error.size()));}
        // Even after logical metadata cancellation, the original record receives
        // its honest data outcome. Its separate physical receipt settles later.
        if(!*valid){ok=false;Bytes().swap(bytes);text=false;}
        if(data)data(ok,bytes,text);
        if(!*alive)return;
        const auto found=jobs_.find(logical);if(found==jobs_.end()||found->second.gate!=gate)return;
        jobs_.erase(found);auto fields=CefDictionaryValue::Create();
        if(ok&&text&&(!publish||bytes.size()<=524288))fields->SetString("postData",std::string(bytes.begin(),bytes.end()));
        else if(ok&&!publish)fields->SetString("postDataBase64",CefBase64Encode(bytes.data(),bytes.size()));
        else if(ok){const auto body=publication_.Begin(bytes,deadline);if(body)fields->SetString("postDataBodyId",std::to_string(*body));else fields->SetBool("postDataUnavailable",true);}
        else fields->SetBool("postDataUnavailable",true);
        auto apply=apply_;apply(logical,fields,ok,bytes,text);if(!*alive)return;publication_.Prepared(gate,fields);
      },[owners=job.owners,receipt=std::move(reservation.receipt)]()mutable{if(owners->settled)return;
        owners->settled=true;auto work=std::move(owners->values);owners->values.clear();
        for(auto&entry:work)if(entry.acknowledge)entry.acknowledge(owners->observed&&owners->ok,owners->observed?owners->error:"Native HAR body receipt arrived without its actual data outcome");
        if(receipt){auto actual=std::move(receipt);actual();}});
  }
  void Clear(){if(closed_)return;closed_=true;*alive_=false;
    // Admission hooks can strongly retain their owning Source. They are no
    // longer callable after Close; actual dispatched readers independently own
    // their callbacks and physical receipts. Retire hooks only at function exit
    // so releasing an owner cannot destroy this object mid-cleanup.
    auto read=std::move(read_);auto reserve=std::move(reserve_);auto apply=std::move(apply_);
    auto jobs=std::move(jobs_);jobs_.clear();auto unavailable=CefDictionaryValue::Create();unavailable->SetBool("postDataUnavailable",true);for(const auto&[id,job]:jobs){*job.valid=false;publication_.Prepared(job.gate,unavailable);}}
  size_t pending()const{return jobs_.size();}
 private:
  struct Retention{std::shared_ptr<const void>owner;NativeHARWorkLeases::Ack acknowledge;};
  struct Retentions{Retentions(){error.reserve(2048);}bool settled=false,observed=false,ok=false;std::string error;std::vector<Retention>values;};
  struct Job{uint64_t gate;std::string session,request;double deadline;bool started;std::shared_ptr<bool>valid=std::make_shared<bool>(true);bool publish=true;std::shared_ptr<Retentions>owners=std::make_shared<Retentions>();};
  NativeNetworkBodyPublication&publication_;Read read_;Reserve reserve_;Apply apply_;std::map<std::string,Job>jobs_;
  bool closed_=false;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
};
} // namespace suitecut
