// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
namespace suitecut::runner {
struct AttemptErrorOwner {
  uint64_t test=0,generation=0;unsigned worker=0;int64_t pid=0;
  bool operator==(const AttemptErrorOwner&)const=default;
};
struct PhaseErrorScope {
  AttemptErrorOwner attempt;
  uint64_t scope=0,generation=0;
};
struct AttemptSecondaryError {
  PhaseErrorScope source;
  std::string error_json;
  uint64_t originating_step=0;
  bool fixture_raw=false;
};
inline bool ValidAttemptErrorOwner(const AttemptErrorOwner& owner){return owner.test&&owner.generation&&owner.worker&&owner.pid>0&&owner.pid<=INT32_MAX;}
// One genuine native phase group per actor. Association is captured BEFORE
// actual phase admission, not reconstructed from error text or a later test.
class PhaseErrorAttribution {
 public:
  void Begin(uint64_t scope,uint64_t generation,std::optional<AttemptErrorOwner> attempt){
    if(pending_scope_||!scope||!generation||(attempt&&!ValidAttemptErrorOwner(*attempt)))throw std::invalid_argument("Native phase error owner admission rejected");
    pending_scope_=scope;pending_generation_=generation;attempt_=attempt;
  }
  std::optional<PhaseErrorScope> Current(uint64_t scope,uint64_t generation,std::optional<AttemptErrorOwner> current)const{
    if(!pending_scope_||scope!=pending_scope_||generation!=pending_generation_||(attempt_&&(!current||*current!=*attempt_)))throw std::invalid_argument("Native phase error result has stale or foreign actual owner");
    return attempt_?std::optional<PhaseErrorScope>(PhaseErrorScope{*attempt_,scope,generation}):std::nullopt;
  }
  std::optional<PhaseErrorScope> Consume(uint64_t scope,uint64_t generation,std::optional<AttemptErrorOwner> current){
    if(!pending_scope_||scope!=pending_scope_||generation!=pending_generation_||
        (attempt_&&(!current||*current!=*attempt_)))throw std::invalid_argument("Native phase error result has stale or foreign actual owner");
    const auto result=attempt_?std::optional<PhaseErrorScope>(PhaseErrorScope{*attempt_,scope,generation}):std::nullopt;
    pending_scope_=pending_generation_=0;attempt_.reset();return result;
  }
  bool Pending(uint64_t scope,uint64_t generation)const{return pending_scope_==scope&&pending_generation_==generation&&scope;}
  void CancelBeforeAdmission(){pending_scope_=pending_generation_=0;attempt_.reset();}
 private:
  uint64_t pending_scope_=0,pending_generation_=0;
  std::optional<AttemptErrorOwner> attempt_;
};
} // namespace suitecut::runner
