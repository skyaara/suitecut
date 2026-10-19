// SPDX-License-Identifier: MIT
#pragma once
#include <charconv>
#include <deque>
#include <map>
#include <set>
#include "include/cef_parser.h"
#include "network_body_transfers.h"

namespace suitecut {
// Native FIFO publication and byte reservations persist through both transport
// receipt and metadata consumption. Slow receivers apply explicit backpressure.
struct NativeNetworkBodyQuota {
  size_t bytes=0,owners=0,preparations=0;
  bool Reserve(size_t count) {if(owners>=128 || count>16777216-bytes)return false;++owners;bytes+=count;return true;}
  void Release(size_t count) {--owners;bytes-=count;}
};
inline NativeNetworkBodyQuota& GlobalNetworkBodyQuota(){static NativeNetworkBodyQuota quota;return quota;}
class NativeNetworkBodyPublication {
 public:
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
    if(reservations_.size()>=NativeNetworkBodyTransfers::kOwnerLimit || bytes.size()>NativeNetworkBodyTransfers::kByteBudget-reserved_bytes_)return std::nullopt;
    const auto count=bytes.size();if(!GlobalNetworkBodyQuota().Reserve(count))return std::nullopt;const auto id=transfers_.Begin(std::move(bytes),deadline,[this](uint64_t owner,bool ok,const std::string&){
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
  bool Received(uint64_t id) {return transfers_.Acknowledge(id);}
  bool Taken(uint64_t id) {
    const auto found=reservations_.find(id);
    if(found==reservations_.end())return taken_.contains(id);
    if(!found->second.ready)return false;
    GlobalNetworkBodyQuota().Release(found->second.bytes);reserved_bytes_-=found->second.bytes;reservations_.erase(found);
    taken_.insert(id);taken_order_.push_back(id);
    while(taken_order_.size()>4096){taken_.erase(taken_order_.front());taken_order_.pop_front();}
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
    if(waiters_.size()>=128)return false;
    waiters_.emplace(owner,Waiter{body,deadline,consumed,std::move(done)});return true;
  }
  void CancelWait(int owner) {waiters_.erase(owner);}

  // A genuine asynchronous getRequestPostData starts before this publication
  // cursor is queued. Its physical callback is owned separately by the native
  // reader; resolving this gate never claims that receipt has been retired.
  std::optional<uint64_t> Prepare(double deadline) {
    if(!std::isfinite(deadline)||deadline<0||preparations_.size()>=64||GlobalNetworkBodyQuota().preparations>=128||preparation_serial_>=9007199254740991ULL)return std::nullopt;
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
        if(bytes>1048576||bytes>4194304-(event_bytes_-event.bytes)){Fail("Native prepared request metadata exceeds publication capacity");return false;}
        event_bytes_=event_bytes_-event.bytes+bytes;event.bytes=bytes;
      }
      event.preparation=0;
    }
    Pump();return true;
  }
  bool Queue(Dict event,uint64_t preparation=0) {
    auto wrapped=CefValue::Create();wrapped->SetDictionary(event);const auto bytes=CefWriteJSON(wrapped,JSON_WRITER_DEFAULT).ToString().size();
    if(queued_serial_>=9007199254740991ULL || bytes>1048576 || events_.size()>=256 || bytes>4194304-event_bytes_){Fail("Native network metadata publication capacity exceeded");return false;}
    const auto owner=BodyId(event);
    if(owner && !reservations_.contains(owner) && !taken_.contains(owner)){auto request=event->GetDictionary("request");request->Remove("postDataBodyId");request->SetBool("postDataUnavailable",true);}
    events_.push_back({event->Copy(false),bytes,++queued_serial_,preparation});event_bytes_+=bytes;Pump();return true;
  }
  void Tick(double now) {
    std::vector<int> expired;for(const auto& [owner,waiter]:waiters_)if(waiter.deadline>0 && now>=waiter.deadline)expired.push_back(owner);
    for(const auto owner:expired){auto found=waiters_.find(owner);if(found==waiters_.end())continue;auto done=std::move(found->second.done);waiters_.erase(found);done(false,"Native network body command deadline exceeded");}
    std::vector<uint64_t> expired_preparations;for(const auto&[id,deadline]:preparations_)if(deadline>0&&now>=deadline)expired_preparations.push_back(id);
    auto unavailable=CefDictionaryValue::Create();unavailable->SetBool("postDataUnavailable",true);
    for(const auto id:expired_preparations)Prepared(id,unavailable);
    transfers_.Tick(now);
    for(const auto& entry:reservations_)if(entry.second.ready && now>=entry.second.deadline){Fail("Native network body metadata consumption deadline exceeded");return;}
    const std::vector<std::pair<uint64_t,double>> canceled(canceled_.begin(),canceled_.end());
    for(const auto& [id,deadline]:canceled) {
      if(!canceled_.erase(id))continue;
      if(emit_(id,0,0,{},true))continue;
      canceled_.emplace(id,deadline);
      if(now>=deadline)Fail("Native network body cancellation delivery deadline exceeded");
      break;
    }
    Pump();
  }
  void Clear() {GlobalNetworkBodyQuota().preparations-=preparations_.size();preparations_.clear();transfers_.Clear();for(const auto& [id,reservation]:reservations_)GlobalNetworkBodyQuota().Release(reservation.bytes);reservations_.clear();events_.clear();canceled_.clear();waiters_.clear();taken_.clear();taken_order_.clear();reserved_bytes_=event_bytes_=0;failed_=false;}
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
      if(it->second.body==body && (consumed || !it->second.consumed)) {ready.push_back(std::move(it->second.done));it=waiters_.erase(it);}else ++it;
    }
    for(auto& done:ready)done(ok,error);
  }
  struct Reservation {size_t bytes;double deadline;bool ready;};
  struct Event {Dict value;size_t bytes;uint64_t serial,preparation=0;};
  static uint64_t BodyId(Dict event) {
    auto request=event->GetDictionary("request");if(!request)return 0;const auto text=request->GetString("postDataBodyId").ToString();
    uint64_t id=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),id);return parsed.ec==std::errc{} && parsed.ptr==text.data()+text.size()?id:0;
  }
  void Pump() {
    while(!events_.empty()) {
      if(preparations_.contains(events_.front().preparation))break;
      const auto owner=reservations_.find(BodyId(events_.front().value));if(owner!=reservations_.end() && !owner->second.ready)break;
      auto event=std::move(events_.front());events_.pop_front();event_bytes_-=event.bytes;publish_(event.value);published_serial_=std::max(published_serial_,event.serial);
    }
  }
  uint64_t queued_serial_=0,published_serial_=0,preparation_serial_=0;std::map<uint64_t,double>preparations_;
  bool failed_=false;Emit emit_;Publish publish_;Failure failure_;NativeNetworkBodyTransfers transfers_;
  std::map<int,Waiter> waiters_;std::set<uint64_t> taken_;std::deque<uint64_t> taken_order_;
  std::map<uint64_t,Reservation> reservations_;std::map<uint64_t,double> canceled_;std::deque<Event> events_;size_t reserved_bytes_=0,event_bytes_=0;
};
}  // namespace suitecut
