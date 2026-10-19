// SPDX-License-Identifier: MIT
#pragma once
#include "file_group_pool.h"
#include <array>
#include <cmath>
#include <span>
namespace suitecut::runner {
// Ownership policy only. Native adapters retain the actual host/controllers;
// SDK idle ACKs never mean waitpid or physical retirement. All storage is
// allocated before the first native host is admitted.
class WorkerReusePool {
 public:
  static constexpr size_t kHosts=8,kGroups=8192,kFixtures=64;
  using Group=FileGroupPool::Admission;
  enum class Outcome {Passed,Failed,Interrupted};
  struct Completion {bool all_done=false,global_stopped=false,native_failure=false,actual_interrupted=false,unexpected_failure=false,all_expected=true;};
  static Outcome CompletionOutcome(const Completion& evidence)noexcept{
    if(evidence.unexpected_failure||evidence.native_failure)return Outcome::Failed;
    if(evidence.actual_interrupted||(!evidence.all_done&&evidence.global_stopped))return Outcome::Interrupted;
    return evidence.all_done&&evidence.all_expected?Outcome::Passed:Outcome::Failed;
  }
  enum class Receipt {Active,IdleTransferred,PhysicalRetired};
  enum class State {Busy,AwaitingACK,AwaitingInventory,Idle,Retiring};
  struct Key {SuiteFileToken file;uint64_t catalog=0;bool operator==(const Key&)const=default;};
  struct Host {uint64_t lease=0;unsigned worker=0;int64_t pid=0;bool operator==(const Host&)const=default;};
  struct Borrow {Host host;Group group;uint64_t generation=0;bool operator==(const Borrow&)const=default;};
  struct Fixture {uint64_t token=0,test=0,generation=0;bool operator==(const Fixture&)const=default;};
  struct Fixtures {std::array<Fixture,kFixtures> values{};size_t count=0;};
  struct GroupFence {
    size_t raw_callbacks=0,steps=0,step_callbacks=0,file_jobs=0,file_callbacks=0,
      file_step_fences=0,output_jobs=0,output_handshakes=0,test_fixtures=0,mutations=0,soft_pending=0,soft_contexts=0;
    bool clean_outcomes=true,control_open=true,fixtures_ready=true;
    Fixtures retained;
  };
  struct IdleRequest {Borrow borrow;uint64_t request=0;double requested=0,deadline=0;size_t fixtures=0;};
  struct IdleACK {Borrow borrow;uint64_t request=0;size_t fixtures=0;};
  struct Probe {uint64_t version=0;std::array<Host,kHosts> hosts{};size_t count=0;};
  struct Inventory {Probe probe;double observed=0;bool complete=false,children_clear=false;};
  struct CohortFence {Borrow borrow;GroupFence fence;};
  struct RetirementFence {
    size_t raw_callbacks=0,steps=0,step_callbacks=0,file_jobs=0,file_callbacks=0,
      file_step_fences=0,output_jobs=0,output_handshakes=0,worker_fixtures=0,test_fixtures=0,soft_pending=0,soft_contexts=0;
    bool descendants_retired=false;
  };
  struct Exit {int code=0;bool signaled=false;};
  struct HostRecord {Host host;std::optional<Exit> exit;bool physical_retired=false;double last_observed=0,exited=0,retired=0;};
  struct BorrowRecord {
    Borrow borrow;std::optional<Outcome> outcome;Receipt receipt=Receipt::Active;
    uint64_t idle_request=0,canceled_idle_request=0;double acknowledged=0,transferred=0,canceled_acknowledged=0;
  };
  WorkerReusePool(Key key,size_t limit,size_t group_capacity,std::optional<double> original_global_deadline=std::nullopt)
      :key_(key),limit_(limit),host_records_(group_capacity),borrows_(group_capacity),groups_(group_capacity),global_deadline_(original_global_deadline){
    if(!key.file.id||!key.catalog||!limit||limit>kHosts||!group_capacity||group_capacity>kGroups)
      throw std::invalid_argument("Native reusable worker key/capacity rejected");
    if(global_deadline_&&(!std::isfinite(*global_deadline_)||*global_deadline_<=0))throw std::invalid_argument("Native reusable worker original deadline rejected");
  }
  std::optional<Borrow> Started(const Key& key,const Group& group,unsigned worker,int64_t pid)noexcept{
    if(stopped_||draining_||key!=key_||!NewGroup(group)||physical_>=limit_||host_count_>=host_records_.size()||
       !worker||worker<=highest_worker_||pid<=0||pid>INT32_MAX||!CanChange())return std::nullopt;
    for(const auto& live:live_)if(live.record&&host_records_[*live.record].host.pid==pid)return std::nullopt;
    const auto free=std::find_if(live_.begin(),live_.end(),[](const auto& row){return !row.record;});
    if(free==live_.end())return std::nullopt;
    const Host host{host_count_+1,worker,pid};host_records_[host_count_].host=host;
    free->record=host_count_++;free->state=State::Busy;++physical_;highest_worker_=worker;
    const auto borrow=NewBorrow(*free,host,group);Changed();return borrow;
  }
  std::optional<Borrow> Reuse(const Host& host,const Key& key,const Group& group)noexcept{
    auto* live=Find(host);
    if(stopped_||draining_||key!=key_||!live||live->state!=State::Idle||host_records_[*live->record].exit||!NewGroup(group)||!CanChange())return std::nullopt;
    live->state=State::Busy;const auto borrow=NewBorrow(*live,host,group);Changed();return borrow;
  }
  bool Complete(const Borrow& borrow,Outcome outcome)noexcept{
    const auto* live=Find(borrow.host);auto* record=Find(borrow);
    if(!live||!Current(*live,borrow)||!record||record->receipt!=Receipt::Active||host_records_[*live->record].physical_retired||record->outcome||(outcome!=Outcome::Passed&&outcome!=Outcome::Failed&&outcome!=Outcome::Interrupted)||!CanChange())return false;
    record->outcome=outcome;Changed();if(outcome!=Outcome::Passed)Drain();return true;
  }
  std::optional<IdleRequest> RequestIdle(const Borrow& borrow,const GroupFence& fence,double now,double deadline)noexcept{
    auto* live=Find(borrow.host);const auto* record=Find(borrow);
    if(stopped_||draining_||!live||live->state!=State::Busy||!Current(*live,borrow)||!record||record->outcome!=Outcome::Passed||
       !Clean(fence)||!Extends(live->fixtures,fence.retained)||!std::isfinite(now)||!std::isfinite(deadline)||now<host_records_[*live->record].last_observed||deadline<=now||
       deadline>now+1000||(global_deadline_&&deadline>*global_deadline_)||request_==UINT64_MAX||!CanChange())return std::nullopt;
    live->fixtures=fence.retained;host_records_[*live->record].last_observed=now;
    live->idle=IdleRequest{borrow,++request_,now,deadline,fence.retained.count};live->state=State::AwaitingACK;Changed();return live->idle;
  }
  bool Acknowledge(const IdleACK& ack,double now)noexcept{
    auto* live=Find(ack.borrow.host);
    if(stopped_||draining_||!live||live->state!=State::AwaitingACK||!live->idle||live->idle->borrow!=ack.borrow||
       live->idle->request!=ack.request||live->idle->fixtures!=ack.fixtures||!std::isfinite(now)||now<live->idle->requested||now>=live->idle->deadline||!CanChange())return false;
    auto& record=borrows_[*live->borrow];record.idle_request=ack.request;record.acknowledged=now;
    host_records_[*live->record].last_observed=now;live->state=State::AwaitingInventory;Changed();return true;
  }
  // A genuine ACK already in flight may arrive during cancellation. Observe
  // its exact request once, without admitting idle or discharging its host.
  bool ObserveCanceledIdleACK(const IdleACK& ack,double now)noexcept{
    auto* live=Find(ack.borrow.host);auto* record=Find(ack.borrow);
    if((!stopped_&&!draining_)||!live||live->state!=State::Retiring||!Current(*live,ack.borrow)||!live->idle||!record||record->receipt!=Receipt::Active||record->idle_request||record->canceled_idle_request||host_records_[*live->record].exit||
       live->idle->borrow!=ack.borrow||live->idle->request!=ack.request||live->idle->fixtures!=ack.fixtures||!std::isfinite(now)||now<live->idle->requested||now<host_records_[*live->record].last_observed||!CanChange())return false;
    record->canceled_idle_request=ack.request;record->canceled_acknowledged=now;host_records_[*live->record].last_observed=now;Changed();return true;
  }
  std::optional<Probe> InventoryProbe()const noexcept{
    if(stopped_||draining_||!physical_)return std::nullopt;
    Probe probe;probe.version=version_;bool pending=false;
    for(const auto& live:live_)if(live.record){
      if(live.state!=State::AwaitingInventory&&live.state!=State::Idle)return std::nullopt;
      pending|=live.state==State::AwaitingInventory;probe.hosts[probe.count++]=host_records_[*live.record].host;
    }
    return pending?std::optional<Probe>(probe):std::nullopt;
  }
  bool ConfirmInventory(const Inventory& inventory,std::span<const CohortFence> fences,double now)noexcept{
    const auto expected=InventoryProbe();
    if(!expected||!inventory.complete||!inventory.children_clear||inventory.probe.version!=expected->version||inventory.probe.count!=expected->count||
       fences.size()!=expected->count||!std::isfinite(now)||!std::isfinite(inventory.observed)||inventory.observed<0||inventory.observed>now||!CanChange())return false;
    for(size_t i=0;i<expected->count;++i){
      if(inventory.probe.hosts[i]!=expected->hosts[i]||fences[i].borrow.host!=expected->hosts[i])return false;
      const auto* live=Find(expected->hosts[i]);
      if(!live||!Current(*live,fences[i].borrow)||!Clean(fences[i].fence)||!SameFixtures(live->fixtures,fences[i].fence.retained))return false;
    }
    for(const auto& live:live_)if(live.record&&live.state==State::AwaitingInventory){
      const auto& record=borrows_[*live.borrow];
      if(!live.idle||inventory.observed<record.acknowledged||now>=live.idle->deadline)return false;
    }
    for(auto& live:live_)if(live.record&&live.state==State::AwaitingInventory){
      auto& record=borrows_[*live.borrow];record.receipt=Receipt::IdleTransferred;record.transferred=now;
      host_records_[*live.record].last_observed=now;live.state=State::Idle;live.idle.reset();
    }
    Changed();return true;
  }
  // Called with an actual known-PID primary waitpid status. Descendants and
  // callback/file/output owners may still be live: physical ownership stays.
  bool PrimaryExited(const Host& host,Exit exit,double now)noexcept{
    auto* live=Find(host);
    if(!live||host_records_[*live->record].exit||exit.code<0||exit.code>255||(exit.signaled&&!exit.code)||!std::isfinite(now)||now<host_records_[*live->record].last_observed||!CanChange())return false;
    auto& record=host_records_[*live->record];record.exit=exit;record.exited=now;record.last_observed=now;Changed();Drain();return true;
  }
  bool Retired(const Host& host,const RetirementFence& fence,double now)noexcept{
    auto* live=Find(host);
    if(!live||!host_records_[*live->record].exit||!RetirementReady(fence)||!std::isfinite(now)||now<host_records_[*live->record].last_observed||!CanChange())return false;
    auto& record=host_records_[*live->record];record.physical_retired=true;record.retired=now;
    if(live->borrow){auto& borrow=borrows_[*live->borrow];if(borrow.receipt==Receipt::Active)borrow.receipt=Receipt::PhysicalRetired;}
    *live=Live{};--physical_;if(!physical_)draining_=false;Changed();return true;
  }
  void Drain()noexcept{
    draining_=physical_!=0;
    for(auto& live:live_)if(live.record)live.state=State::Retiring;
    Changed();
  }
  void Stop()noexcept{stopped_=true;Drain();}
  bool ExpireIdle(double now)noexcept{
    if(!std::isfinite(now))return false;
    for(const auto& live:live_)if(live.record&&live.idle&&(live.state==State::AwaitingACK||live.state==State::AwaitingInventory)&&now>=live.idle->deadline){Drain();return true;}
    return false;
  }
  std::optional<double> NextIdleDeadline()const noexcept{
    std::optional<double> next;
    for(const auto& live:live_)if(live.record&&live.idle&&(live.state==State::AwaitingACK||live.state==State::AwaitingInventory))if(!next||live.idle->deadline<*next)next=live.idle->deadline;
    return next;
  }
  bool Idle(const Host& host)const noexcept{const auto* live=Find(host);return live&&live->state==State::Idle&&!stopped_&&!draining_;}
  bool NeedsRetirement(const Host& host)const noexcept{const auto* live=Find(host);return live&&live->state==State::Retiring;}
  bool Transferred(const Borrow& borrow)const noexcept{const auto* row=Find(borrow);return row&&row->receipt==Receipt::IdleTransferred;}
  bool PhysicallyRetired(const Host& host)const noexcept{return host.lease&&host.lease<=host_count_&&host_records_[host.lease-1].host==host&&host_records_[host.lease-1].physical_retired;}
  size_t Physical()const noexcept{return physical_;}
  size_t RetainedFixtures()const noexcept{size_t count=0;for(const auto& live:live_)if(live.record)count+=live.fixtures.count;return count;}
  bool Draining()const noexcept{return draining_;}
  std::span<const HostRecord> Hosts()const noexcept{return {host_records_.data(),host_count_};}
  std::span<const BorrowRecord> Borrows()const noexcept{return {borrows_.data(),borrow_count_};}
 private:
  struct Live {std::optional<size_t> record,borrow;State state=State::Busy;Fixtures fixtures;std::optional<IdleRequest> idle;};
  static bool Clean(const GroupFence& f)noexcept{return !f.raw_callbacks&&!f.steps&&!f.step_callbacks&&!f.file_jobs&&!f.file_callbacks&&!f.file_step_fences&&!f.output_jobs&&!f.output_handshakes&&!f.test_fixtures&&!f.mutations&&!f.soft_pending&&!f.soft_contexts&&f.clean_outcomes&&f.control_open&&f.fixtures_ready;}
  static bool RetirementReady(const RetirementFence& f)noexcept{return !f.raw_callbacks&&!f.steps&&!f.step_callbacks&&!f.file_jobs&&!f.file_callbacks&&!f.file_step_fences&&!f.output_jobs&&!f.output_handshakes&&!f.worker_fixtures&&!f.test_fixtures&&!f.soft_pending&&!f.soft_contexts&&f.descendants_retired;}
  static bool Valid(const Fixtures& fixtures)noexcept{
    if(fixtures.count>kFixtures)return false;
    uint64_t previous=0;for(size_t i=0;i<fixtures.count;++i){const auto& f=fixtures.values[i];if(!f.token||!f.test||!f.generation||f.token<=previous)return false;previous=f.token;}return true;
  }
  static bool Extends(const Fixtures& previous,const Fixtures& next)noexcept{
    if(!Valid(next))return false;
    for(size_t i=0;i<previous.count;++i){bool found=false;for(size_t j=0;j<next.count;++j)found|=previous.values[i]==next.values[j];if(!found)return false;}return true;
  }
  static bool SameFixtures(const Fixtures& a,const Fixtures& b)noexcept{
    if(a.count!=b.count||!Valid(b))return false;
    for(size_t i=0;i<a.count;++i)if(a.values[i]!=b.values[i])return false;
    return true;
  }
  bool NewGroup(const Group& group)const noexcept{return group.file==key_.file&&group.generation&&group.index<groups_.size()&&!groups_[group.index]&&borrow_count_<borrows_.size();}
  Borrow NewBorrow(Live& live,const Host& host,const Group& group)noexcept{
    const Borrow borrow{host,group,borrow_count_+1};live.borrow=borrow_count_;live.idle.reset();
    groups_[group.index]=borrow_count_;borrows_[borrow_count_++].borrow=borrow;return borrow;
  }
  bool Current(const Live& live,const Borrow& borrow)const noexcept{return live.borrow&&borrows_[*live.borrow].borrow==borrow;}
  const Live* Find(const Host& host)const noexcept{for(const auto& live:live_)if(live.record&&host_records_[*live.record].host==host)return &live;return nullptr;}
  Live* Find(const Host& host)noexcept{return const_cast<Live*>(std::as_const(*this).Find(host));}
  const BorrowRecord* Find(const Borrow& borrow)const noexcept{return borrow.generation&&borrow.generation<=borrow_count_&&borrows_[borrow.generation-1].borrow==borrow?&borrows_[borrow.generation-1]:nullptr;}
  BorrowRecord* Find(const Borrow& borrow)noexcept{return const_cast<BorrowRecord*>(std::as_const(*this).Find(borrow));}
  bool CanChange()const noexcept{return version_<UINT64_MAX;}
  void Changed()noexcept{if(version_<UINT64_MAX)++version_;}
  Key key_;size_t limit_,host_count_=0,borrow_count_=0,physical_=0;unsigned highest_worker_=0;
  uint64_t version_=1,request_=0;bool draining_=false,stopped_=false;
  std::array<Live,kHosts> live_{};std::vector<HostRecord> host_records_;std::vector<BorrowRecord> borrows_;std::vector<std::optional<size_t>> groups_;std::optional<double> global_deadline_;
};
}
