// SPDX-License-Identifier: MIT
#include "include/cef_api_hash.h"
#include "include/cef_parser.h"
#include "visibility_snapshot_reply.h"
#include <cassert>
#include <iostream>
int main(){
  cef_api_hash(CEF_API_VERSION,0);
  for(const bool visible:{false,true}){
    auto envelope=CefDictionaryValue::Create();auto old=CefDictionaryValue::Create();
    old->SetString("type","object");old->SetString("objectId","actual-wrapper");old->SetString("description","Object");
    auto wrapper=CefDictionaryValue::Create();wrapper->SetBool("waiting",false);wrapper->SetBool("value",visible);old->SetDictionary("value",wrapper);envelope->SetDictionary("result",old);
    envelope->SetString("actualReceipt","preserved");
    auto observed=envelope->GetDictionary("result")->GetDictionary("value")->GetValue("value");
    assert(SuiteCutVisibilitySnapshotReply(envelope,observed));
    auto actual=envelope->GetDictionary("result");assert(actual&&actual->GetSize()==2);assert(actual->GetString("type")=="boolean");assert(actual->GetType("value")==VTYPE_BOOL);assert(actual->GetBool("value")==visible);
    assert(!actual->HasKey("objectId")&&!actual->HasKey("description"));assert(envelope->GetString("actualReceipt")=="preserved");
    auto owned=CefValue::Create();owned->SetDictionary(envelope);envelope=nullptr;actual=nullptr;old=nullptr;wrapper=nullptr;
    auto copied=CefParseJSON(CefWriteJSON(owned,JSON_WRITER_DEFAULT),JSON_PARSER_RFC);assert(copied);assert(copied->GetDictionary()->GetDictionary("result")->GetBool("value")==visible);
  }
  auto envelope=CefDictionaryValue::Create();envelope->SetString("unrelated","actual");
  for(const int kind:{0,1,2}){auto value=CefValue::Create();if(kind==0)value->SetNull();if(kind==1)value->SetString("false");if(kind==2)value->SetDictionary(CefDictionaryValue::Create());assert(!SuiteCutVisibilitySnapshotReply(envelope,value));assert(envelope->GetSize()==1&&envelope->GetString("unrelated")=="actual");}
  assert(!SuiteCutVisibilitySnapshotReply(nullptr,nullptr));
  std::cout<<"PASS native visibility boolean reply: actual false/true, fresh CEF ownership/serialization, nonboolean rejection, unrelated envelope preserved\n";
}
