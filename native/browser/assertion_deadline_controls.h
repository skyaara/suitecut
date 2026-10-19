// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace suitecut {
// A native assertion clock decision is not an observed mismatch. Publication
// remains held until the actual Source query/group owner is physically ready.
class AssertionDeadlineControls {
 public:
  static constexpr size_t kCapacity=64;
  enum class Verdict { Hold, Deadline, HardFailure };
  struct Entry {int owner=0;uint64_t generation=0;double deadline=0,cleanup_deadline=0;std::string matcher,error;bool frozen=false,validated=false,watchdog=false,admitted=false;};
  static bool Supported(std::string_view name){
    for(const auto actual:{"toHaveText","toContainText","toHaveValue","toHaveCount","toHaveId","toHaveAttribute","toHaveClass","toContainClass","toHaveCSS","toBeVisible","toBeHidden","toBeAttached","toBeEnabled","toBeDisabled","toBeEditable","toBeFocused","toBeEmpty","toBeInViewport","toHaveTitle","toHaveURL"})if(name==actual)return true;
    return false;
  }
  bool Admit(int owner,uint64_t generation,double deadline,std::string matcher){
    if(owner<=0||!generation||!std::isfinite(deadline)||deadline<=0||!Supported(matcher)||entries_.size()>=kCapacity||entries_.contains(owner))return false;
    entries_.emplace(owner,Entry{owner,generation,deadline,0,std::move(matcher),{},false,false,false,false});return true;
  }
  bool Contains(int owner)const{return entries_.contains(owner);}
  const Entry* Get(int owner)const{const auto found=entries_.find(owner);return found==entries_.end()?nullptr:&found->second;}
  const Entry* Get(int owner,uint64_t generation)const{const auto found=entries_.find(owner);return found!=entries_.end()&&found->second.generation==generation?&found->second:nullptr;}
  bool Freeze(int owner,uint64_t generation,double now){
    auto* entry=Find(owner,generation);if(!entry||!std::isfinite(now)||now<entry->deadline)return false;
    if(!entry->frozen){entry->frozen=true;entry->cleanup_deadline=now+1000;}return true;
  }
  bool Fail(int owner,uint64_t generation,const std::string& error,double now){
    auto* entry=Find(owner,generation);if(!entry||!std::isfinite(now)||error.empty())return false;
    if(entry->error.empty())entry->error=error.substr(0,4096);
    if(!entry->frozen){entry->frozen=true;entry->cleanup_deadline=now+1000;}return true;
  }
  bool Validated(int owner,uint64_t generation){auto* entry=Find(owner,generation);if(!entry)return false;entry->validated=true;return true;}
  bool Admitted(int owner,uint64_t generation){auto* entry=Find(owner,generation);if(!entry||entry->frozen)return false;entry->admitted=true;return true;}
  Verdict Ready(int owner,uint64_t generation,bool physical_ready)const{
    const auto* entry=Get(owner,generation);if(!entry||!entry->frozen||!physical_ready)return Verdict::Hold;
    return entry->error.empty()&&entry->validated&&!entry->admitted?Verdict::Deadline:Verdict::HardFailure;
  }
  bool Watchdog(int owner,uint64_t generation,double now){auto* entry=Find(owner,generation);if(!entry||!entry->frozen||entry->watchdog||!std::isfinite(now)||now<entry->cleanup_deadline)return false;entry->watchdog=true;return true;}
  bool Cancel(int owner,uint64_t generation){const auto* entry=Get(owner,generation);return entry&&entries_.erase(owner);}
  std::vector<int> Owners()const{std::vector<int> result;for(const auto&[id,entry]:entries_){(void)entry;result.push_back(id);}return result;}
  bool NeedsTick()const{return !entries_.empty();}
  size_t Size()const{return entries_.size();}
 private:
  Entry* Find(int owner,uint64_t generation){const auto found=entries_.find(owner);return found!=entries_.end()&&found->second.generation==generation?&found->second:nullptr;}
  std::map<int,Entry> entries_;
};
}
