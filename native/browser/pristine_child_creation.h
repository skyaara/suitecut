// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
namespace suitecut {
// A genuine child wait receipt survives releasing that wait only as a one-shot
// first-document creation token. It never asserts the renderer is still held.
class PristineChildCreation {
 public:
  PristineChildCreation()=default;
  PristineChildCreation(bool actual_waiting,std::string frame,double original_deadline)
      :frame_(std::move(frame)),deadline_(original_deadline),valid_(actual_waiting&&!frame_.empty()&&frame_.size()<=128&&std::isfinite(deadline_)&&deadline_>0),held_(valid_){}
  bool BindLease(uint64_t actual_lease){
    if(!valid_||consumed_||!actual_lease||(lease_&&lease_!=actual_lease))return false;
    lease_=actual_lease;return true;
  }
  void Released(){held_=false;}
  bool Select(const std::string&frame,const std::string&identity,uint64_t armed_lease,bool actually_armed,bool fresh,double now){
    if(!Current(frame,identity,armed_lease,now)||!actually_armed)return false;
    if(!identity_.empty())return identity_==identity;
    if(!fresh)return false;
    identity_=identity;return true;
  }
  bool Consume(const std::string&frame,const std::string&identity,uint64_t armed_lease,double now){
    if(!Current(frame,identity,armed_lease,now)||identity_.empty()||identity_!=identity)return false;
    consumed_=true;return true;
  }
  bool Held()const{return held_;}
 private:
  bool Current(const std::string&frame,const std::string&identity,uint64_t lease,double now)const{
    return valid_&&!consumed_&&lease_&&lease_==lease&&frame_==frame&&!identity.empty()&&identity.size()<=256&&std::isfinite(now)&&now<deadline_;
  }
  std::string frame_,identity_;
  double deadline_=0;uint64_t lease_=0;
  bool valid_=false,held_=false,consumed_=false;
};
} // namespace suitecut
