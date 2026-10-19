// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "locator_normalization_metadata.h"
#include <cassert>
#include <iostream>
using Dict=CefRefPtr<CefDictionaryValue>;
int main(){assert(cef_api_hash(CEF_API_VERSION,0));
 const std::string ast=R"JSON({"kind":"id","query":[{"engine":"css","selector":"#actual"}]})JSON";
 const auto capture=[&]{auto result=CefDictionaryValue::Create(),remote=CefDictionaryValue::Create(),deep=CefDictionaryValue::Create(),element=CefDictionaryValue::Create(),node=CefDictionaryValue::Create(),text=CefDictionaryValue::Create();node->SetInt("backendNodeId",913);node->SetInt("nodeType",1);node->SetString("nodeName","BUTTON");element->SetString("type","node");element->SetDictionary("value",node);text->SetString("type","string");text->SetString("value",ast);auto values=CefListValue::Create();values->SetDictionary(0,element);values->SetDictionary(1,text);deep->SetString("type","array");deep->SetList("value",values);remote->SetString("subtype","array");remote->SetDictionary("deepSerializedValue",deep);result->SetDictionary("result",remote);return result;};
 const auto values=[](Dict result){return result->GetDictionary("result")->GetDictionary("deepSerializedValue")->GetList("value");};
 int backend=42;std::string metadata="old",error;const auto valid=capture();assert(suitecut::LocatorNormalizationCapture(valid,backend,metadata,error)&&backend==913&&metadata==ast&&error.empty());
 const auto invalid=[&](Dict candidate){backend=42;metadata="old";assert(!suitecut::LocatorNormalizationCapture(candidate,backend,metadata,error)&&backend==0&&metadata.empty()&&!error.empty());};
 invalid(nullptr);auto candidate=capture();candidate->SetDictionary("exceptionDetails",CefDictionaryValue::Create());invalid(candidate);
 candidate=capture();candidate->GetDictionary("result")->SetString("subtype","object");invalid(candidate);
 candidate=capture();candidate->GetDictionary("result")->GetDictionary("deepSerializedValue")->SetString("type","object");invalid(candidate);
 candidate=capture();values(candidate)->SetSize(3);invalid(candidate);
 candidate=capture();values(candidate)->GetDictionary(0)->SetString("type","object");invalid(candidate);
 candidate=capture();values(candidate)->GetDictionary(0)->GetDictionary("value")->SetInt("nodeType",3);invalid(candidate);
 candidate=capture();values(candidate)->GetDictionary(0)->GetDictionary("value")->SetInt("backendNodeId",0);invalid(candidate);
 candidate=capture();values(candidate)->GetDictionary(0)->GetDictionary("value")->SetString("backendNodeId","913");invalid(candidate);
 candidate=capture();values(candidate)->GetDictionary(1)->SetString("type","object");invalid(candidate);
 candidate=capture();values(candidate)->GetDictionary(1)->SetDictionary("value",CefDictionaryValue::Create());invalid(candidate);
 candidate=capture();values(candidate)->GetDictionary(1)->SetString("value",R"JSON({"kind":"id","query":[{"engine":"reference","ref":"invented"}]})JSON");invalid(candidate);
 candidate=capture();values(candidate)->GetDictionary(0)->GetDictionary("value")->SetString("__suitecutNativeUTF16Scalar","d800");assert(suitecut::LocatorNormalizationCapture(candidate,backend,metadata,error)&&backend==913&&metadata==ast);
 std::cout<<"PASS14 real-CEF finite normalization capture/backend/schema and author-shaped irrelevant field groups; no installed browser claim\n";
}
