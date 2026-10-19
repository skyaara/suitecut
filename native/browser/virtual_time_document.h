// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace suitecut {
// Pin the actual default realm, not just its reusable Inspector integer ID.
// This guards ownership; it does not create context-local native time.
class NativeVirtualTimeDocument {
 public:
  struct Identity {
    std::string frame, session, unique_context;
    int context=0;
    uint64_t renderer_epoch=0;
    bool operator==(const Identity&)const=default;
  };
  bool Pin(Identity identity){
    if(identity_||identity.frame.empty()||identity.frame.size()>128||
       !identity.session.empty()||identity.unique_context.empty()||
       identity.unique_context.size()>256||identity.context<=0||!identity.renderer_epoch)return false;
    identity_=std::move(identity);return true;
  }
  bool Current(const Identity& identity)const{return identity_&&*identity_==identity;}
  bool Destroyed(std::string_view session,int context,std::string_view unique)const{
    if(!identity_||identity_->session!=session)return false;
    // Newer protocol receipts can identify destruction by unique ID alone.
    // If both identities are supplied, both must match. A late numeric-ID reuse
    // must not destroy a replacement realm with a different unique identity.
    if(!unique.empty())return unique==identity_->unique_context&&(context<=0||context==identity_->context);
    return context>0&&context==identity_->context;
  }
  bool Frame(std::string_view session,std::string_view frame)const{
    return identity_&&identity_->session==session&&identity_->frame==frame;
  }
  void Reset(){identity_.reset();}
 private:
  std::optional<Identity> identity_;
};
}
