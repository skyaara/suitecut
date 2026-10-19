// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace suitecut {
// Cross-page read-only CDP calls cannot borrow the remote page's public request
// IDs. Their process cap survives cancellation/closure of the originating page;
// only the actual remote ACK, submission failure or remote channel death retires
// a call. A stuck remote renderer is closed by its own physical watchdog.
class OwnerFrameControls {
 public:
  using Clock=std::function<double()>;using Fail=std::function<void(const std::string&)>;
  struct Physical {bool settled=false;std::function<void()>release;void Settle(){if(!settled){settled=true;release();}}~Physical(){Settle();}};
  OwnerFrameControls(Clock now,Fail fail):now_(std::move(now)),fail_(std::move(fail)){}
  ~OwnerFrameControls(){Close();}
  std::shared_ptr<Physical> Begin(){
    if(life_->closed||life_->failed||process_physical_>=64||life_->entries.size()>=64||life_->sequence==std::numeric_limits<uint64_t>::max())return nullptr;
    const auto token=++life_->sequence;life_->entries.emplace(token,now_()+30000);++process_physical_;
    const auto life=life_;auto result=std::make_shared<Physical>();result->release=[life,token]{if(life->entries.erase(token))--process_physical_;};return result;
  }
  void Failed(const std::string&error){if(life_->failed||life_->closed)return;life_->failed=true;fail_(error);}
  void Tick(){if(life_->failed||life_->closed)return;for(const auto&[token,deadline]:life_->entries)if(now_()>=deadline){Failed("Native frame owner peer physical settlement deadline exceeded");return;}}
  bool Healthy()const{return !life_->closed&&!life_->failed;}
  bool NeedsTick()const{return !life_->closed&&!life_->failed&&!life_->entries.empty();}
  void Close(){if(life_->closed)return;life_->closed=true;process_physical_-=life_->entries.size();life_->entries.clear();}
  size_t PhysicalSize()const{return life_->entries.size();}
  static size_t ProcessPhysicalSize(){return process_physical_;}
 private:
  struct Life{bool closed=false,failed=false;uint64_t sequence=0;std::map<uint64_t,double>entries;};
  inline static size_t process_physical_=0;
  Clock now_;Fail fail_;std::shared_ptr<Life>life_=std::make_shared<Life>();
};
} // namespace suitecut
