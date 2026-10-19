// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include "route_callback_leases.h"
#include "route_registry.h"

namespace suitecut {
// Language callbacks only decide one native candidate. The registry owns
// snapshots, ordering and consumption; the callback lease owns physical work.
class NativeRouteMatcherSelections {
 public:
  struct Ticket {
    std::string id,registration,url;
    uint64_t scope=0,token=0;bool context=false;
  };
  enum class Failure {None,InvalidResult,Deadline,Capacity,OwnerClosed};
  using Done=std::function<void(std::optional<NativeRouteRegistry::Invocation>,NativeRouteRegistry::Plan,const std::string&,Failure)>;
  using Emit=std::function<void(const Ticket&)>;
  NativeRouteMatcherSelections(NativeRouteRegistry&registry,NativeRouteCallbackLeases&leases,uint64_t scope,Emit emit):registry_(registry),leases_(leases),scope_(scope),emit_(std::move(emit)){}
  ~NativeRouteMatcherSelections(){Clear();*alive_=false;}
  NativeRouteMatcherSelections(const NativeRouteMatcherSelections&)=delete;
  NativeRouteMatcherSelections&operator=(const NativeRouteMatcherSelections&)=delete;
  bool Select(NativeRouteRegistry::Plan plan,std::string original_url,double deadline,double now,size_t retained_payload,Done done,Emit emit={},std::string owner={}){
    if(!std::isfinite(deadline)||!std::isfinite(now)||now>=deadline||original_url.empty()||original_url.size()>8192||owner.size()>128||!done)return false;
    if(retained_payload>kBytes||plan.bytes()>kBytes||original_url.capacity()>kBytes)return false;
    const size_t bytes=sizeof(Entry)+plan.bytes()+original_url.capacity()+owner.capacity()+retained_payload;
    if(serial_==UINT64_MAX||global_count_>=128||bytes>kBytes-global_bytes_)return false;
    const auto id=++serial_;Entry entry;entry.plan=std::move(plan);entry.url=std::move(original_url);entry.deadline=deadline;entry.bytes=bytes;entry.done=std::move(done);entry.emit=std::move(emit);entry.owner=std::move(owner);
    ++global_count_;global_bytes_+=bytes;entries_.emplace(id,std::move(entry));Advance(id);return true;
  }
  // Error results may abort logically while an invalid async continuation is
  // still running. A boolean result requires its real finish acknowledgment.
  bool Result(const Ticket&ticket,std::optional<bool> matched,double now,bool*skipped=nullptr){
    const auto found=entries_.find(ticket_serial(ticket.id));
    if(found==entries_.end())return false;
    auto&entry=found->second;
    if(!entry.candidate||entry.ticket.id!=ticket.id||entry.ticket.registration!=ticket.registration||ticket.scope!=scope_||ticket.token!=entry.ticket.token)return false;
    const auto id=found->first;
    if(!std::isfinite(now)||now>=entry.deadline){Complete(id,{},"Native route URL matcher exceeded its original deadline",Failure::Deadline);return false;}
    // Native removal/exhaustion is not an authored false or invalid result.
    // Skip the unavailable original generation even if its queued host packet
    // reaches an SDK which has already removed that registration.
    const auto current=registry_.Peek(entry.plan);
    if(!current||current->index!=entry.candidate->index){leases_.Revoke(scope_,entry.ticket.id);entry.candidate.reset();entry.receipt.reset();entry.ticket={};if(skipped)*skipped=true;Advance(id);return true;}
    if(!matched){Complete(id,{},"Native route URL matcher did not return a synchronous boolean",Failure::InvalidResult);return true;}
    if(!entry.receipt||entry.receipt->unavailable()||!entry.receipt->settled())return false;
    const auto candidate=*entry.candidate;entry.candidate.reset();entry.receipt.reset();entry.ticket={};
    if(auto invocation=registry_.Decide(entry.plan,candidate,*matched)){Complete(id,std::move(invocation),{});return true;}
    Advance(id);return true;
  }
  void Tick(double now){
    auto alive=alive_;std::vector<uint64_t>expired;
    for(const auto&[id,entry]:entries_)if(!std::isfinite(now)||now>=entry.deadline)expired.push_back(id);
    for(const auto id:expired){if(!*alive)return;if(entries_.contains(id))Complete(id,{},"Native route URL matcher exceeded its original deadline",Failure::Deadline);}
  }
  // Actual owner closure drops logical continuations. Running language work
  // remains retained by leases until its genuine receipt/process boundary.
  void CancelOwner(const std::string&owner){
    auto alive=alive_;std::vector<uint64_t>closed;for(const auto&[id,entry]:entries_)if(entry.owner==owner)closed.push_back(id);
    for(const auto id:closed){if(!*alive)return;if(entries_.contains(id))Complete(id,{},"Actual native URL matcher channel closed",Failure::OwnerClosed);}
  }
  void Cancel(const std::string&id){const auto found=entries_.find(ticket_serial(id));if(found!=entries_.end())Erase(found);}
  void Clear(){while(!entries_.empty())Erase(entries_.begin());}
  size_t pending()const{return entries_.size();}
  static size_t retained_count(){return global_count_;}
  static size_t retained_bytes(){return global_bytes_;}
 private:
  struct Entry {
    NativeRouteRegistry::Plan plan;std::string url,owner;double deadline=0;size_t bytes=0,selection_bytes=0;Done done;Emit emit;
    std::optional<NativeRouteRegistry::Candidate>candidate;Ticket ticket;
    std::shared_ptr<const NativeRouteCallbackReceipt>receipt;
  };
  // IDs are generated here; caller strings are never parsed as owner identity.
  static uint64_t ticket_serial(const std::string&id){
    if(id.size()<7||id.size()>128||id.compare(0,6,"match:")!=0)return 0;
    uint64_t value=0;for(size_t i=6;i<id.size();++i){const auto c=id[i];if(c<'0'||c>'9'||value>(UINT64_MAX-static_cast<unsigned>(c-'0'))/10)return 0;value=value*10+static_cast<unsigned>(c-'0');}return value;
  }
  void Advance(uint64_t id){
    const auto found=entries_.find(id);if(found==entries_.end())return;auto&entry=found->second;
    while(auto candidate=registry_.Peek(entry.plan)){
      if(!candidate->host_matcher){if(auto invocation=registry_.Decide(entry.plan,*candidate,true)){Complete(id,std::move(invocation),{});return;}continue;}
      Ticket ticket{"match:"+std::to_string(id),candidate->registration,entry.url,scope_,0,candidate->policy!=nullptr};
      // Reserve actual retained string capacities and the emitter's local
      // ticket before publishing or issuing physical ownership. Keep the peak
      // charged until this whole selection finishes, including later false hops.
      const size_t selection_bytes=2*(ticket.id.capacity()+ticket.registration.capacity()+ticket.url.capacity())+candidate->registration.capacity();
      if(selection_bytes>entry.selection_bytes){const auto extra=selection_bytes-entry.selection_bytes;if(extra>kBytes-global_bytes_){Complete(id,{},"Native route URL matcher metadata capacity exceeded",Failure::Capacity);return;}global_bytes_+=extra;entry.bytes+=extra;entry.selection_bytes=selection_bytes;}
      auto token=leases_.Issue(scope_,ticket.id,candidate->policy);
      if(!token){Complete(id,{},"Native route URL matcher callback capacity exceeded",Failure::Capacity);return;}
      ticket.token=*token;entry.candidate=std::move(candidate);entry.receipt=leases_.Receipt(*token,scope_,ticket.id);entry.ticket=ticket;
      auto emit=entry.emit?entry.emit:emit_;emit(ticket);return;
    }
    Complete(id,{},{});
  }
  void Complete(uint64_t id,std::optional<NativeRouteRegistry::Invocation>invocation,const std::string&error,Failure failure=Failure::None){
    const auto found=entries_.find(id);if(found==entries_.end())return;
    auto done=std::move(found->second.done);auto plan=std::move(found->second.plan);Erase(found);
    done(std::move(invocation),std::move(plan),error,failure);
  }
  void Erase(std::map<uint64_t,Entry>::iterator found){
    if(!found->second.ticket.id.empty())leases_.Revoke(scope_,found->second.ticket.id);
    --global_count_;global_bytes_-=found->second.bytes;entries_.erase(found);
  }
  static constexpr size_t kBytes=4194304;
  inline static size_t global_count_=0,global_bytes_=0;
  NativeRouteRegistry&registry_;NativeRouteCallbackLeases&leases_;uint64_t scope_,serial_=0;Emit emit_;
  std::map<uint64_t,Entry>entries_;std::shared_ptr<bool>alive_=std::make_shared<bool>(true);
};
}
