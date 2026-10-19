// SPDX-License-Identifier: MIT
#pragma once
#include "developer_debugger_worker_documents.h"
#include "debugger_domains.h"
#include <functional>
#include <optional>
namespace suitecut {
class DeveloperDebuggerWorkerPause {
 public:
  struct Script{int context=0;bool attested_native=false;};
  using Pin=DeveloperDebuggerWorkerDocuments::Pin;
  using Disposition=DebuggerDomains::DeveloperPauseDisposition;
  static Disposition Classify(const Pin&pin,DebuggerDomains::Dict packet,
      const std::function<bool(const Pin&)>&current,
      const std::function<std::optional<Script>(const std::string&,const std::string&)>&lookup){
    if(!current||!lookup||!current(pin)||!packet||packet->GetType("callFrames")!=VTYPE_LIST)return Disposition::Unsupported;
    const auto frames=packet->GetList("callFrames");if(!frames->GetSize()||frames->GetSize()>64)return Disposition::Unsupported;
    for(size_t i=0;i<frames->GetSize();++i){
      if(frames->GetType(i)!=VTYPE_DICTIONARY)return Disposition::Unsupported;
      const auto frame=frames->GetDictionary(i);
      if(frame->GetType("callFrameId")!=VTYPE_STRING||frame->GetString("callFrameId").empty()||frame->GetString("callFrameId").length()>512||frame->GetType("location")!=VTYPE_DICTIONARY)return Disposition::Unsupported;
      const auto location=frame->GetDictionary("location");
      if(location->GetType("scriptId")!=VTYPE_STRING||location->GetString("scriptId").empty()||location->GetString("scriptId").length()>256)return Disposition::Unsupported;
      const auto script=lookup(pin.attachment.session,location->GetString("scriptId").ToString());
      if(!script||script->context<=0)return Disposition::Unsupported;
      if(script->context!=pin.context)return i==0?Disposition::Foreign:Disposition::Unsupported;
      if(script->attested_native)return Disposition::Unsupported;
    }
    return current(pin)?Disposition::Owned:Disposition::Unsupported;
  }
};
}
