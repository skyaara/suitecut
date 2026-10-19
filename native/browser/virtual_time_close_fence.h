// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace suitecut {
// A real renderer death invalidates its clock immediately. Physical page close
// waits for the exact crash observers that already owned that death receipt.
// This controller neither acknowledges crash events nor releases Inspector work.
class NativeVirtualTimeCloseFence {
 public:
  static constexpr size_t kCapacity=64;
  static constexpr double kUnboundedPhysicalGraceMs=5000;
  struct Owner {int id;uint64_t generation;double original_deadline;};
  bool RendererTerminated(const std::vector<Owner>& owners,double now){
    if(closed_||dispatched_)return false;
    if(captured_)return true; // Never restart grace or admit later observers.
    if(!std::isfinite(now)||!std::isfinite(now+kUnboundedPhysicalGraceMs)||owners.size()>kCapacity)return false;
    std::map<int,Owner> captured;
    for(const auto& owner:owners){
      if(owner.id<1||owner.generation==0||!std::isfinite(owner.original_deadline)||owner.original_deadline<0||!captured.emplace(owner.id,owner).second)return false;
    }
    owners_=std::move(captured);captured_=true;unbounded_close_at_=now+kUnboundedPhysicalGraceMs;return true;
  }
  void Request(){if(!closed_)requested_=true;}
  void Finished(int id,uint64_t generation){
    const auto found=owners_.find(id);
    if(found!=owners_.end()&&found->second.generation==generation)owners_.erase(found);
  }
  bool Post(double now){
    if(!Ready(now)||posted_)return false;
    posted_=true;return true;
  }
  bool EnterPosted(double now){
    if(!posted_)return false;
    posted_=false;
    // A renderer-death receipt can arrive after posting but before this UI task.
    if(!Ready(now))return false;
    dispatched_=true;return true;
  }
  void PostRejected(){posted_=false;}
  void Closed(){closed_=true;requested_=false;posted_=false;owners_.clear();}
  bool NeedsTick()const{return requested_&&!closed_&&!dispatched_;}
  size_t HeldOwners()const{return owners_.size();}
 private:
  bool Ready(double now)const{
    if(!requested_||closed_||dispatched_||!std::isfinite(now))return false;
    for(const auto& [id,owner]:owners_){
      (void)id;
      // Finite logical deadlines finish only through Source's real operation
      // completion path. Reaching that timestamp alone is never an event ACK.
      if(owner.original_deadline!=0||now<unbounded_close_at_)return false;
    }
    return true;
  }
  std::map<int,Owner> owners_;
  double unbounded_close_at_=0;
  bool captured_=false,requested_=false,posted_=false,dispatched_=false,closed_=false;
};
}
