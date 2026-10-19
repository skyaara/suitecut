// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <set>
#include <string>
#include "include/cef_parser.h"
#include "storage_utf16.h"
namespace suitecut {
// Validate only the native normalizer's finite builtin AST envelope. Unicode
// projection is used for shape validation only; Source returns the original
// JSON string unchanged. Author text/tag-shaped strings are not decoded.
inline bool LocatorNormalizationMetadata(const std::string& original,std::string& error){
  const auto fail=[&](const char* message){error=message;return false;};
  if(original.empty()||original.size()>32768)return fail("Native normalized locator metadata exceeds byte bounds");
  const auto parsed=CefParseJSON(StorageJSONValidationProjection(original),JSON_PARSER_RFC);
  const auto envelope=parsed?parsed->GetDictionary():nullptr;
  const auto keys=[](CefRefPtr<CefDictionaryValue> value,const std::set<std::string>& wanted){
    if(!value||value->GetSize()!=wanted.size())return false;
    CefDictionaryValue::KeyList actual;value->GetKeys(actual);for(const auto& key:actual)if(!wanted.contains(key.ToString()))return false;return true;
  };
  const auto text=[](CefRefPtr<CefDictionaryValue> value,const char* name,size_t maximum,bool nonempty=false){return value&&value->GetType(name)==VTYPE_STRING&&value->GetString(name).length()<=maximum&&(!nonempty||!value->GetString(name).empty());};
  if(!keys(envelope,{"kind","query"})||!text(envelope,"kind",16,true))return fail("Invalid native normalized locator envelope");
  const auto kind=envelope->GetString("kind").ToString();
  if(kind!="id"&&kind!="role"&&kind!="testId"&&kind!="attribute"&&kind!="path")return fail("Unsupported native normalized locator kind");
  const auto query=envelope->GetList("query");
  if(!query||!query->GetSize()||query->GetSize()>64||(kind!="path"&&query->GetSize()!=1))return fail("Native normalized locator query exceeds step bounds");
  auto value=CefValue::Create();value->SetList(query->Copy());if(CefWriteJSON(value,JSON_WRITER_DEFAULT).length()>16384)return fail("Native normalized locator query exceeds encoded bounds");
  for(size_t index=0;index<query->GetSize();++index){
    const auto step=query->GetDictionary(index);if(!text(step,"engine",16,true))return fail("Invalid native normalized locator query step");
    const auto engine=step->GetString("engine").ToString();
    if(engine=="css"){
      if((kind!="id"&&kind!="path")||!keys(step,{"engine","selector"})||!text(step,"selector",16384,true)||(kind=="id"&&step->GetString("selector").ToString().front()!='#'))return fail("Invalid native normalized CSS locator");
    }else if(engine=="testId"){
      if(kind!="testId"||!keys(step,{"engine","attribute","text","exact"})||!text(step,"attribute",256,true)||!text(step,"text",16384)||step->GetType("exact")!=VTYPE_BOOL||!step->GetBool("exact"))return fail("Invalid native normalized test-ID locator");
    }else if(engine=="role"){
      const auto name=step->GetDictionary("name");
      if(kind!="role"||!keys(step,{"engine","role","name"})||!text(step,"role",128,true)||!keys(name,{"text","exact"})||!text(name,"text",16384)||name->GetType("exact")!=VTYPE_BOOL||!name->GetBool("exact"))return fail("Invalid native normalized role locator");
      const auto role=step->GetString("role").ToString();if(!std::all_of(role.begin(),role.end(),[](unsigned char ch){return(ch>='a'&&ch<='z')||ch=='-';}))return fail("Invalid native normalized role token");
    }else if(engine=="placeholder"||engine=="alt"||engine=="title"){
      if(kind!="attribute"||!keys(step,{"engine","text","exact"})||!text(step,"text",16384)||step->GetType("exact")!=VTYPE_BOOL||!step->GetBool("exact"))return fail("Invalid native normalized attribute locator");
    }else return fail("Native normalized locator contains an unsupported engine");
  }
  error.clear();return true;
}
// This finite reply belongs only to the native utility normalizer. Chromium
// serializes the exact selected Element and the authored metadata JSON in one
// synchronous invocation, so backend identity and identity-validated AST share
// one actual capture boundary. Arbitrary author dictionaries are never walked.
inline bool LocatorNormalizationCapture(CefRefPtr<CefDictionaryValue> result,
    int& backend,std::string& metadata,std::string& error){
  backend=0;metadata.clear();
  const auto fail=[&](const char* message){error=message;return false;};
  if(!result||result->HasKey("exceptionDetails"))return fail("Native normalized locator capture failed");
  const auto remote=result->GetDictionary("result");
  const auto deep=remote&&remote->GetString("subtype")=="array"?remote->GetDictionary("deepSerializedValue"):nullptr;
  const auto values=deep&&deep->GetString("type")=="array"?deep->GetList("value"):nullptr;
  if(!values||values->GetSize()!=2)return fail("Invalid native normalized locator capture envelope");
  const auto element=values->GetDictionary(0),text=values->GetDictionary(1);
  const auto node=element&&element->GetString("type")=="node"?element->GetDictionary("value"):nullptr;
  if(!node||node->GetType("nodeType")!=VTYPE_INT||node->GetInt("nodeType")!=1||
      node->GetType("backendNodeId")!=VTYPE_INT||node->GetInt("backendNodeId")<=0)
    return fail("Native normalized locator capture is not a genuine Element backend node");
  if(!text||text->GetString("type")!="string"||text->GetType("value")!=VTYPE_STRING)
    return fail("Native normalized locator AST JSON is unavailable");
  const auto encoded=text->GetString("value").ToString();
  if(!LocatorNormalizationMetadata(encoded,error))return false;
  backend=node->GetInt("backendNodeId");metadata=encoded;error.clear();return true;
}
}
