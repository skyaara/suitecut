// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_controls.h"
#include "request_body_reads.h"
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));using Controls=suitecut::NetworkControls;using Reads=suitecut::NativeRequestBodyReads;using Dict=Controls::Dict;
  double now=1;std::vector<Reads::Result>acks;std::vector<Dict>events,observed;uint64_t body=0;int reservations=0,receipts=0;std::vector<uint8_t>wire;
  Reads reads([&](const std::string&session,const std::string&id,double deadline,Reads::Result ack){assert(session=="real-session"&&id=="actual-multipart"&&deadline==30001);acks.push_back(std::move(ack));},[&]{return now;},[]{});
  Controls controls([](const std::string&,const std::string&,Dict,Controls::Result done){done(true,"");},[&](Dict value){events.push_back(value->Copy(false));},[](const std::string&){assert(false);},[]{},[&]{return now;},[&](uint64_t id,size_t,size_t,std::span<const uint8_t>bytes,bool cancel){if(!cancel){body=id;wire.insert(wire.end(),bytes.begin(),bytes.end());}return true;});
  controls.SetObserver([&](Dict event){observed.push_back(event->Copy(false));});
  controls.SetRequestBodyReader([&](const auto&session,const auto&id,double deadline,auto data,auto receipt){reads.Read(session,id,deadline,std::move(data),std::move(receipt));},[&](const std::string&id){assert(id=="1"&&!observed.empty()&&observed.front()->GetString("event")=="request");++reservations;return suitecut::NativeNetworkPostPreparation::Reservation{[](bool ok,const auto&bytes,bool text){assert(ok&&!text&&bytes==std::vector<uint8_t>({0,255,1}));},[&]{++receipts;}};});
  auto data=CefDictionaryValue::Create();data->SetString("requestId","actual-multipart");data->SetString("type","Fetch");auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/multipart");request->SetString("method","POST");request->SetBool("hasPostData",true);auto entries=CefListValue::Create();entries->SetDictionary(0,CefDictionaryValue::Create());request->SetList("postDataEntries",entries);request->SetDictionary("headers",CefDictionaryValue::Create());data->SetDictionary("request",request);
  controls.Event("Network.requestWillBeSent",data,"real-session");assert(reservations==1&&acks.size()==1&&events.empty()&&controls.QueuedPublication()==1&&controls.PublishedPublication()==0);
  auto response=CefDictionaryValue::Create();response->SetInt("status",200);response->SetString("url","https://native.test/multipart");response->SetDictionary("headers",CefDictionaryValue::Create());data->SetDictionary("response",response);controls.Event("Network.responseReceived",data,"real-session");controls.Event("Network.loadingFinished",data,"real-session");assert(events.empty()&&controls.QueuedPublication()==3&&observed.size()==3);
  acks[0](true,"{\"postData\":\"AP8B\",\"base64Encoded\":true}");assert(receipts==1&&events.empty());controls.Tick(now);assert(body&&wire==std::vector<uint8_t>({0,255,1}));assert(controls.BodyAcknowledged(std::to_string(body),"received"));assert(events.size()==4);
  for(const auto&event:events){auto actual=event->GetDictionary("request");assert(actual->GetString("id")=="1"&&actual->GetString("postDataBodyId")==std::to_string(body)&&!actual->GetBool("postDataUnavailable"));}
  assert(controls.BodyAcknowledged(std::to_string(body),"taken"));controls.Clear();reads.Close();
}
