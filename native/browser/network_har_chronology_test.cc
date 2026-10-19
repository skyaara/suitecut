// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <iostream>
#include "include/cef_api_hash.h"
#include "network_controls.h"
using Controls=suitecut::NetworkControls;
using Dict=Controls::Dict;
using Policy=suitecut::NativeHARRouteRegistration;
Dict Parse(const char*json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
std::shared_ptr<const suitecut::NativeHARReplay> Archive(const std::string&body,bool post=false,bool empty=false){
  auto json=Parse(R"({"log":{"entries":[]}})");
  if(!empty){
    auto entry=Parse(R"({"request":{"url":"https://native.test/original","method":"GET","headers":[]},"response":{"status":200,"headers":[],"content":{"text":""}}})");
    entry->GetDictionary("response")->GetDictionary("content")->SetString("text",body);
    if(post){auto request=entry->GetDictionary("request");request->SetString("method","POST");request->SetDictionary("postData",Parse(R"({"text":"actual-post"})"));}
    json->GetDictionary("log")->GetList("entries")->SetDictionary(0,entry);
  }
  auto result=std::make_shared<suitecut::NativeHARReplay>();std::string error;assert(result->Load(json,error));return result;
}
struct Fixture{
  struct Call{std::string method;Dict params;Controls::Result ack;};
  struct Read{suitecut::NativeNetworkPostPreparation::Data data;suitecut::NativeNetworkPostPreparation::Receipt receipt;};
  double now=1;bool hold=false;std::vector<Dict>events;std::vector<std::string>errors;std::vector<Call>sent;std::deque<Call>pending;std::vector<Read>reads;
  Controls controls{
    [this](const std::string&,const std::string&method,Dict params,Controls::Result ack){sent.push_back({method,params?params->Copy(false):nullptr,{}});if(hold&&(method=="Fetch.fulfillRequest"||method=="Fetch.failRequest"))pending.push_back({method,params,std::move(ack)});else ack(true,"");},
    [this](Dict event){events.push_back(event->Copy(false));},[this](const std::string&error){errors.push_back(error);},[]{},[this]{return now;},
    [](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;}};
  Fixture(){controls.SetRequestBodyReader([this](const auto&,const auto&,double,auto data,auto receipt){reads.push_back({std::move(data),std::move(receipt)});});}
  void Register(const std::string&id){auto config=Parse(R"({"action":"registerRoute","url":"**"})");config->SetString("registrationId",id);controls.Command(1,config,[](bool ok,const auto&,const auto&){assert(ok);});}
  void Bind(const std::string&id,Policy::Policy policy,bool context=false){assert(controls.BindReplayPolicy(id,"**",std::move(policy),[](bool ok,const auto&){assert(ok);},0,context));}
  void Unbind(const std::string&id){controls.UnbindReplay(id,[](bool ok,const auto&){assert(ok);});}
  void Pause(const std::string&id,bool post=false){
    auto actual=Parse(R"({"resourceType":"Fetch","frameId":"actual-frame","request":{"url":"https://native.test/original","method":"GET","headers":{}}})");actual->SetString("requestId","fetch:"+id);actual->SetString("networkId",id);
    if(post){auto request=actual->GetDictionary("request");request->SetString("method","POST");request->SetBool("hasPostData",true);request->SetList("postDataEntries",Parse(R"({"entries":[{}]})")->GetList("entries"));}
    controls.Event("Fetch.requestPaused",actual,"");
  }
  Dict LastRoute(){for(auto it=events.rbegin();it!=events.rend();++it)if((*it)->GetString("event")=="route")return (*it)->GetDictionary("route");assert(false);return nullptr;}
  void Body(size_t index){auto data=std::move(reads.at(index).data);assert(data);const std::string value="actual-post";data(true,{value.begin(),value.end()},true,"");}
  void Receipt(size_t index){auto receipt=std::move(reads.at(index).receipt);assert(receipt);receipt();}
  void Ack(){assert(!pending.empty());auto call=std::move(pending.front());pending.pop_front();call.ack(true,"");}
  std::string LastBody(){for(auto it=sent.rbegin();it!=sent.rend();++it)if(it->method=="Fetch.fulfillRequest")return it->params->GetString("body");return "";}
  // Controlled fixture teardown explicitly models the native OnBeforeClose
  // channel boundary; logical Clear alone cannot acknowledge a paused Fetch.
  void Close(){controls.HARWorkChannelClosed();controls.Clear();}
};
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  {
    auto normal=suitecut::NativeContextRouteRegistration::Create("older-normal","**",0);assert(normal);auto har=Policy::Create(Archive("newer"),false);assert(har);
    Fixture current,future;assert(current.controls.BindContextRoute(normal,[](bool ok,const auto&){assert(ok);}));current.Bind("new-har",har,true);
    future.Bind("new-har",har,true);assert(future.controls.BindContextRoute(normal,[](bool ok,const auto&){assert(ok);}));
    current.Pause("current");future.Pause("future");assert(current.LastBody()=="bmV3ZXI="&&future.LastBody()=="bmV3ZXI=");current.Close();future.Close();
  }
  {
    Fixture f;f.Register("older-page");f.Bind("newer-context-har",Policy::Create(Archive("context"),false),true);f.Pause("page-first");
    auto config=Parse(R"({"action":"fallback"})");config->SetString("routeId",f.LastRoute()->GetString("id"));assert(f.LastRoute()->GetString("registrationId")=="older-page");
    bool done=false;f.controls.Command(2,config,[&](bool ok,const auto&,const auto&){assert(ok);done=true;});assert(done&&f.LastBody()=="Y29udGV4dA==");f.Close();
  }
  {
    Fixture f;f.Register("older-page");f.Bind("newer-empty-har",Policy::Create(Archive("",false,true),true));f.Pause("har-miss");assert(f.LastRoute()->GetString("registrationId")=="older-page");f.Close();
  }
  {
    const auto before_read=suitecut::NativeRouteRegistrationOrder::Reserve();assert(before_read);Fixture f;f.Register("admitted-after-read-start");f.Bind("older-late-file",Policy::Create(Archive("older"),false,*before_read));f.Pause("file-completion");assert(f.LastRoute()->GetString("registrationId")=="admitted-after-read-start");f.Close();
  }
  {
    Fixture f;f.hold=true;auto original=Policy::Create(Archive("original",true),false);std::weak_ptr<const Policy>weak=original;f.Bind("reused-id",original);original.reset();f.Pause("held-post",true);assert(f.reads.size()==1);
    f.Unbind("reused-id");f.Bind("reused-id",Policy::Create(Archive("replacement",true),false));assert(!weak.expired());
    f.Body(0);assert(f.LastBody()=="b3JpZ2luYWw="&&f.pending.size()==1);f.Ack();assert(!weak.expired()); // The independent body receipt is still real pending work.
    f.Receipt(0);assert(weak.expired());f.Close();
  }
  {
    Fixture f;f.hold=true;f.Bind("timed-post",Policy::Create(Archive("post",true),false));f.Pause("deadline",true);
    assert(f.reads.size()==1);f.now=30002;f.Body(0);assert(f.LastBody().empty()&&f.pending.size()==1&&f.pending.front().method=="Fetch.failRequest");f.Ack();assert(f.errors.size()==1&&f.errors.front().find("original")!=std::string::npos);f.Receipt(0);f.Close();
  }
  {
    Fixture f;f.hold=true;auto policy=Policy::Create(Archive("held-fetch"),false);std::weak_ptr<const Policy>weak=policy;f.Bind("held",policy);policy.reset();f.Pause("fetch-ack");f.Unbind("held");assert(!weak.expired()&&f.pending.size()==1);f.Ack();assert(weak.expired());f.Close();
  }
  assert(Policy::retained_policies()==0&&Policy::retained_archives()==0&&Policy::retained_bytes()==0);
  std::cout<<"Native HAR chronology: 7 shared-order/fallback/archive-receipt groups passed\n";
}
