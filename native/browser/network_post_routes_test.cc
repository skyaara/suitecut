// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_controls.h"
#include "request_body_reads.h"
using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
Dict Parse(const char*json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
Dict Request(){return Parse(R"({"url":"https://native.test/multipart","method":"POST","headers":{},"hasPostData":true,"postDataEntries":[{}]})");}
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));
 for(const int scenario:{0,1,2}){const bool har=scenario!=0,cold=scenario==2;
  std::vector<suitecut::NativeRequestBodyReads::Result>acks;std::vector<Dict>events;std::vector<std::string>sent;uint64_t body=0;
  suitecut::NativeRequestBodyReads reads([&](const std::string&,const std::string&id,double deadline,auto ack){assert(id=="actual-request"&&deadline==30001);acks.push_back(std::move(ack));},[]{return 1;},[]{});
  Controls controls([&](const std::string&,const std::string&method,Dict params,Controls::Result done){sent.push_back(method);if(method=="Fetch.fulfillRequest"){assert(har&&params->GetInt("responseCode")==201&&params->GetString("body")=="YWN0dWFs");}done(true,"");},[&](Dict event){events.push_back(event->Copy(false));},[](const std::string&){assert(false);},[]{},[]{return 1;},[&](uint64_t id,size_t,size_t,std::span<const uint8_t>,bool canceled){assert(!canceled);body=id;return true;});
  controls.SetRequestBodyReader([&](const auto&session,const auto&id,double deadline,auto data,auto receipt){reads.Read(session,id,deadline,std::move(data),std::move(receipt));});
  if(har){auto replay=std::make_shared<suitecut::NativeHARReplay>();std::string error;assert(replay->Load(Parse(R"({"log":{"entries":[{"request":{"url":"https://native.test/multipart","method":"POST","headers":[],"postData":{"text":"AP8B","_encoding":"base64"}},"response":{"status":201,"statusText":"Created","headers":[],"content":{"text":"actual"}}}]}})"),error));assert(controls.BindReplay("actual-har","**",replay,false,[](bool ok,const std::string&){assert(ok);}));}
  else controls.Command(1,Parse(R"({"action":"registerRoute","registrationId":"actual-handler","url":"**"})"),[](bool ok,const std::string&,const std::string&){assert(ok);});
  sent.clear();
  auto request=Parse(R"({"requestId":"actual-request","type":"Fetch"})");request->SetDictionary("request",Request());if(!cold)controls.Event("Network.requestWillBeSent",request,"");
  auto pause=Parse(R"({"requestId":"actual-fetch","networkId":"actual-request","resourceType":"Fetch"})");pause->SetDictionary("request",Request());controls.Event("Fetch.requestPaused",pause,"");assert(acks.size()==1&&events.empty()&&sent.empty());
  acks.front()(true,R"({"postData":"AP8B","base64Encoded":true})");
  if(har)assert(sent==std::vector<std::string>({"Fetch.fulfillRequest"}));else assert(sent.empty());
  if(cold){controls.Tick(1);assert(body==0&&events.empty());auto diagnostics=CefDictionaryValue::Create();controls.Diagnostics(diagnostics);assert(diagnostics->GetInt("networkBodyOwners")==0&&diagnostics->GetInt("networkPostPreparations")==0);controls.Clear();reads.Close();continue;}
  controls.Tick(1);assert(body);assert(controls.BodyAcknowledged(std::to_string(body),"received"));assert(!events.empty());
  if(!har){assert(events.size()==3&&events[1]->GetString("event")=="route");auto projected=events[1]->GetDictionary("request");assert(projected->GetString("id")=="1"&&projected->GetString("postDataBodyId")==std::to_string(body));auto resolve=Parse(R"({"action":"continue"})");resolve->SetString("routeId",events[1]->GetDictionary("route")->GetString("id"));controls.Command(2,resolve,[](bool ok,const std::string&,const std::string&){assert(ok);});assert(sent==std::vector<std::string>({"Fetch.continueRequest"}));}
  assert(controls.BodyAcknowledged(std::to_string(body),"taken"));controls.Clear();reads.Close();
 }
}
