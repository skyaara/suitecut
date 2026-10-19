// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace suitecut {
// Wire identities are supplied only by native browser/renderer ownership.
// A document URL, authored integer, or independently inferred version is not
// evidence that a receiver owns this transport.
struct InitSnapshotOwner {
  uint64_t context=0,browser=0,renderer_epoch=0;
  std::string frame;
  bool operator==(const InitSnapshotOwner&)const=default;
};
struct InitSnapshotPin {
  InitSnapshotOwner owner;
  uint64_t version=0,transfer=0;
  bool operator==(const InitSnapshotPin&)const=default;
};
class InitSnapshotWire {
 public:
  static constexpr size_t kHeader=64,kFrame=256,kPayload=8*1048576;
  static constexpr std::string_view kMessage="suitecutInitSnapshotRegionV1";
  struct View {InitSnapshotPin pin;std::string_view payload;};
  static bool ValidOwner(const InitSnapshotOwner&owner){
    return owner.context>0&&owner.context<=std::numeric_limits<int>::max()&&
      owner.browser>0&&owner.browser<=std::numeric_limits<int>::max()&&
      owner.renderer_epoch>0&&!owner.frame.empty()&&owner.frame.size()<=kFrame&&
      owner.frame.find('\0')==std::string::npos;
  }
  static size_t Size(const InitSnapshotPin&pin,size_t payload){
    return ValidOwner(pin.owner)&&pin.version&&pin.transfer&&payload&&payload<=kPayload?
      kHeader+pin.owner.frame.size()+payload:0;
  }
  static bool Encode(std::span<uint8_t>bytes,const InitSnapshotPin&pin,std::string_view payload){
    if(!Size(pin,payload.size())||bytes.size()!=Size(pin,payload.size()))return false;
    std::copy(kMagic.begin(),kMagic.end(),bytes.begin());
    Put(bytes,8,1,4);Put(bytes,12,kHeader,4);
    Put(bytes,16,pin.owner.context);Put(bytes,24,pin.owner.browser);
    Put(bytes,32,pin.owner.renderer_epoch);Put(bytes,40,pin.version);Put(bytes,48,pin.transfer);
    Put(bytes,56,pin.owner.frame.size(),4);Put(bytes,60,payload.size(),4);
    std::memcpy(bytes.data()+kHeader,pin.owner.frame.data(),pin.owner.frame.size());
    std::memcpy(bytes.data()+kHeader+pin.owner.frame.size(),payload.data(),payload.size());
    return true;
  }
  static std::optional<View>Decode(std::span<const uint8_t>bytes){
    if(bytes.size()<kHeader||!std::equal(kMagic.begin(),kMagic.end(),bytes.begin())||
       Get(bytes,8,4)!=1||Get(bytes,12,4)!=kHeader)return std::nullopt;
    const auto frame=Get(bytes,56,4),payload=Get(bytes,60,4);
    if(!frame||frame>kFrame||!payload||payload>kPayload||bytes.size()!=kHeader+frame+payload)return std::nullopt;
    View view{{{Get(bytes,16),Get(bytes,24),Get(bytes,32),
      std::string(reinterpret_cast<const char*>(bytes.data()+kHeader),frame)},Get(bytes,40),Get(bytes,48)},
      std::string_view(reinterpret_cast<const char*>(bytes.data()+kHeader+frame),payload)};
    if(!Size(view.pin,payload))return std::nullopt;
    return view;
  }
 private:
  inline static constexpr std::string_view kMagic="SCINIT01";
  static void Put(std::span<uint8_t>bytes,size_t offset,uint64_t value,size_t count=8){
    for(size_t i=0;i<count;++i)bytes[offset+i]=static_cast<uint8_t>(value>>(i*8));
  }
  static uint64_t Get(std::span<const uint8_t>bytes,size_t offset,size_t count=8){
    uint64_t value=0;for(size_t i=0;i<count;++i)value|=static_cast<uint64_t>(bytes[offset+i])<<(i*8);return value;
  }
};

// UI-thread physical receipt accounting. Abort/timeout is a logical event only.
// Installation ACK settles the public registration but keeps the shared region
// charged. Only an actual receiver region-release receipt (and installation
// ACK), or real renderer-epoch death, retires those bytes. A watchdog event
// requests teardown; it never fabricates either physical receipt.
class InitSnapshotReceipts {
 public:
  static constexpr size_t kTransfers=64,kBytes=64*1048576;
  static constexpr double kRetirementGrace=30;
  enum class Status {Accepted,Rejected,Canceled,Expired,CloseRequired,RendererGone};
  struct Event {InitSnapshotPin pin;Status status;bool public_reply=false,region_installed=false;};
  std::optional<InitSnapshotPin>Reserve(InitSnapshotOwner owner,uint64_t version,size_t payload,double deadline,double now){
    InitSnapshotPin pin{std::move(owner),version,next_+1};const auto bytes=InitSnapshotWire::Size(pin,payload);
    if(!std::isfinite(now)||now<0||!std::isfinite(deadline)||deadline<0||(deadline&&now>=deadline)||
       next_==std::numeric_limits<uint64_t>::max()||!bytes||receipts_.size()>=kTransfers||bytes>kBytes-bytes_)return std::nullopt;
    Receipt receipt;receipt.pin=pin;receipt.bytes=bytes;receipt.deadline=deadline;
    ++next_;bytes_+=bytes;receipts_.emplace(pin.transfer,std::move(receipt));return pin;
  }
  bool Submitted(uint64_t transfer,double now){auto found=receipts_.find(transfer);
    if(found==receipts_.end()||found->second.submitted||found->second.logical_done||!ValidNow(now)||
       (found->second.deadline&&now>=found->second.deadline))return false;
    found->second.submitted=true;return true;
  }
  // A local builder/admission refusal before SendProcessMessage was attempted.
  bool Refused(uint64_t transfer){auto found=receipts_.find(transfer);
    if(found==receipts_.end()||found->second.submitted)return false;
    Erase(found);return true;
  }
  std::optional<Event>Cancel(uint64_t transfer,double now){
    auto found=receipts_.find(transfer);if(found==receipts_.end()||found->second.logical_done||!ValidNow(now))return std::nullopt;
    auto&receipt=found->second;receipt.logical_done=true;receipt.retirement=now+kRetirementGrace;receipt.terminal=Status::Canceled;
    return Event{receipt.pin,Status::Canceled,true};
  }
  std::optional<Event>Acknowledge(const InitSnapshotPin&actual,bool installed,double now){
    auto found=receipts_.find(actual.transfer);
    if(found==receipts_.end()||!found->second.submitted||found->second.pin!=actual||!ValidNow(now))return std::nullopt;
    auto&receipt=found->second;
    if(receipt.acknowledged)return std::nullopt;
    const bool expired=receipt.deadline&&now>=receipt.deadline;
    const auto status=receipt.logical_done?receipt.terminal:expired?Status::Expired:installed?Status::Accepted:Status::Rejected;
    Event event{receipt.pin,status,!receipt.logical_done,installed};
    receipt.acknowledged=true;receipt.installed=installed;
    if(!installed||receipt.release_seen){Erase(found);return event;}
    if(!receipt.logical_done){
      receipt.logical_done=true;receipt.terminal=status;
      if(status==Status::Expired)receipt.retirement=receipt.deadline+kRetirementGrace;
    }
    return event;
  }
  // These are physical receipts from the exact receiver after its last region
  // lease dies. They are independent of registration cancellation or supersede.
  bool Released(const InitSnapshotPin&actual){
    auto found=receipts_.find(actual.transfer);
    if(found==receipts_.end()||!found->second.submitted||found->second.pin!=actual||found->second.release_seen)return false;
    found->second.release_seen=true;
    if(found->second.acknowledged)Erase(found);
    return true;
  }
  // For an already accepted current region, ask the receiver to discard it.
  // No entry is removed until Released or actual renderer death follows.
  bool Retire(uint64_t transfer,double now){
    auto found=receipts_.find(transfer);
    if(found==receipts_.end()||!ValidNow(now)||!found->second.acknowledged||!found->second.installed)return false;
    if(!found->second.retirement)found->second.retirement=now+kRetirementGrace;
    return true;
  }
  // Invoke only after an actual positive install ACK establishes this owner's
  // newer version. A superseded mapping stays charged until its release ACK;
  // the watchdog only requests real teardown if that receipt never arrives.
  size_t RetireSuperseded(const InitSnapshotOwner&owner,uint64_t installed_version,double now){
    if(!installed_version||!ValidNow(now))return 0;
    size_t marked=0;
    for(auto&[id,receipt]:receipts_){
      if(receipt.pin.owner!=owner||receipt.pin.version>=installed_version||
         !receipt.acknowledged||!receipt.installed||receipt.retirement)continue;
      receipt.retirement=now+kRetirementGrace;++marked;
    }
    return marked;
  }
  std::vector<Event>Tick(double now){
    std::vector<Event>events;if(!ValidNow(now))return events;
    for(auto&[id,receipt]:receipts_){
      if(!receipt.logical_done&&receipt.deadline&&now>=receipt.deadline){
        receipt.logical_done=true;receipt.retirement=receipt.deadline+kRetirementGrace;receipt.terminal=Status::Expired;
        events.push_back({receipt.pin,Status::Expired,true});
      }
      if(receipt.retirement&&!receipt.close_requested&&now>=receipt.retirement){
        receipt.close_requested=true;events.push_back({receipt.pin,Status::CloseRequired,false});
      }
    }
    return events;
  }
  std::vector<Event>RendererGone(uint64_t browser,uint64_t epoch){
    std::vector<Event>events;
    for(auto it=receipts_.begin();it!=receipts_.end();){
      if(it->second.pin.owner.browser!=browser||it->second.pin.owner.renderer_epoch!=epoch){++it;continue;}
      const auto receipt=it->second;bytes_-=receipt.bytes;it=receipts_.erase(it);
      events.push_back({receipt.pin,Status::RendererGone,!receipt.logical_done});
    }
    return events;
  }
  bool NeedsTick()const{for(const auto&[id,r]:receipts_)if(!r.close_requested&&(!r.logical_done?r.deadline!=0:r.retirement!=0))return true;return false;}
  // Actual installed current regions remain physically owned while a worker
  // page is retained. This observes existing receipt state; it retires nothing.
  struct AcceptedWorkSnapshot {size_t stable_mappings=0,pending_receipts=0;};
  AcceptedWorkSnapshot AcceptedWork()const noexcept{
    AcceptedWorkSnapshot result;
    for(const auto&[id,receipt]:receipts_){
      (void)id;
      if(receipt.submitted&&receipt.acknowledged&&receipt.installed&&
         receipt.logical_done&&receipt.terminal==Status::Accepted&&
         !receipt.retirement&&!receipt.close_requested&&!receipt.release_seen)
        ++result.stable_mappings;
      else ++result.pending_receipts;
    }
    return result;
  }
  size_t Physical()const{return receipts_.size();}
  size_t Bytes()const{return bytes_;}
 private:
  struct Receipt {InitSnapshotPin pin;size_t bytes=0;double deadline=0,retirement=0;
    bool submitted=false,logical_done=false,close_requested=false,acknowledged=false,installed=false,release_seen=false;
    Status terminal=Status::Canceled;};
  using Iterator=std::map<uint64_t,Receipt>::iterator;
  void Erase(Iterator found){bytes_-=found->second.bytes;receipts_.erase(found);}
  static bool ValidNow(double now){return std::isfinite(now)&&now>=0;}
  std::map<uint64_t,Receipt>receipts_;size_t bytes_=0;uint64_t next_=0;
};
} // namespace suitecut
