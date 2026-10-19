// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <charconv>
#include <deque>
#include "include/cef_api_hash.h"
#include "network_controls.h"
using Controls=suitecut::NetworkControls;using Dict=Controls::Dict;
struct Fixture{
 struct Call{std::string method;double deadline;Controls::Result done;};std::deque<Call>calls;std::vector<Dict>events;std::vector<std::string>errors;double now=1;bool accept_body=true;uint64_t body=0;std::vector<uint8_t>accepted;std::unique_ptr<Controls>controls;
 Fixture(){
  controls=std::make_unique<Controls>([this](const auto&,const auto&method,Dict,auto done){calls.push_back({method,0,std::move(done)});},[this](Dict event){events.push_back(event->Copy(false));},[this](const auto&error){errors.push_back(error);},[]{},[this]{return now;},
    [this](uint64_t owner,size_t offset,size_t total,std::span<const uint8_t>bytes,bool canceled){assert(!canceled);body=owner;if(!accept_body)return false;assert(total==2&&offset==accepted.size());accepted.insert(accepted.end(),bytes.begin(),bytes.end());return true;});
  controls->SetBoundedSender([this](const auto&,const auto&method,Dict,auto done,double deadline){calls.push_back({method,deadline,std::move(done)});});controls->SessionAttached("");Ack();
 }
 ~Fixture(){controls->Clear();}
 void Ack(){size_t count=0;while(!calls.empty()){assert(++count<128);auto call=std::move(calls.front());calls.pop_front();call.done(true,"");}}
 void Register(){auto params=CefDictionaryValue::Create();params->SetString("action","registerRoute");params->SetString("registrationId","actual-page-policy");params->SetString("url","https://native.test/wait");params->SetInt("times",0);bool registered=false;controls->Command(1,params,[&](bool ok,const auto&,const auto&){assert(ok);registered=true;});Ack();assert(registered);}
 Dict Pause(const std::string&path,const std::string&id){auto params=CefDictionaryValue::Create();params->SetString("requestId",id);params->SetString("networkId","network-"+id);params->SetString("frameId","actual-frame");params->SetString("resourceType","Fetch");auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test"+path);request->SetString("method","GET");request->SetDictionary("headers",CefDictionaryValue::Create());params->SetDictionary("request",request);const auto before=events.size();controls->Event("Fetch.requestPaused",params,"");return events.size()>before?events.back():nullptr;}
 Dict Removal(){auto params=CefDictionaryValue::Create();params->SetString("action","removeRoute");params->SetString("registrationId","");params->SetString("behavior","wait");params->SetString("mutationId","actual-page-removal");return params;}
 Dict Last(const char*kind){for(auto it=events.rbegin();it!=events.rend();++it)if((*it)->GetString("event")==kind)return *it;return nullptr;}
};
uint64_t Counter(const CefString&value){const auto text=value.ToString();uint64_t result=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);assert(parsed.ec==std::errc{}&&parsed.ptr==text.data()+text.size()&&result);return result;}
struct Lease{uint64_t scope,token;std::string invocation;};
Lease Issued(Dict event){assert(event&&event->GetString("event")=="route");auto route=event->GetDictionary("route");return {Counter(route->GetString("callbackScope")),Counter(route->GetString("callbackToken")),route->GetString("id")};}
int main(){assert(cef_api_hash(CEF_API_VERSION,0));auto&book=suitecut::GlobalRouteCallbackLeases();
 {
  Fixture f;f.Register();auto manager=std::make_shared<bool>(true);auto policy=suitecut::NativeContextRouteRegistration::Create("actual-context-policy","https://native.test/context",0);assert(policy&&policy->Claim(manager,7));bool bound=false;assert(f.controls->BindContextRoute(policy,[&](bool ok,const auto&){assert(ok);bound=true;}));f.Ack();assert(bound);
  const auto page=Issued(f.Pause("/wait","page-fetch")),context=Issued(f.Pause("/context","context-fetch"));assert(book.Start(page.token,page.scope,page.invocation)&&book.Start(context.token,context.scope,context.invocation));int done=0;
  f.controls->Command(2,f.Removal(),[&](bool ok,const auto&,const auto&){assert(ok);++done;},100,77,7);
  auto admitted=f.Last("routemutation");assert(admitted&&admitted->GetString("phase")=="admitted"&&Counter(admitted->GetString("callbackScope"))==page.scope&&admitted->GetList("callbackTokens")->GetSize()==1&&Counter(admitted->GetList("callbackTokens")->GetString(0))==page.token);assert(admitted->GetList("registrationIds")->GetSize()==1&&admitted->GetList("registrationIds")->GetString(0)=="actual-page-policy");assert(!f.calls.empty());for(const auto&call:f.calls)assert(call.deadline==100);f.Ack();assert(done==0&&f.Last("routemutation")->GetString("phase")=="applied");
  assert(book.Finish(page.token,page.scope,page.invocation));f.controls->Tick(f.now);assert(done==1&&book.running()==1&&policy->available());assert(f.Pause("/context","future-context-fetch"));assert(book.Finish(context.token,context.scope,context.invocation));assert(f.errors.empty());f.controls->Clear();
 }
 {
  Fixture f;f.Register();f.accept_body=false;
  auto event=CefDictionaryValue::Create();event->SetString("requestId","binary-unrelated");event->SetString("type","Fetch");auto request=CefDictionaryValue::Create();request->SetString("url","https://native.test/unrelated");request->SetString("method","POST");request->SetBool("hasPostData",true);request->SetDictionary("headers",CefDictionaryValue::Create());auto entries=CefListValue::Create();auto entry=CefDictionaryValue::Create();entry->SetString("bytes","AP8=");entries->SetDictionary(0,entry);request->SetList("postDataEntries",entries);event->SetDictionary("request",request);f.controls->Event("Network.requestWillBeSent",event,"");f.controls->Tick(f.now);assert(f.body&&f.events.empty());assert(!f.Pause("/wait","held-page-fetch"));
  int done=0;f.controls->Command(2,f.Removal(),[&](bool ok,const auto&,const auto&){assert(ok);++done;},100,1,7);assert(f.events.size()==1&&f.events[0]->GetString("event")=="routemutation"&&f.events[0]->GetString("phase")=="admitted");f.Ack();assert(f.events.size()==2&&f.events[1]->GetString("phase")=="applied"&&done==0);
  f.accept_body=true;f.controls->Tick(f.now);assert(f.accepted==std::vector<uint8_t>({0,255}));assert(f.controls->BodyAcknowledged(std::to_string(f.body),"received"));auto route=f.Last("route");assert(route);const auto lease=Issued(route);assert(book.Start(lease.token,lease.scope,lease.invocation));assert(book.Finish(lease.token,lease.scope,lease.invocation));f.controls->Tick(f.now);assert(done==1);assert(f.controls->BodyAcknowledged(std::to_string(f.body),"taken"));assert(f.errors.empty());
 }
 {
  Fixture f;f.Register();int rejected=0;f.controls->Command(2,f.Removal(),[&](bool ok,const auto&,const auto&){assert(!ok);++rejected;});assert(rejected==1&&f.calls.empty());const auto lease=Issued(f.Pause("/wait","must-still-match"));assert(book.Start(lease.token,lease.scope,lease.invocation));
  int failed=0;f.controls->Command(3,f.Removal(),[&](bool ok,const auto&,const auto&){assert(!ok);++failed;},100,1,7);assert(!f.calls.empty());f.controls->Clear();auto diagnostics=CefDictionaryValue::Create();f.controls->Diagnostics(diagnostics);assert(failed==1&&diagnostics->GetInt("networkRouteRemovalObservers")==0&&diagnostics->GetInt("networkRouteRemovalApplyCallbacks")==1&&book.running()==1);f.Ack();f.controls->Diagnostics(diagnostics);assert(failed==1&&diagnostics->GetInt("networkRouteRemovalApplyCallbacks")==0);assert(book.Finish(lease.token,lease.scope,lease.invocation));assert(f.errors.empty());
 }
 assert(book.size()==0&&suitecut::NativePageRouteRemovals::retained_tasks()==0&&suitecut::GlobalNetworkBodyQuota().owners==0);
}
