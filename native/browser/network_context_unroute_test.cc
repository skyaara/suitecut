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
using Policy=suitecut::NativeHARRouteRegistration;
using Dict=Controls::Dict;
Dict Parse(const char*json){return CefParseJSON(json,JSON_PARSER_RFC)->GetDictionary();}
auto Archive(bool post=false){auto json=Parse(R"({"log":{"entries":[{"request":{"url":"https://native.test/original","method":"GET","headers":[]},"response":{"status":200,"headers":[],"content":{"text":"actual"}}}]}})");if(post){auto request=json->GetDictionary("log")->GetList("entries")->GetDictionary(0)->GetDictionary("request");request->SetString("method","POST");request->SetDictionary("postData",Parse(R"({"text":"actual-post"})"));}auto result=std::make_shared<suitecut::NativeHARReplay>();std::string error;assert(result->Load(json,error));return result;}
struct Fixture {
 struct Call{std::string session,method;Controls::Result ack;double deadline;};
 double now=1;std::vector<std::string>errors;std::deque<Call>pending;std::vector<std::string>methods;
 suitecut::NativeNetworkPostPreparation::Data data;suitecut::NativeNetworkPostPreparation::Receipt receipt;
 Controls controls{[this](const auto&session,const auto&method,Dict,auto ack){Send(session,method,std::move(ack),0);},[](Dict){},[this](const auto&error){errors.push_back(error);},[]{},[this]{return now;},[](uint64_t,size_t,size_t,std::span<const uint8_t>,bool){return true;}};
 Fixture(){controls.SetBoundedSender([this](const auto&session,const auto&method,Dict,auto ack,double deadline){Send(session,method,std::move(ack),deadline);});controls.SetRequestBodyReader([this](const auto&,const auto&,double,auto d,auto r){data=std::move(d);receipt=std::move(r);});}
 void Send(const std::string&session,const std::string&method,Controls::Result ack,double deadline){methods.push_back(method);if(method=="Fetch.fulfillRequest"||method=="Fetch.failRequest")pending.push_back({session,method,std::move(ack),deadline});else ack(true,"");}
 void Bind(const std::string&id,const Policy::Policy&policy){assert(controls.BindReplayPolicy(id,"**",policy,[](bool ok,const auto&){assert(ok);},0,true));}
 void Pause(bool post=false,const std::string&session=""){auto actual=Parse(R"({"requestId":"actual-fetch","networkId":"actual-network","resourceType":"Fetch","request":{"url":"https://native.test/original","method":"GET","headers":{}}})");if(post){auto request=actual->GetDictionary("request");request->SetString("method","POST");request->SetBool("hasPostData",true);request->SetList("postDataEntries",Parse(R"({"entries":[{}]})")->GetList("entries"));}controls.Event("Fetch.requestPaused",actual,session);}
 void Ack(bool ok=true){assert(!pending.empty());auto call=std::move(pending.front());pending.pop_front();call.ack(ok,ok?"":"actual Fetch rejection");}
 auto Capture(const Policy::Policy&policy){auto selected=suitecut::GlobalHARWorkLeases().Capture([&](const auto&actual){return actual==policy;});assert(selected);return *selected;}
 void Body(){assert(data);auto actual=std::move(data);const std::string body="actual-post";actual(true,{body.begin(),body.end()},true,"");}
 void Close(){controls.HARWorkChannelClosed();controls.Clear();}
};
int main(){assert(cef_api_hash(CEF_API_VERSION,0));
 {Fixture f;auto policy=Policy::Create(Archive(),false);f.Bind("har",policy);f.Pause();auto selected=f.Capture(policy);assert(selected.size()==1&&!selected.front()->settled()&&f.pending.size()==1);bool removed=false;f.controls.UnbindReplayPolicy("har",policy,[&](bool ok,const auto&){assert(ok);removed=true;},100);assert(removed&&!selected.front()->settled());f.Ack();assert(selected.front()->settled()&&!selected.front()->failed());f.Close();}
 {Fixture f;auto policy=Policy::Create(Archive(true),false);f.Bind("post",policy);f.Pause(true);auto selected=f.Capture(policy);assert(selected.size()==2&&f.data&&f.receipt);f.Body();assert(f.pending.size()==1&&!selected[0]->settled()&&!selected[1]->settled());f.Ack();assert(selected[0]->settled()!=selected[1]->settled());f.receipt();assert(selected[0]->settled()&&selected[1]->settled());f.Close();}
 {Fixture f;auto policy=Policy::Create(Archive(),false);f.Bind("har",policy);f.Pause();auto selected=f.Capture(policy);f.Ack(false);assert(selected.size()==1&&selected.front()->failed()&&!selected.front()->settled()&&f.pending.size()==1&&f.pending.front().method=="Fetch.failRequest"&&f.pending.front().deadline==1001);f.Ack();assert(selected.front()->settled()&&selected.front()->failed());f.Close();}
 {Fixture f;auto policy=Policy::Create(Archive(),false);f.Bind("har",policy);f.Pause();auto selected=f.Capture(policy);f.Ack(false);f.Ack(false);assert(selected.front()->failed()&&!selected.front()->settled()&&!f.errors.empty());f.controls.Clear();assert(!selected.front()->settled());f.controls.HARWorkChannelClosed();assert(selected.front()->settled());}
 {Fixture f;auto policy=Policy::Create(Archive(true),false);f.Bind("post",policy);f.controls.SessionAttached("child",[](bool ok,const auto&){assert(ok);});f.Pause(true,"child");auto selected=f.Capture(policy);assert(selected.size()==2);f.controls.SessionDetached("child");assert(selected[0]->settled()!=selected[1]->settled());f.Body();assert(f.pending.empty());f.receipt();assert(selected[0]->settled()&&selected[1]->settled());f.Close();}
 // Removal is exact-policy identity, preserving a newly installed same-ID
 // policy and a Page registration when an older target task eventually runs.
 {Fixture f;auto original=Policy::Create(Archive(),false),replacement=Policy::Create(Archive(),false);f.Bind("same",original);f.controls.UnbindReplayPolicy("same",original,[](bool ok,const auto&){assert(ok);},100);f.Bind("same",replacement);const auto before=f.methods.size();f.controls.UnbindReplayPolicy("same",original,[](bool ok,const auto&){assert(ok);},100);assert(f.methods.size()==before);f.Pause();assert(f.Capture(original).empty()&&f.Capture(replacement).size()==1);f.Ack();f.Close();}
 {Fixture f;using Route=suitecut::NativeContextRouteRegistration;auto old=Route::Create("same","**",0),fresh=Route::Create("same","**",0);assert(f.controls.BindContextRoute(old,[](bool ok,const auto&){assert(ok);}));f.controls.UnbindContextRoutePolicy(old,[](bool ok,const auto&){assert(ok);},100);assert(f.controls.BindContextRoute(fresh,[](bool ok,const auto&){assert(ok);}));const auto before=f.methods.size();f.controls.UnbindContextRoutePolicy(old,[](bool ok,const auto&){assert(ok);},100);assert(f.methods.size()==before);auto state=CefDictionaryValue::Create();f.controls.Diagnostics(state);assert(state->GetInt("networkRoutes")==1);f.Close();}
 {Fixture f;auto policy=Policy::Create(Archive(),false);f.Bind("expired",policy);f.now=100;const auto before=f.methods.size();bool failed=false;f.controls.UnbindReplayPolicy("expired",policy,[&](bool ok,const auto&reason){assert(!ok&&reason.find("original deadline")!=std::string::npos);failed=true;},100);assert(failed&&f.methods.size()==before);f.Close();}
 // A real HAR miss can hand the same paused request to an authored Page
 // handler without sending any terminal Fetch command. Logical Clear must
 // retain that policy; only the later native channel-close boundary retires it.
 {Fixture f;auto empty=std::make_shared<suitecut::NativeHARReplay>();std::string error;assert(empty->Load(Parse(R"({"log":{"entries":[]}})"),error));
   auto registration=Parse(R"({"action":"registerRoute","registrationId":"older-page","url":"**"})");f.controls.Command(1,registration,[](bool ok,const auto&,const auto&){assert(ok);});auto policy=Policy::Create(empty,true);assert(f.controls.BindReplayPolicy("newer-page-har","**",policy,[](bool ok,const auto&){assert(ok);},0,false));f.Pause();auto selected=f.Capture(policy);assert(selected.size()==1&&!selected.front()->settled()&&f.pending.empty());f.controls.Clear();assert(!selected.front()->settled());f.controls.HARWorkChannelClosed();assert(selected.front()->settled()&&selected.front()->failed());}
 assert(!suitecut::GlobalHARWorkLeases().physical());
 std::cout<<"PASS9 actual NetworkControls HAR held Fetch/body ACKs/rejected cleanup/channel identity/policy-safe removal/HAR fallback Clear retention; controlled native sender only\n";
}
