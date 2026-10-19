// SPDX-License-Identifier: MIT
#pragma once
#include "init_snapshot_transport.h"
#include <array>
#include <memory>
#include <tuple>

namespace suitecut {
// Renderer-thread only. Admission comes from actual native browser/frame/epoch
// ownership, never from the shared packet. Readers must die on this same thread.
// This registry does not admit a renderer, release author execution, or send an
// ACK by itself. It reports facts for the native process-message adapter.
class InitSnapshotReceiver {
 private:
  struct State {
    struct Slot {InitSnapshotPin pin;size_t bytes=0;bool occupied=false,released=false;};
    std::array<Slot,InitSnapshotReceipts::kTransfers>slots{};
    size_t bytes=0,count=0;
  };
 public:
  // The caller owns the actual mapped memory represented by bytes through hold.
  struct Region {std::shared_ptr<const void>hold;std::span<const uint8_t>bytes;};
  class Lease {
   public:
    Lease(const Lease&)=delete;Lease&operator=(const Lease&)=delete;
    const InitSnapshotPin&Pin()const{return pin_;}
    std::string_view Payload()const{return payload_;}
    ~Lease(){
      // Actual last reader drops the mapping before making release observable.
      region_.hold.reset();
      state_->slots[slot_].released=true;
    }
   private:
    friend class InitSnapshotReceiver;
    Lease(std::shared_ptr<State>state,size_t slot,Region region,InitSnapshotWire::View view):
      state_(std::move(state)),slot_(slot),region_(std::move(region)),pin_(std::move(view.pin)),payload_(view.payload){}
    std::shared_ptr<State>state_;size_t slot_;Region region_;InitSnapshotPin pin_;std::string_view payload_;
  };
  enum class Status {Installed,Duplicate,NotAdmitted,Malformed,Stale,Conflict,Capacity,Expired};
  struct Result {Status status;std::shared_ptr<const Lease>lease;};
  bool Admit(const InitSnapshotOwner&owner){
    if(!InitSnapshotWire::ValidOwner(owner))return false;
    auto key=Key(owner);if(owners_.contains(key))return true;
    if(owners_.size()>=InitSnapshotReceipts::kTransfers)return false;
    owners_.emplace(std::move(key),Record{});return true;
  }
  Result Install(Region region,const InitSnapshotPin&expected,double deadline,double now){
    if(!std::isfinite(now)||now<0||!std::isfinite(deadline)||deadline<0||(deadline&&now>=deadline))return {Status::Expired,{}};
    auto found=owners_.find(Key(expected.owner));if(found==owners_.end())return {Status::NotAdmitted,{}};
    if(!region.hold)return {Status::Malformed,{}};
    auto view=InitSnapshotWire::Decode(region.bytes);
    if(!view||view->pin!=expected)return {Status::Malformed,{}};
    auto&record=found->second;
    if(expected.version<record.version)return {Status::Stale,{}};
    if(expected.version==record.version){
      if(record.current&&record.current->Pin()==expected&&record.current->Payload()==view->payload)return {Status::Duplicate,record.current};
      return {Status::Conflict,{}};
    }
    if(expected.transfer<=record.transfer)return {Status::Conflict,{}};
    if(state_->count>=state_->slots.size()||region.bytes.size()>InitSnapshotReceipts::kBytes-state_->bytes)return {Status::Capacity,{}};
    size_t slot=0;while(state_->slots[slot].occupied)++slot;
    State::Slot pending{expected,region.bytes.size(),true,false};
    auto lease=std::shared_ptr<const Lease>(new Lease(state_,slot,std::move(region),std::move(*view)));
    // No callbacks run during commit; allocation succeeded before mutation.
    auto&receipt=state_->slots[slot];receipt=std::move(pending);
    state_->bytes+=receipt.bytes;++state_->count;
    record.version=expected.version;record.transfer=expected.transfer;record.current=lease;return {Status::Installed,std::move(lease)};
  }
  std::shared_ptr<const Lease>Read(const InitSnapshotPin&expected)const{
    auto found=owners_.find(Key(expected.owner));
    return found!=owners_.end()&&found->second.current&&found->second.current->Pin()==expected?found->second.current:nullptr;
  }
  // Logical retirement stops new readers. Existing readers keep the mapping.
  bool Retire(const InitSnapshotOwner&owner){return owners_.erase(Key(owner))!=0;}
  void Closed(){owners_.clear();}
  // These facts are generated only by actual last-reader destruction. Draining
  // them does not assert successful IPC delivery; the adapter must retain each
  // outgoing release ACK through its own actual send/closure boundary.
  std::vector<InitSnapshotPin>TakeReleased(){
    std::vector<InitSnapshotPin>pins;pins.reserve(state_->count);
    for(auto&slot:state_->slots)if(slot.occupied&&slot.released){
      pins.push_back(slot.pin);state_->bytes-=slot.bytes;--state_->count;slot=State::Slot{};
    }
    return pins;
  }
  size_t Physical()const{return state_->count;}
  size_t Bytes()const{return state_->bytes;}
  size_t Owners()const{return owners_.size();}
 private:
  using OwnerKey=std::tuple<uint64_t,uint64_t,uint64_t,std::string>;
  static OwnerKey Key(const InitSnapshotOwner&owner){return {owner.context,owner.browser,owner.renderer_epoch,owner.frame};}
  struct Record {uint64_t version=0,transfer=0;std::shared_ptr<const Lease>current;};
  std::shared_ptr<State>state_=std::make_shared<State>();std::map<OwnerKey,Record>owners_;
};
} // namespace suitecut
