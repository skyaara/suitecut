// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <deque>
#include <map>
#include <set>
#include "include/cef_parser.h"
#include "network/network_body_transfers.h"

namespace suitecut {
// Native FIFO publication. Byte reservations are tracked until the receiver
// consumes a body; transport backpressure applies explicitly. A body read or
// transfer that cannot finish in time costs that body, never the page.
// Browser-wide in-flight accounting only; nothing is refused.
struct NativeNetworkBodyQuota {
  size_t bytes=0,owners=0,preparations=0;
  void Reserve(size_t count) {++owners;bytes+=count;}
  void Release(size_t count) {--owners;bytes-=count;}
};
inline NativeNetworkBodyQuota& GlobalNetworkBodyQuota(){static NativeNetworkBodyQuota quota;return quota;}
class NativeNetworkBodyPublication {
 public:
  // A body still preparing or transferring at the head holds the events
  // queued behind it for at most this long; then, like Playwright's immediate
  // request events, that one request is published without its body. A head
  // with nothing queued behind it keeps its body until its own deadline.
  static constexpr double kHeadOfLineMs=1000;
  std::function<void(const std::string&)> degraded;
  using Dict=CefRefPtr<CefDictionaryValue>;
  using Emit=std::function<bool(uint64_t,size_t,size_t,std::span<const uint8_t>,bool)>;
  using Publish=std::function<void(Dict)>;
  using Failure=std::function<void(const std::string&)>;
  using Completion=std::function<void(bool,const std::string&)>;
  NativeNetworkBodyPublication(Emit emit,Publish publish,Failure failure)
      :emit_(std::move(emit)),publish_(std::move(publish)),failure_(std::move(failure)),
       transfers_([this](uint64_t id,size_t offset,size_t total,std::span<const uint8_t> bytes){return emit_(id,offset,total,bytes,false);}){}
  ~NativeNetworkBodyPublication(){Clear();}
  std::optional<uint64_t> Begin(std::vector<uint8_t> bytes,double deadline) {
    const auto count=bytes.size();GlobalNetworkBodyQuota().Reserve(count);const auto id=transfers_.Begin(std::move(bytes),deadline,[this](uint64_t owner,bool ok,const std::string&){
      auto found=reservations_.find(owner);if(found==reservations_.end())return;
      if(ok){found->second.ready=true;CompleteWaiters(owner,false,true,"");Pump();return;}
      for(auto& event:events_)if(BodyId(event.value)==owner){auto request=event.value->GetDictionary("request");request->Remove("postDataBodyId");request->SetBool("postDataUnavailable",true);}
      canceled_[owner]=found->second.deadline+1000;GlobalNetworkBodyQuota().Release(found->second.bytes);reserved_bytes_-=found->second.bytes;reservations_.erase(found);CompleteWaiters(owner,true,false,"Native network body publication expired");Pump();
    });
    if(!id){GlobalNetworkBodyQuota().Release(count);return std::nullopt;}
    reservations_.emplace(*id,Reservation{count,deadline,false});reserved_bytes_+=count;return id;
  }
  bool Cancel(uint64_t id) {
    const auto found=reservations_.find(id);if(found==reservations_.end())return false;
    if(!found->second.ready)return transfers_.Cancel(id);
    for(auto& event:events_)if(BodyId(event.value)==id){auto request=event.value->GetDictionary("request");request->Remove("postDataBodyId");request->SetBool("postDataUnavailable",true);}
    canceled_[id]=found->second.deadline+1000;GlobalNetworkBodyQuota().Release(found->second.bytes);reserved_bytes_-=found->second.bytes;reservations_.erase(found);CompleteWaiters(id,true,false,"Native network body publication canceled");Pump();return true;
  }
  bool Received(uint64_t id) {return transfers_.Issued(id);}
  bool Taken(uint64_t id) {
    const auto found=reservations_.find(id);
    if(found==reservations_.end())return taken_.contains(id)||transfers_.Issued(id);
    if(!found->second.ready)return false;
    GlobalNetworkBodyQuota().Release(found->second.bytes);reserved_bytes_-=found->second.bytes;reservations_.erase(found);
    taken_.insert(id);
    CompleteWaiters(id,true,true,"");return true;
  }
  // Command replies and host-predicate packets use their original native owner
  // and deadline. A canceled command cannot be resurrected by a later receipt.
  bool Wait(uint64_t body,int owner,double deadline,bool consumed,Completion done) {
    if(owner<=0 || !done || !std::isfinite(deadline) || deadline<0 || waiters_.contains(owner))return false;
    if(taken_.contains(body)){done(true,"");return true;}
    const auto found=reservations_.find(body);
    if(found==reservations_.end()){done(false,"Native request body publication is no longer retained");return true;}
    if(found->second.ready && !consumed){done(true,"");return true;}
    waiters_.emplace(owner,Waiter{body,deadline,consumed,std::move(done)});return true;
  }
  void CancelWait(int owner) {waiters_.erase(owner);}

  // A genuine asynchronous getRequestPostData starts before this publication
  // cursor is queued. Its physical callback is owned separately by the native
  // reader; resolving this gate never claims that receipt has been retired.
  std::optional<uint64_t> Prepare(double deadline) {
    if(!std::isfinite(deadline)||deadline<0||preparation_serial_>=9007199254740991ULL)return std::nullopt;
    const auto id=++preparation_serial_;preparations_.emplace(id,deadline);++GlobalNetworkBodyQuota().preparations;return id;
  }
  bool Prepared(uint64_t id,Dict fields) {
    if(!preparations_.erase(id))return false;
    --GlobalNetworkBodyQuota().preparations;
    for(auto&event:events_)if(event.preparation==id){
      auto request=event.value->GetDictionary("request");
      if(request){
        for(const char*name:{"postData","postDataBase64","postDataBodyId","postDataUnavailable"}){
          request->Remove(name);if(fields&&fields->HasKey(name))request->SetValue(name,fields->GetValue(name)->Copy());
        }
        auto wrapper=CefValue::Create();wrapper->SetDictionary(event.value->Copy(false));const auto bytes=CefWriteJSON(wrapper,JSON_WRITER_DEFAULT).ToString().size();
        event_bytes_=event_bytes_-event.bytes+bytes;event.bytes=bytes;
      }
      event.preparation=0;
    }
    Pump();return true;
  }
  bool Queue(Dict event,uint64_t preparation=0) {
    auto wrapped=CefValue::Create();wrapped->SetDictionary(event);const auto bytes=CefWriteJSON(wrapped,JSON_WRITER_DEFAULT).ToString().size();
    if(queued_serial_>=9007199254740991ULL){Fail("Native network metadata publication serial exhausted");return false;}
    const auto owner=BodyId(event);
    if(owner && !reservations_.contains(owner) && !taken_.contains(owner)){auto request=event->GetDictionary("request");request->Remove("postDataBodyId");request->SetBool("postDataUnavailable",true);}
    events_.push_back({event->Copy(false),bytes,++queued_serial_,preparation,-1.0});event_bytes_+=bytes;Pump();return true;
  }
  void Tick(double now) {
    std::vector<int> expired;for(const auto& [owner,waiter]:waiters_)if(waiter.deadline>0 && now>=waiter.deadline)expired.push_back(owner);
    for(const auto owner:expired){auto found=waiters_.find(owner);if(found==waiters_.end())continue;auto done=std::exchange(found->second.done, nullptr);waiters_.erase(found);done(false,"Native network body command deadline exceeded");}
    std::vector<uint64_t> expired_preparations;for(const auto&[id,deadline]:preparations_)if(deadline>0&&now>=deadline)expired_preparations.push_back(id);
    auto unavailable=CefDictionaryValue::Create();unavailable->SetBool("postDataUnavailable",true);
    for(const auto id:expired_preparations)Prepared(id,unavailable);
    // Completions inside the transfer tick are published by the Pump below.
    pumping_=true;transfers_.Tick(now);pumping_=false;
    // The receiver consumes a body when its metadata arrives. A receipt that
    // never comes retires the reservation instead of failing the page.
    std::vector<uint64_t> stale;for(const auto& entry:reservations_)if(entry.second.ready && now>=entry.second.deadline)stale.push_back(entry.first);
    for(const auto id:stale)Retire(id);
    ReleaseHead(now);
    const std::vector<std::pair<uint64_t,double>> canceled(canceled_.begin(),canceled_.end());
    for(const auto& [id,deadline]:canceled) {
      if(!canceled_.erase(id))continue;
      if(emit_(id,0,0,{},true))continue;
      // A cancellation the host hasn't taken in 30 s is dropped, not fatal.
      if(now>=deadline){if(degraded)degraded("Native network body cancellation delivery deadline exceeded");continue;}
      canceled_.emplace(id,deadline);
      break;
    }
    Pump();
  }
  void Clear() {GlobalNetworkBodyQuota().preparations-=preparations_.size();preparations_.clear();transfers_.Clear();for(const auto& [id,reservation]:reservations_)GlobalNetworkBodyQuota().Release(reservation.bytes);reservations_.clear();events_.clear();canceled_.clear();waiters_.clear();taken_.clear();reserved_bytes_=event_bytes_=0;failed_=false;}
  uint64_t QueuedCursor() const{return queued_serial_;}
  uint64_t PublishedCursor() const{return published_serial_;}
  size_t preparations() const {return preparations_.size();}
  size_t owners() const {return reservations_.size();}
  size_t bytes() const {return reserved_bytes_;}
  size_t waiters() const {return waiters_.size();}
  bool NeedsTick() const {return !preparations_.empty() || !reservations_.empty() || !canceled_.empty() || !waiters_.empty();}
 private:
  void Fail(const std::string& message) {if(failed_)return;failed_=true;failure_(message);}
  struct Waiter {uint64_t body;double deadline;bool consumed;Completion done;};
  void CompleteWaiters(uint64_t body,bool consumed,bool ok,const std::string& error) {
    std::vector<Completion> ready;
    for(auto it=waiters_.begin();it!=waiters_.end();) {
      if(it->second.body==body && (consumed || !it->second.consumed)) {ready.push_back(std::exchange(it->second.done, nullptr));it=waiters_.erase(it);}else ++it;
    }
    for(auto& done:ready)done(ok,error);
  }
  struct Reservation {size_t bytes;double deadline;bool ready;};
  struct Event {Dict value;size_t bytes;uint64_t serial,preparation=0;double seen=-1;};
  static uint64_t BodyId(Dict event) {
    auto request=event->GetDictionary("request");if(!request)return 0;const auto text=request->GetString("postDataBodyId").ToString();
    uint64_t id=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),id);return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size()?id:0;
  }
  // Chunks for a waiting head are emitted at once rather than on the next tick.
  void Pump() {
    if(pumping_)return;
    pumping_=true;
    while(!events_.empty()) {
      if(preparations_.contains(events_.front().preparation))break;
      const auto owner=reservations_.find(BodyId(events_.front().value));
      if(owner!=reservations_.end() && !owner->second.ready){if(transfers_.Flush())continue;break;}
      auto event=std::move(events_.front());events_.pop_front();event_bytes_-=event.bytes;publish_(event.value);published_serial_=std::max(published_serial_,event.serial);
    }
    pumping_=false;
  }
  // Gives up the body blocking the head; false when nothing blocks it.
  bool Unblock() {
    if(events_.empty() || pumping_)return false;
    const auto& head=events_.front();
    if(preparations_.contains(head.preparation)){auto unavailable=CefDictionaryValue::Create();unavailable->SetBool("postDataUnavailable",true);Prepared(head.preparation,unavailable);return true;}
    const auto owner=BodyId(head.value);const auto found=reservations_.find(owner);
    return found!=reservations_.end() && !found->second.ready && Cancel(owner);
  }
  // Stamps each event when a tick first sees it waiting; a blocked head is
  // released once the event right behind it has waited kHeadOfLineMs.
  void ReleaseHead(double now) {
    for(auto event=events_.rbegin();event!=events_.rend()&&event->seen<0;++event)event->seen=now;
    while(events_.size()>1 && now-events_[1].seen>=kHeadOfLineMs && Unblock())Pump();
  }
  void Retire(uint64_t id) {
    for(const auto& event:events_)if(BodyId(event.value)==id){Cancel(id);return;}
    Taken(id);
  }
  uint64_t queued_serial_=0,published_serial_=0,preparation_serial_=0;std::map<uint64_t,double>preparations_;
  bool failed_=false,pumping_=false;Emit emit_;Publish publish_;Failure failure_;NativeNetworkBodyTransfers transfers_;
  std::map<int,Waiter> waiters_;std::set<uint64_t> taken_;
  std::map<uint64_t,Reservation> reservations_;std::map<uint64_t,double> canceled_;std::deque<Event> events_;size_t reserved_bytes_=0,event_bytes_=0;
};
}  // namespace suitecut
