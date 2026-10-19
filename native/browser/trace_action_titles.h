// SPDX-License-Identifier: MIT
#pragma once
#include <string>
#include "include/cef_parser.h"
namespace suitecut {
inline std::string TraceQuoted(const std::string& text){auto value=CefValue::Create();value->SetString(text);return CefWriteJSON(value,JSON_WRITER_DEFAULT);}
inline std::string TraceNavigationTitle(const std::string& url){const auto scheme=url.find("://");return "Navigate "+(scheme==std::string::npos?url:url.substr(scheme+3));}
inline std::string TraceQueryPattern(CefRefPtr<CefDictionaryValue> clause){if(clause->GetType("text")==VTYPE_STRING)return clause->GetBool("exact")?TraceQuoted(clause->GetString("text")):clause->GetString("text").ToString();if(clause->GetType("pattern")==VTYPE_STRING)return "/"+clause->GetString("pattern").ToString()+"/"+clause->GetString("flags").ToString();return "";}
inline std::string TraceSelectorTitle(const std::string& selector){
  const auto first=selector.find_first_not_of(" \t\r\n"),last=selector.find_last_not_of(" \t\r\n");
  if(first==std::string::npos||first==last||(selector[first]!='\''&&selector[first]!='"')||selector[last]!=selector[first])return selector;
  std::string text;const char quote=selector[first];
  for(size_t i=first+1;i<last;++i){const char ch=selector[i];if(ch==quote)return selector;if(ch=='\\'&&i+1<last&&(selector[i+1]=='\\'||selector[i+1]==quote))text+=selector[++i];else text+=ch;}
  return "text="+TraceQuoted(text);
}
inline std::string TraceQueryText(CefRefPtr<CefListValue> query,size_t depth=0){
  if(!query||query->GetSize()>128||depth>16){return "";}std::string selector;
  for(size_t index=0;index<query->GetSize();++index){auto clause=query->GetDictionary(index);if(!clause)return "";const auto engine=clause->GetString("engine").ToString();std::string step;
    if(engine=="css"){step=TraceSelectorTitle(clause->GetString("selector"));if(step==":scope"&&query->GetSize()>1)continue;}
    else if(engine=="text"||engine=="label"||engine=="placeholder"||engine=="alt"||engine=="title"||engine=="testId")step=engine+"="+TraceQueryPattern(clause);
    else if(engine=="role"){step="role="+clause->GetString("role").ToString();if(auto name=clause->GetDictionary("name"))step+="[name="+TraceQueryPattern(name)+"]";}
    else if(engine=="custom")step=clause->GetString("name").ToString()+"="+clause->GetString("selector").ToString();
    else if(engine=="nth")step="nth="+std::to_string(clause->GetInt("index"));
    else if(engine=="and"||engine=="or"||engine=="chain")step=engine+"("+TraceQueryText(clause->GetList("query"),depth+1)+")";
    else{auto value=CefValue::Create();value->SetDictionary(clause->Copy(false));step=engine+"("+CefWriteJSON(value,JSON_WRITER_DEFAULT).ToString()+")";}
    if(step.empty()){return "";}if(!selector.empty()){selector+=" >> ";}selector+=step;if(selector.size()>8192){return "";}
  }return selector;
}
inline std::string TraceLocatorTitle(CefRefPtr<CefDictionaryValue> request){
  auto selector=TraceQueryText(request->GetList("query"));if(selector.empty())selector=TraceSelectorTitle(request->GetString("selector"));if(selector.empty())return "";
  std::string escaped;for(const auto ch:selector){if(ch=='\\'||ch=='\'')escaped+='\\';escaped+=ch;}
  return " locator('"+escaped+"')";
}
inline std::string TraceActionTitle(CefRefPtr<CefDictionaryValue> request,const std::string& kind,const std::string& method){
  if(kind=="Dialog")return method=="accept"?"Accept dialog":"Dismiss dialog";
  if(kind=="Route"){if(method=="continue")return "Continue request";if(method=="fulfill")return "Fulfill request";if(method=="abort")return "Abort request";return "Fallback request";}
  if(method=="route")return "Route requests";
  if(method=="goto")return TraceNavigationTitle(request->GetString("url"));
  if(method=="setContent")return "Set content";
  if(method=="ariaSnapshot")return "Capture ARIA snapshot";
  if(method=="highlight")return "Highlight"+TraceLocatorTitle(request);
  if(method=="hideHighlight")return "Hide highlight";
  if(method=="evaluate")return "Evaluate";
  if(method=="evaluateHandle")return "Evaluate handle";
  if(method=="frameElement")return "Get frame element";
  if(method=="reload")return "Reload";
  if(method=="goBack")return "Go back";
  if(method=="goForward")return "Go forward";
  if(method=="waitForTimeout")return "Wait for timeout";
  if(method=="close")return "Close page";
  if(method=="screenshot")return "Screenshot";
  if(method=="pdf")return "Print PDF";
  if(method=="waitForEvent")return "Wait for event "+TraceQuoted(request->GetString("event"));
  if(kind=="Mouse"){
    if(method=="move")return "Mouse move";
    if(method=="dblclick")return "Double click";
    if(method=="click")return "Click";
    if(method=="down")return "Mouse down";
    if(method=="up")return "Mouse up";
    if(method=="wheel")return "Mouse wheel";
  }
  if(kind=="Keyboard"){
    if(method=="insertText")return "Insert "+TraceQuoted(request->GetString("text"));
    if(method=="type")return "Type "+TraceQuoted(request->GetString("text"));
    if(method=="press")return "Press "+TraceQuoted(request->GetString("key"));
    if(method=="down")return "Key down";
    if(method=="up")return "Key up";
  }
  const auto target=TraceLocatorTitle(request);
  if(method=="ariaSnapshotJSON")return "Capture ARIA JSON"+target;
  if(method=="waitForFunction")return "Wait for element predicate"+target;
  if(method=="boundingBox")return "Get bounding box"+target;
  if(method=="click")return "Click"+target;
  if(method=="dblclick")return "Double click"+target;
  if(method=="tap")return "Tap"+target;
  if(method=="hover")return "Hover"+target;
  if(method=="fill")return "Fill"+target;
  if(method=="setInputFiles")return "Set input files"+target;
  if(method=="setChecked")return std::string(request->GetBool("checked")?"Check":"Uncheck")+target;
  if(method=="selectOption")return "Select option"+target;
  if(method=="dragTo")return "Drag"+target;
  if(method=="expect")return "Assert"+target;
  // Unknown future native methods still retain their actual method name.
  return method;
}
} // namespace suitecut
