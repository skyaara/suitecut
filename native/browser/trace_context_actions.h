// SPDX-License-Identifier: MIT
#pragma once
#include "include/cef_values.h"
namespace suitecut {
// The Engine calls this only for an actual public closePage request after
// resolving its real page/context registry entry. Internal cleanup skips it.
inline CefRefPtr<CefDictionaryValue> TracePageCloseMetadata(CefRefPtr<CefDictionaryValue> params,int page,double now){
  auto event=CefDictionaryValue::Create();event->SetString("type","before");event->SetString("class","Page");event->SetString("method","close");event->SetString("apiName","Close page");event->SetString("pageId","page@"+std::to_string(page));event->SetDouble("startTime",now);
  auto actual=CefDictionaryValue::Create();if(params->GetType("reason")==VTYPE_STRING)actual->SetString("reason",params->GetString("reason"));event->SetDictionary("params",actual);
  if(params->GetType("traceStack")==VTYPE_LIST){event->SetList("stack",params->GetList("traceStack")->Copy());}return event;
}
} // namespace suitecut
