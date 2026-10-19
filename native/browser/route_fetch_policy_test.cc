// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "include/cef_api_hash.h"
#include "network_controls.h"
using Dict=CefRefPtr<CefDictionaryValue>;
Dict Parse(const char* json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
int main(){
  assert(cef_api_hash(CEF_API_VERSION,0));
  const auto request=Parse(R"({"url":"https://native.test/a","method":"POST","headers":{"X-Actual":"original"},"postDataBase64":"AP8NCg=="})");
  auto options=Parse(R"({"apiContextId":7})");std::string error;
  auto result=suitecut::NativeRouteFetchRequest(request,options,1,100,200,error);
  assert(result&&result->GetString("body")=="AP8NCg=="&&result->GetString("method")=="POST"&&result->GetInt("maxRedirects")==20&&result->GetInt("maxRetries")==0&&result->GetInt("timeoutMs")==30000);
  assert(result->GetList("headers")->GetDictionary(0)->GetString("value")=="original");
  options=Parse(R"({"apiContextId":7,"url":"https://native.test/b","method":"PUT","headers":[],"postData":"","maxRedirects":0,"maxRetries":2,"timeoutMs":0})");
  result=suitecut::NativeRouteFetchRequest(request,options,1,100,200,error);
  assert(result&&result->GetString("url")=="https://native.test/b"&&result->GetString("method")=="PUT"&&result->GetString("body").empty()&&result->GetList("headers")->GetSize()==0&&result->GetInt("timeoutMs")==0);
  for(const char* bad:{R"({"maxRetries":101})",R"({"maxRedirects":-1})",R"({"timeoutMs":1.5})",R"({"timeoutMs":-1})",R"({"headers":{}})",R"({"postData":"!"})"})assert(!suitecut::NativeRouteFetchRequest(request,Parse(bad),1,100,200,error));
  assert(!suitecut::NativeRouteFetchRequest(request,options,100,100,200,error));
  auto unavailable=Parse(R"({"url":"https://native.test/a","method":"POST","postDataUnavailable":true})");
  assert(!suitecut::NativeRouteFetchRequest(unavailable,Parse("{}"),1,100,200,error));
  assert(suitecut::NativeRouteFetchRequest(unavailable,options,1,100,200,error));

  assert(suitecut::NativeRouteFetchDeadline(Parse(R"({"timeoutMs":5})"),1,100,200,error)==6);
  assert(suitecut::NativeRouteFetchDeadline(Parse(R"({"timeoutMs":0})"),1,100,90,error)==90);
  assert(suitecut::NativeRouteFetchDeadline(Parse(R"({"timeoutMs":2147483647})"),1,100,90,error)==90);
  // Genuine NetworkControls pause, command/cancel, and lease lifetime. The
  // HTTP producer is controlled here; this does not claim actual HTTP/CEF IO.
  for(int mode:{0,1,2,3,4,5}){
    double now=1;std::vector<Dict> events;std::vector<int> canceled;std::shared_ptr<void> physical;
    std::function<bool(bool,Dict,const std::string&)> reply;bool delivered=false,success=false;
    suitecut::NetworkControls controls([](const std::string&,const std::string&,Dict,auto done){done(true,"");},[&](Dict event){events.push_back(event);},[](const std::string&){assert(false);},[]{},[&]{return now;});
    controls.SetRouteFetch([&](int owner,const std::string& session,const std::string& request_session,Dict actual,Dict config,double deadline,std::shared_ptr<void> lease,auto done){assert(session.empty()&&request_session.empty()&&owner==3&&actual->GetString("url")=="https://native.test/a"&&config->GetInt("apiContextId")==7&&deadline==(mode==4?6:100));physical=std::move(lease);reply=std::move(done);},[&](int owner){canceled.push_back(owner);});
    controls.Command(1,Parse(R"({"action":"registerRoute","registrationId":"handler","url":"**"})"),[](bool ok,const auto&,const auto&){assert(ok);});
    controls.Event("Fetch.requestPaused",Parse(R"({"requestId":"fetch-real","resourceType":"Fetch","request":{"url":"https://native.test/a","method":"GET","headers":{}}})"),"");
    assert(events.size()==1);const auto route=events.front()->GetDictionary("route");
    auto fetch=Parse(R"({"action":"fetch","apiContextId":7})");fetch->SetString("routeId",route->GetString("id"));fetch->SetString("callbackScope",route->GetString("callbackScope"));
    if(mode==4)fetch->SetInt("timeoutMs",5);
    auto foreign=fetch->Copy(false);foreign->SetString("callbackScope","foreign");controls.Command(2,foreign,[](bool ok,const auto&,const auto&){assert(!ok);},100);
    controls.Command(3,fetch,[&](bool ok,const auto&,const auto&){delivered=true;success=ok;},100);assert(reply&&physical&&!delivered);
    if(mode==0){assert(reply(true,Parse(R"({"responseId":"actual"})"),""));assert(delivered&&success);auto finish=Parse(R"({"action":"abort","errorReason":"Failed"})");finish->SetString("routeId",route->GetString("id"));controls.Command(4,finish,[](bool ok,const auto&,const auto&){assert(ok);});}
    if(mode==1){controls.Cancel(3);assert(!delivered&&canceled==std::vector<int>{3});controls.Clear();}
    if(mode==2){auto finish=Parse(R"({"action":"abort","errorReason":"Failed"})");finish->SetString("routeId",route->GetString("id"));controls.Command(4,finish,[](bool ok,const auto&,const auto&){assert(ok);});assert(delivered&&!success&&canceled==std::vector<int>{3});assert(!reply(true,Parse("{}"),""));assert(!success);}
    if(mode==3){now=30002;controls.Tick(now);assert(delivered&&!success&&canceled==std::vector<int>{3});assert(!reply(true,Parse("{}"),""));assert(!success);}
    if(mode==4){now=6;controls.Tick(now);assert(delivered&&!success&&canceled==std::vector<int>{3});auto finish=Parse(R"({"action":"abort","errorReason":"Failed"})");finish->SetString("routeId",route->GetString("id"));controls.Command(4,finish,[](bool ok,const auto&,const auto&){assert(ok);});}
    if(mode==5){controls.SessionDetached("");assert(delivered&&!success&&canceled==std::vector<int>{3});}
    assert(controls.NeedsTick());physical.reset();reply={};assert(!controls.NeedsTick());controls.Clear();
  }
  {
    double now=1;Dict route;bool fetched=false;
    suitecut::NetworkControls controls([](const std::string&,const std::string&,Dict,auto done){done(true,"");},[&](Dict event){const auto actual=event->GetDictionary("route");if(actual)route=actual->Copy(false);},[](const std::string&){assert(false);},[]{},[&]{return now;});
    controls.SetRouteFetch([&](int,const std::string&,const std::string&,Dict,Dict,double,std::shared_ptr<void>,auto){fetched=true;},[](int){});
    controls.Command(1,Parse(R"({"action":"registerRoute","registrationId":"handler","url":"**"})"),[](bool ok,const auto&,const auto&){assert(ok);});
    controls.Event("Fetch.requestPaused",Parse(R"({"requestId":"fetch-real","resourceType":"Fetch","request":{"url":"https://native.test/a","method":"POST","headers":{}}})"),"");assert(route);
    auto reset=Parse(R"({"action":"resetBody","fetchOptions":{"timeoutMs":5}})");reset->SetString("routeId",route->GetString("id"));reset->SetString("callbackScope",route->GetString("callbackScope"));
    controls.Command(2,reset,[](bool ok,const auto&,const auto&){assert(ok);},100);
    now=7;auto chunk=Parse(R"({"action":"bodyChunk","body":"AP8="})");chunk->SetString("routeId",route->GetString("id"));
    controls.Command(3,chunk,[](bool ok,const auto&,const auto&){assert(!ok);},200);
    auto fetch=reset->Copy(false);fetch->Remove("fetchOptions");fetch->SetString("action","fetch");fetch->SetBool("bodyUpload",true);fetch->SetInt("timeoutMs",500);
    controls.Command(4,fetch,[](bool ok,const auto&,const auto&){assert(!ok);},1000);assert(!fetched);controls.Clear();
  }
}
