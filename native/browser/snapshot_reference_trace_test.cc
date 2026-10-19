// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "trace_action_metadata.h"
#include <cassert>
#include <iostream>
int main(){assert(cef_api_hash(CEF_API_VERSION,0));
  auto request=CefDictionaryValue::Create();request->SetString("command","snapshotReferences");request->SetInt("id",71);request->SetString("frame","actual-native-child-frame");request->SetBool("accessibilityBoxes",true);request->SetInt("referenceDepth",3);auto query=CefListValue::Create();auto ref=CefDictionaryValue::Create();ref->SetString("engine","reference");ref->SetString("ref","rActualNativeNamespace_7_3");query->SetDictionary(0,ref);request->SetList("query",query);
  const auto result=suitecut::TraceActionMetadata(request,4,10);assert(result&&result->GetString("class")=="Frame"&&result->GetString("method")=="ariaSnapshot"&&result->GetString("apiName")=="Capture ARIA snapshot"&&result->GetString("pageId")=="page@4"&&result->GetString("callId")=="call@71"&&result->GetDouble("startTime")==10);
  const auto params=result->GetDictionary("params");assert(params&&params->GetString("frame")=="actual-native-child-frame"&&params->GetBool("accessibilityBoxes")&&params->GetInt("referenceDepth")==3&&params->GetList("query")->GetDictionary(0)->GetString("ref")=="rActualNativeNamespace_7_3");assert(!result->HasKey("title")&&!params->HasKey("id")&&!params->HasKey("command"));assert(request->GetString("command")=="snapshotReferences"&&request->GetInt("id")==71&&request->GetList("query")->GetDictionary(0)->GetString("ref")=="rActualNativeNamespace_7_3");
  for(const char* command:{"snapshotReferences","queryEvaluate","dialog","network","waitEvent"})assert(!suitecut::TraceActionHasPhases(command));
  for(const char* command:{"locator","evaluate","navigateFrame","artifact","handle"})assert(suitecut::TraceActionHasPhases(command));
  assert(!suitecut::TraceActionCapturesAfterDOM("ariaSnapshot"));
  for(const char* method:{"expect","evaluate","click","goto","screenshot","waitForEvent","actual-unknown-method"})assert(suitecut::TraceActionCapturesAfterDOM(method));
  auto plain=CefDictionaryValue::Create();plain->SetString("command","evaluate");plain->SetInt("id",1);const auto baseline=suitecut::TraceActionMetadata(plain,2,3);assert(baseline&&baseline->GetString("method")=="evaluate"&&baseline->GetString("apiName")=="Evaluate");plain->SetString("command","actual-unknown-command");assert(!suitecut::TraceActionMetadata(plain,2,3));
  std::cout<<"PASS5 actual CEF reference trace metadata/title/unaltered params and native before/after capture classification; no browser trace archive claim\n";
}
