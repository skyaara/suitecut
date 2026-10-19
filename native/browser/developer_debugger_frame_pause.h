// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <optional>
#include <string>
#include "debugger_domains.h"
#include "developer_debugger_frame_scopes.h"
namespace suitecut {
class FrameDebuggerPause {
 public:
  struct Script {int context=0;bool attested_native=false;};
  using Lookup=std::function<std::optional<Script>(const std::string&,const std::string&)>;
  using Disposition=DebuggerDomains::DeveloperPauseDisposition;
  // Invoke only from the captured channel's scoped arbiter callback. The
  // actual native arbiter/host, not this packet dictionary, routes the session.
  static Disposition Classify(const std::shared_ptr<FrameDebuggerScopes>&scopes,FrameDebuggerScopes::Token token,DebuggerDomains::Dict packet,const Lookup&lookup){
    FrameDebuggerScopes::Pin pin;
    if(!scopes||!lookup||!scopes->ObservedCurrent(token)||!scopes->Pins(token,pin)||!packet||packet->GetType("callFrames")!=VTYPE_LIST)return Disposition::Unsupported;
    const auto frames=packet->GetList("callFrames");if(!frames->GetSize()||frames->GetSize()>64)return Disposition::Unsupported;
    for(size_t i=0;i<frames->GetSize();++i){
      if(frames->GetType(i)!=VTYPE_DICTIONARY)return Disposition::Unsupported;
      const auto frame=frames->GetDictionary(i);
      if(frame->GetType("callFrameId")!=VTYPE_STRING||frame->GetString("callFrameId").empty()||frame->GetString("callFrameId").length()>512||frame->GetType("location")!=VTYPE_DICTIONARY)return Disposition::Unsupported;
      const auto location=frame->GetDictionary("location");
      if(location->GetType("scriptId")!=VTYPE_STRING||location->GetString("scriptId").empty()||location->GetString("scriptId").length()>256)return Disposition::Unsupported;
      const auto script=lookup(pin.channel.session,location->GetString("scriptId").ToString());
      if(!script||script->context<=0)return Disposition::Unsupported;
      if(script->context!=pin.context)return i==0?Disposition::Foreign:Disposition::Unsupported;
      // Factory-owned hits are routed before this classifier. A remaining
      // attested native frame cannot become a scoped author mutation target.
      if(script->attested_native)return Disposition::Unsupported;
    }
    return scopes->ObservedCurrent(token)?Disposition::Owned:Disposition::Unsupported;
  }
};
}
