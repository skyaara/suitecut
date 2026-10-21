// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "include/cef_api_hash.h"
#include "network_controls.h"
#include <cassert>
#include <iostream>
using Dict=CefRefPtr<CefDictionaryValue>;
// The production broker include and NetworkControls run against real CEF value
// objects. Context references and HTTP completions are controlled here; no
// browser, curl IO, cookies or actual worker execution is claimed by this test.
struct Context {std::shared_ptr<int> cef;int id=0;};
struct Owner {
  struct Hooks {
    std::function<void(int,int,std::shared_ptr<int>,Dict,Dict,double,std::shared_ptr<void>,std::function<bool(bool,Dict,const std::string&)>)> route_fetch;
    std::function<void(int)> cancel_route_fetch;
  }hooks_;
};
struct Harness {
  struct Target {std::string id="worker-id",session="worker-A",parent_session,kind="service_worker";bool context_destroyed=false;int context=17;uint64_t generation=3;};
  struct Fatal {bool pending()const{return false;}};
  static constexpr size_t kWorkerTargets=1024;
  std::shared_ptr<Owner> owner=std::make_shared<Owner>();std::weak_ptr<Owner> owner_=owner;
  std::shared_ptr<int> request_context_=std::make_shared<int>(7);int context_=7;
  std::string handle_namespace_="actual-broker",chromium_context_="native-chromium-context";
  bool ready_=true,closing_page_=false;std::unique_ptr<Fatal> network_fatal_;
  std::map<std::string,Target> worker_targets_{{"worker-id",{}}};std::map<std::string,std::string> worker_sessions_{{"worker-A","worker-id"}};
  std::unique_ptr<suitecut::NetworkControls>network_;double now=1;Dict route;bool delivered=false,success=false;unsigned fetched=0,canceled=0;
  std::shared_ptr<void> physical;std::function<bool(bool,Dict,const std::string&)> completion;
  Harness(){
    owner->hooks_.route_fetch=[this](int operation,int context,std::shared_ptr<int> expected,Dict request,Dict options,double deadline,std::shared_ptr<void> lease,auto done){
      assert(operation==13&&context==7&&expected==request_context_&&request->GetString("url")=="https://native.test/worker-data"&&request->GetString("method")=="POST"&&options->GetInt("timeoutMs")==10&&deadline==11);
      std::string error;auto params=suitecut::NativeRouteFetchRequest(request,options,now,deadline,deadline,error);assert(params&&params->GetString("body")=="AP8NCg=="&&params->GetInt("maxRedirects")==20&&params->GetInt("maxRetries")==0);
      ++fetched;physical=std::move(lease);completion=std::move(done);
    };
    owner->hooks_.cancel_route_fetch=[this](int operation){assert(operation==13);++canceled;};
    network_=std::make_unique<suitecut::NetworkControls>([](const std::string&,const std::string&,Dict,auto done){done(true,"");},[this](Dict event){if(auto actual=event->GetDictionary("route"))route=actual->Copy(false);},[](const auto&){assert(false);},[]{},[this]{return now;});
    ConfigureWorkerRouteFetch();
    network_->Command(1,Parse(R"({"action":"registerRoute","registrationId":"handler","url":"**"})"),[](bool ok,const auto&,const auto&){assert(ok);});
  }
  static Dict Parse(const char* value){return CefParseJSON(value,JSON_PARSER_RFC)->GetDictionary();}
  Dict Worker(){auto worker=Parse(R"({"id":"worker-id","type":"service_worker","control":{"owner":"actual-broker","generation":"3"}})");return worker;}
  void Pause(bool parent=false){
    auto data=Parse(R"({"requestId":"fetch-id","networkId":"native-request","resourceType":"Fetch","request":{"url":"https://native.test/worker-data","method":"POST","headers":{"X-Actual":"native"},"postDataBase64":"AP8NCg=="}})");data->SetDictionary("suitecutWorkerInfo",Worker());data->SetBool("suitecutContextWorker",true);
    if(parent){network_->WorkerScriptAttached("worker-id","","worker-A","https://native.test/service.js");auto request=data->Copy(false);request->SetString("requestId","native-request");request->SetString("type","Fetch");network_->Event("Network.requestWillBeSent",request,"worker-A");}
    network_->Event("Fetch.requestPaused",data,parent?"":"worker-A");assert(route);
  }
  void Fetch(){auto command=Parse(R"({"action":"fetch","timeoutMs":10})");command->SetString("callbackScope",route->GetString("callbackScope"));command->SetString("routeId",route->GetString("id"));network_->Command(13,command,[this](bool ok,const auto&,const auto&){delivered=true;success=ok;},100);}
  std::shared_ptr<void> RetainWorkerRouteFetchOwner(){return {};}
  void Finish(){network_->Clear();physical.reset();completion={};assert(!network_->NeedsTick());}
#include "controllers/workers/worker_route_fetch.inc"
};
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));unsigned controls=0;
  const auto actual=std::make_shared<int>(7),foreign=std::make_shared<int>(7);
  assert(suitecut::NativeRouteFetchContextMatches(Context{actual,7},Context{actual,7}));
  for(const auto& expected:std::vector<Context>{{actual,0},{nullptr,7},{foreign,7},{actual,8}})assert(!suitecut::NativeRouteFetchContextMatches(expected,Context{actual,7}));
  assert(!suitecut::NativeRouteFetchContextMatches(Context{actual,7},Context{nullptr,7}));++controls;
  for(int mode=0;mode<13;++mode){Harness h;h.Pause();
    if(mode==1)h.worker_targets_.at("worker-id").id="foreign";
    if(mode==2)h.handle_namespace_="foreign-owner";
    if(mode==3)h.worker_targets_.at("worker-id").generation=4;
    if(mode==4)h.request_context_.reset();
    if(mode==5)h.closing_page_=true;
    if(mode==6)h.ready_=false;
    if(mode==7)h.chromium_context_.clear();
    if(mode==8)h.owner->hooks_.route_fetch={};
    if(mode==9)h.owner->hooks_.cancel_route_fetch={};
    if(mode==10)h.worker_targets_.at("worker-id").context_destroyed=true;
    if(mode==11)h.worker_sessions_.clear();
    if(mode==12)h.worker_targets_.at("worker-id").context=0;
    h.Fetch();if(mode==0){assert(h.fetched==1&&h.physical&&!h.delivered);assert(h.completion(true,Harness::Parse(R"({"responseId":"native-cache"})"),""));assert(h.delivered&&h.success);}else assert(h.delivered&&!h.success&&h.fetched==0);h.Finish();++controls;
  }
  {Harness h;h.Pause(true);assert(h.WorkerRouteFetchSessionOwned("","worker-A")&&!h.WorkerRouteFetchSessionOwned("foreign","worker-A"));h.Fetch();assert(h.fetched==1&&!h.delivered);h.network_->SessionDetached("worker-A");assert(h.delivered&&!h.success&&h.canceled==1&&h.network_->NeedsTick());assert(!h.completion(true,Harness::Parse(R"({"responseId":"late-native-cache"})"),""));h.Finish();++controls;}
  for(const bool parent:{false,true})for(const bool cleared:{false,true}){Harness h;h.Pause(parent);h.Fetch();h.WorkerRouteFetchRuntimeEvent("Runtime.executionContextDestroyed",Harness::Parse(R"({"executionContextId":99})"),"worker-A");assert(!h.delivered&&h.canceled==0);h.WorkerRouteFetchRuntimeEvent(cleared?"Runtime.executionContextsCleared":"Runtime.executionContextDestroyed",Harness::Parse(R"({"executionContextId":17})"),"foreign-session");assert(!h.delivered&&h.canceled==0);h.WorkerRouteFetchRuntimeEvent(cleared?"Runtime.executionContextsCleared":"Runtime.executionContextDestroyed",Harness::Parse(R"({"executionContextId":17})"),"worker-A");assert(h.delivered&&!h.success&&h.canceled==1&&h.network_->NeedsTick());assert(!h.completion(true,Harness::Parse(R"({"responseId":"late-native-cache"})"),""));h.Finish();++controls;}
  for(const bool recreated:{false,true}){Harness h;h.Pause();h.Fetch();auto& target=h.worker_targets_.at("worker-id");target.context_destroyed=!recreated;target.context=recreated?18:0;assert(!h.completion(true,Harness::Parse(R"({"responseId":"late-native-cache"})"),""));assert(h.delivered&&!h.success&&h.physical);h.Finish();++controls;}
  {Harness h;h.Pause();h.Fetch();h.now=11;h.network_->Tick(h.now);assert(h.delivered&&!h.success&&h.canceled==1&&h.network_->NeedsTick());assert(!h.completion(true,Harness::Parse("{}"),""));h.Finish();++controls;}
  {Harness h;h.Pause();h.Fetch();h.network_->Cancel(13);assert(!h.delivered&&h.canceled==1&&h.network_->NeedsTick());assert(!h.completion(true,Harness::Parse("{}"),""));h.Finish();++controls;}
  {Harness h;h.Pause();h.Fetch();h.network_->Clear();assert(!h.delivered&&h.canceled==1&&h.network_->NeedsTick());assert(!h.completion(true,Harness::Parse("{}"),""));h.Finish();++controls;}
  {Harness h;h.worker_targets_.at("worker-id").parent_session="worker-A";assert(!h.WorkerRouteFetchSessionOwned("foreign","worker-A"));++controls;}
  std::cout<<"worker route fetch: "<<controls<<" production broker/NetworkControls groups; controlled HTTP/context, no browser IO claim\n";
}
