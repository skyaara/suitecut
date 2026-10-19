// SPDX-License-Identifier: MIT
#pragma once
#include "init_snapshot_transport.h"
#include <atomic>
#include <functional>
#include <memory>

namespace suitecut {
// Single native UI publisher; old byte bodies may die on the IO thread. Version
// order is assigned here at accepted commits, never inferred from payload fields.
class InitSnapshotPublications {
 private:
  struct State {std::atomic<size_t>bytes{0},bodies{0};};
  struct Body {
    std::shared_ptr<State>state;std::string text;bool charged=false;
    ~Body(){if(charged){state->bytes.fetch_sub(text.size());state->bodies.fetch_sub(1);}}
  };
 public:
  static constexpr size_t kContexts=32,kBodies=64,kBytes=64*1048576;
  struct Snapshot {uint64_t version=0;std::shared_ptr<const std::string>bytes;};
  using Accept=std::function<bool(std::shared_ptr<const std::string>)>;
  bool Commit(int context,std::string text,const Accept&accept){
    if(context<=0||text.empty()||text.size()>InitSnapshotWire::kPayload||!accept)return false;
    auto found=current_.find(context);
    if(found!=current_.end()&&found->second.bytes&&*found->second.bytes==text)return true;
    if((found==current_.end()&&current_.size()>=kContexts)||
       (found!=current_.end()&&found->second.version==std::numeric_limits<uint64_t>::max())||
       state_->bodies.load()>=kBodies||text.size()>kBytes-state_->bytes.load())return false;
    // Prepare every allocation before making the real native store acceptance
    // observable. The alias owns Body, so old native IO leases remain charged.
    auto body=std::make_shared<Body>();body->state=state_;body->text=std::move(text);
    state_->bytes.fetch_add(body->text.size());state_->bodies.fetch_add(1);body->charged=true;
    std::shared_ptr<const std::string>bytes(body,&body->text);
    const bool inserted=found==current_.end();if(inserted)found=current_.emplace(context,Snapshot{}).first;
    try{
      if(!accept(bytes)){if(inserted)current_.erase(found);return false;}
    }catch(...){
      if(inserted)current_.erase(found);
      throw; // Preserve the original native acceptance failure.
    }
    found->second=Snapshot{found->second.version+1,std::move(bytes)};return true;
  }
  Snapshot Current(int context)const{const auto found=current_.find(context);return found==current_.end()?Snapshot{}:found->second;}
  void Drop(int context){current_.erase(context);}
  size_t Contexts()const{return current_.size();}
  size_t Bytes()const{return state_->bytes.load();}
  size_t Bodies()const{return state_->bodies.load();}
 private:
  std::shared_ptr<State>state_=std::make_shared<State>();std::map<int,Snapshot>current_;
};
} // namespace suitecut
