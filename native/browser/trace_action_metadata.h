// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include "include/cef_values.h"
#include "trace_action_titles.h"
namespace suitecut {
// Read/observation APIs record before/after metadata without recursive action captures.
inline bool TraceActionHasPhases(const std::string& command){return command!="queryEvaluate"&&command!="snapshotReferences"&&command!="dialog"&&command!="network"&&command!="waitEvent";}
// Existing observation methods retain their historical after-DOM behavior. The
// reference snapshot is itself a semantic capture and must not recurse into DOM.
inline bool TraceActionCapturesAfterDOM(const std::string& method){return method!="ariaSnapshot";}
inline CefRefPtr<CefDictionaryValue> TraceActionMetadata(CefRefPtr<CefDictionaryValue> request,int page,double now){
  const auto command=request->GetString("command").ToString(),action=request->GetString("action").ToString();std::string method,kind="Frame";
  if(command=="evaluate")method=request->GetString("traceMethod")=="setContent"?"setContent":"evaluate";
  else if(command=="navigateFrame")method="goto";
  else if(command=="reload")method="reload";
  else if(command=="historyBack")method="goBack";
  else if(command=="historyForward")method="goForward";
  else if(command=="waitTimeout"){kind="Page";method="waitForTimeout";}
  else if(command=="waitEvent"){kind="Page";method="waitForEvent";}
  else if(command=="locator")method=action=="click"&&request->GetInt("clickCount")==2?"dblclick":action;
  else if(command=="form"&&(action=="setInputFiles"||action=="setChecked"||action=="selectOption"||action=="dragTo"))method=action;
  else if(command=="artifact"&&action=="captureCommit"&&(request->GetString("kind")=="screenshot"||request->GetString("kind")=="pdf")){kind="Page";method=request->GetString("kind").ToString();}
  else if(command=="artifact"&&(action=="screenshot"||action=="pdf")){kind="Page";method=action;}
  else if(command=="input"){const auto input=request->GetString("kind").ToString();kind=input=="mouse"?"Mouse":input=="keyboard"?"Keyboard":"Touchscreen";method=action;if(input=="mouse"&&action=="click"&&request->GetInt("clickCount")==2)method="dblclick";}
  else if(command=="handle"&&action=="frameElement")method="frameElement";
  else if(command=="handle"&&(action=="create"||action=="evaluate")){kind=action=="create"?"Frame":"JSHandle";method=action=="create"?"evaluateHandle":"evaluate";}
  else if(command=="snapshotReferences")method="ariaSnapshot";
  else if(command=="highlight"&&action!="claim")method=action=="show"?"highlight":"hideHighlight";
  else if(command=="queryEvaluate"&&request->GetBool("expectation"))method="expect";
  else if(command=="dialog"&&action=="answer"){kind="Dialog";method=request->GetBool("accept")?"accept":"dismiss";}
  else if(command=="network"&&!request->GetBool("internal")){
    if(action=="registerRoute"){kind="Page";method="route";}
    else if(action=="continue"||action=="fulfill"||action=="abort"||action=="fallback"){kind="Route";method=action;}
    else return nullptr;
  }
  else return nullptr;
  if(method.empty())return nullptr;
  auto before=CefDictionaryValue::Create();before->SetString("type","before");before->SetString("callId","call@"+std::to_string(request->GetInt("id")));before->SetString("class",kind);before->SetString("method",method);before->SetString("pageId","page@"+std::to_string(page));before->SetDouble("startTime",now);before->SetString("apiName",TraceActionTitle(request,kind,method));
  auto params=request->Copy(false);for(const auto* key:{"id","version","command","pageId","timeoutMs","traceStack","traceMethod"})params->Remove(key);before->SetDictionary("params",params);
  if(command=="waitEvent")before->SetString("title","Wait for event \""+request->GetString("event").ToString()+"\"");
  if(request->GetType("traceStack")==VTYPE_LIST){before->SetList("stack",request->GetList("traceStack")->Copy());}return before;
}
} // namespace suitecut
