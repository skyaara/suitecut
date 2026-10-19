// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <string>
#include "artifact_controls.h"
#include "devtools_utf16.h"
namespace suitecut {
// Validate the actual native stage response even when its logical continuation
// is suppressed by assertion freeze. No late pixel value is admitted here.
struct ScreenshotResponseObservation {bool valid=false;std::string error;CefRefPtr<CefDictionaryValue> result;};
inline ScreenshotResponseObservation ObserveScreenshotResponse(SuiteCutArtifactController::ResponseStage stage,bool ok,const std::string&json){
  ScreenshotResponseObservation observation;
  const auto parsed=CefParseJSON(json,JSON_PARSER_RFC);observation.result=parsed&&parsed->GetType()==VTYPE_DICTIONARY?parsed->GetDictionary():nullptr;
  const auto fail=[&](const std::string&error){observation.error=error;return observation;};
  if(!ok){const auto message=observation.result?DevToolsString(observation.result->GetValue("message")):std::string{};return fail(message.empty()?"Actual native screenshot renderer command failed":message);}
  const auto result=observation.result;if(!result)return fail("Invalid native screenshot renderer response object");
  if(result->HasKey("exceptionDetails")){const auto details=result->GetDictionary("exceptionDetails");const auto exception=details?details->GetDictionary("exception"):nullptr;const auto error=exception?DevToolsString(exception->GetValue("description")):details?DevToolsString(details->GetValue("text")):std::string{};return fail(error.empty()?"Actual native screenshot renderer exception":error);}
  using Stage=SuiteCutArtifactController::ResponseStage;
  const auto number=[](CefRefPtr<CefDictionaryValue>value,const char*name,double&out){if(!value)return false;const auto type=value->GetType(name);if(type!=VTYPE_INT&&type!=VTYPE_DOUBLE)return false;out=type==VTYPE_INT?value->GetInt(name):value->GetDouble(name);return std::isfinite(out);};
  if(stage==Stage::Boolean){const auto remote=result->GetDictionary("result");if(!remote||remote->GetString("type")!="boolean"||remote->GetType("value")!=VTYPE_BOOL||!remote->GetBool("value"))return fail("Invalid native screenshot boolean readiness response");}
  else if(stage==Stage::Viewport){const auto remote=result->GetDictionary("result"),value=remote?remote->GetDictionary("value"):nullptr;double width=0,height=0;if(!remote||remote->GetString("type")!="object"||!number(value,"width",width)||!number(value,"height",height)||width<=0||height<=0)return fail("Invalid native screenshot viewport readiness response");}
  else if(stage==Stage::Layout){const auto viewport=result->GetDictionary("cssVisualViewport"),content=result->GetDictionary("cssContentSize");double x=0,y=0,cx=0,cy=0,width=0,height=0;if(!number(viewport,"pageX",x)||!number(viewport,"pageY",y)||!number(content,"x",cx)||!number(content,"y",cy)||!number(content,"width",width)||!number(content,"height",height)||width<=0||height<=0)return fail("Invalid native screenshot layout response");}
  else if(stage==Stage::PNG){if(result->GetType("data")!=VTYPE_STRING||result->GetString("data").empty()||result->GetString("data").length()>((2*1024*1024+2)/3)*4)return fail("Invalid or excessive native screenshot PNG response");}
  else if(stage==Stage::PDF)return fail("PDF is not a native screenshot assertion stage");
  observation.valid=true;return observation;
}
}
