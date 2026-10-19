// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <memory>
namespace suitecut {
// UI-thread serialized, including the process-wide physical counter. A lease
// belongs to a real submitted callback, never to a logical operation lifetime.
class SnapshotReferenceDispatch {
 public:
  static constexpr size_t kLocal=128,kGlobal=256;
  std::shared_ptr<void> Begin(){
    if(state_->pending>=kLocal||Global()>=kGlobal)return{};
    ++state_->pending;++Global();const auto state=state_;
    return std::shared_ptr<void>(new unsigned char(0),[state](void* pointer){delete static_cast<unsigned char*>(pointer);--state->pending;--Global();});
  }
  size_t Pending()const{return state_->pending;}
  static size_t ProcessPending(){return Global();}
 private:
  struct State{size_t pending=0;};
  static size_t& Global(){static size_t value=0;return value;}
  std::shared_ptr<State>state_=std::make_shared<State>();
};
}
