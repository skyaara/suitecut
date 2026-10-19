// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <vector>
#include "include/cef_api_hash.h"
#include "context_har_routes.h"
using Policy=suitecut::NativeContextHARRoutes;
int main(){
 assert(cef_api_hash(CEF_API_VERSION,0));std::vector<Policy::Result>acks;std::vector<std::string>calls;std::vector<double>deadlines;int failures=0;
 Policy policy([&](const std::string&,const std::string&){++failures;});assert(policy.Seed(1)&&policy.Seed(2));
 const auto apply=[&](const std::string&name){return [&,name](Policy::Mutation mutation,const Policy::Route&route,double deadline,Policy::Result done){calls.push_back(name+":"+(mutation==Policy::Mutation::Install?"bind:":"drop:")+route.id);deadlines.push_back(deadline);acks.push_back(std::move(done));};};
 bool root_ready=false;assert(policy.Attach(1,"page:1",apply("page"),[&](bool ok,const std::string&){assert(ok);root_ready=true;}));assert(root_ready);
 auto replay=std::make_shared<suitecut::NativeHARReplay>();auto archive=CefParseJSON(R"({"log":{"entries":[]}})",JSON_PARSER_RFC)->GetDictionary();std::string error;assert(replay->Load(archive,error));
 bool installed=false;assert(policy.Install(1,1,{"route1","**",replay,false},50,[&](bool ok,const std::string&){assert(ok);installed=true;}));assert(!installed&&policy.physical()==1&&deadlines.back()==50);
 bool broker_ready=false;assert(policy.Attach(1,"actual-worker-broker:1",apply("broker"),[&](bool ok,const std::string&){assert(ok);broker_ready=true;}));assert(!broker_ready&&policy.physical()==2);
 acks[0](true,"");assert(!installed);acks[1](true,"");assert(installed&&broker_ready&&policy.physical()==0);
 // Another genuine context never inherits context1's route or callbacks.
 bool isolated=false;assert(policy.Attach(2,"page:2",apply("isolated"),[&](bool ok,const std::string&){assert(ok);isolated=true;}));assert(isolated&&calls.size()==2);
 // Original deadline survives future-target admission and delayed ACKs.
 bool timed_out=false;assert(policy.Install(2,1,{"route2","**",replay,true},60,[&](bool ok,const std::string&reason){assert(!ok&&reason.find("original deadline")!=std::string::npos);timed_out=true;}));assert(policy.physical()==2);const auto before=acks.size();policy.Tick(60);assert(timed_out&&policy.pending()==0&&policy.physical()==4&&acks.size()==before+2);
 acks[before-2](true,"");acks[before-1](true,"");assert(policy.physical()==2);acks[before](true,"");acks[before+1](true,"");assert(policy.physical()==0);
 // A canceled install queued behind an older bootstrap bind never dispatches.
 bool late_ready=false;const auto old=acks.size();assert(policy.Attach(1,"page:future",apply("future"),[&](bool ok,const std::string&){assert(ok);late_ready=true;}));assert(!late_ready&&calls.back()=="future:bind:route1");
 bool canceled_reply=false;assert(policy.Install(3,1,{"route3","**",replay,false},80,[&](bool,const std::string&){canceled_reply=true;}));policy.Cancel(3);assert(!canceled_reply&&policy.physical()>=1);acks[old](true,"");assert(late_ready);for(const auto&call:calls)assert(call!="future:bind:route3");
 // Clear retires logical owner/ready callbacks but physical receipts remain.
 const auto physical=policy.physical();policy.Clear();assert(policy.pending()==0&&policy.targets()==0&&policy.physical()==physical);for(size_t i=old+1;i<acks.size();++i)acks[i](true,"");assert(policy.physical()==0&&failures==0);
 // An actual ACK crossing the original deadline fails before the next Tick,
 // retires logical ownership, and keeps physical rollback until its own ACK.
 double clock=1;std::vector<Policy::Result>late_acks;std::vector<Policy::Mutation>late_calls;bool late_timeout=false;
 Policy deadline_policy([](const std::string&,const std::string&){assert(false);},[&]{return clock;});assert(deadline_policy.Seed(1));
 assert(deadline_policy.Attach(1,"page:actual-late",[&](Policy::Mutation mutation,const Policy::Route&,double,Policy::Result reply){late_calls.push_back(mutation);late_acks.push_back(std::move(reply));},[](bool ok,const std::string&){assert(ok);}));
 assert(deadline_policy.Install(4,1,{"late-route","**",replay,false},5,[&](bool ok,const std::string&reason){assert(!ok&&reason.find("original deadline")!=std::string::npos);late_timeout=true;}));
 clock=5;late_acks[0](true,"");assert(late_timeout&&deadline_policy.pending()==0&&deadline_policy.physical()==1&&late_calls.size()==2&&late_calls[1]==Policy::Mutation::Remove);late_acks[1](true,"");assert(deadline_policy.physical()==0);

 // Actual target detachment fails readiness once while its dispatched archive
 // policy remains physically owned until the genuine installer callback.
 {
  Policy detached([](const auto&,const auto&){assert(false);});assert(detached.Seed(1));
  auto pin=suitecut::NativeHARRouteRegistration::Create(replay,false);std::weak_ptr<const suitecut::NativeHARRouteRegistration>weak=pin;
  assert(detached.Install(20,1,{"detach-route","**",replay,false,{},pin},0,[](bool ok,const auto&){assert(ok);}));pin.reset();
  Policy::Result actual;int ready=0;assert(detached.Attach(1,"closing-owner",[&](auto,const auto&,double,auto done){actual=std::move(done);},[&](bool ok,const auto&){assert(!ok);++ready;}));
  assert(detached.physical()==1);detached.Detach("closing-owner");assert(ready==1&&detached.physical()==1);detached.Closed(1);detached.Clear();assert(!weak.expired());actual(true,"");assert(ready==1&&weak.expired());
 }
 // A canceled operation ID may be reused. Its old real install ACK cannot
 // complete the replacement batch, including an identical registration ID.
 {
  Policy reused([](const auto&,const auto&){assert(false);});assert(reused.Seed(1));std::vector<Policy::Result>actual;int old_reply=0,new_reply=0;
  assert(reused.Attach(1,"same-owner",[&](auto,const auto&,double,auto done){actual.push_back(std::move(done));},[](bool ok,const auto&){assert(ok);}));
  assert(reused.Install(21,1,{"same-route","**",replay,false},0,[&](bool,const auto&){++old_reply;}));assert(actual.size()==1);reused.Cancel(21);assert(actual.size()==2);
  assert(reused.Install(21,1,{"same-route","**",replay,true},0,[&](bool ok,const auto&){assert(ok);++new_reply;}));assert(actual.size()==2);
  actual[0](true,"");assert(old_reply==0&&new_reply==0&&actual.size()==3&&reused.pending()==1);actual[2](true,"");assert(new_reply==1&&old_reply==0);actual[1](true,"");assert(reused.physical()==0);reused.Clear();
 }

 // Real immutable CEF-decoded archives reserve before mutation. Sharing one
 // object does not duplicate the global allocation but still counts per route
 // for each future Source's actual 64 MiB replay policy limit.
 const auto make_large=[](){auto archive=CefDictionaryValue::Create();auto log=CefDictionaryValue::Create();auto entries=CefListValue::Create();const std::string body(2097152,'x');for(size_t i=0;i<12;++i){auto entry=CefDictionaryValue::Create();auto request=CefDictionaryValue::Create();request->SetString("method","GET");request->SetString("url","https://native.test/actual-"+std::to_string(i));request->SetList("headers",CefListValue::Create());entry->SetDictionary("request",request);auto response=CefDictionaryValue::Create();response->SetInt("status",200);response->SetList("headers",CefListValue::Create());auto content=CefDictionaryValue::Create();content->SetString("text",body);response->SetDictionary("content",content);entry->SetDictionary("response",response);entries->SetDictionary(i,entry);}log->SetList("entries",entries);archive->SetDictionary("log",log);auto result=std::make_shared<suitecut::NativeHARReplay>();std::string error;assert(result->Load(archive,error)&&result->bytes()>24*1048576);return result;};
 auto first=make_large(),second=make_large(),third=make_large();Policy capacity_policy([](const std::string&,const std::string&){assert(false);});assert(capacity_policy.Seed(1)&&capacity_policy.Seed(2)&&capacity_policy.Seed(3));int admitted=0;const auto admit=[&](bool ok,const std::string&){assert(ok);++admitted;};
 assert(capacity_policy.Install(10,1,{"same-object-a","**",first,false},0,admit));assert(capacity_policy.Install(11,1,{"same-object-b","**",first,false},0,admit));assert(capacity_policy.retained_bytes()==first->bytes());const auto first_bytes=capacity_policy.retained_bytes();
 assert(!capacity_policy.Install(12,1,{"local-overflow","**",first,false},0,admit));assert(capacity_policy.pending()==0&&capacity_policy.retained_bytes()==first_bytes&&admitted==2);
 assert(capacity_policy.Install(13,2,{"another-context","**",second,false},0,admit));const auto both=capacity_policy.retained_bytes();assert(!capacity_policy.Install(14,3,{"global-overflow","**",third,false},0,admit));assert(capacity_policy.pending()==0&&capacity_policy.retained_bytes()==both&&admitted==3);
 capacity_policy.Closed(1);assert(capacity_policy.retained_bytes()==second->bytes());assert(capacity_policy.Install(15,3,{"after-real-policy-release","**",third,false},0,admit));capacity_policy.Closed(2);capacity_policy.Closed(3);assert(capacity_policy.retained_bytes()==0&&admitted==4);capacity_policy.Clear();

 // The physical-policy ledger remains charged through retained immutable
 // owners after logical context policies are gone; distinct images cannot
 // bypass its 64 MiB bound, while references to one real image share charge.
 {
  using ArchivePolicy=suitecut::NativeHARRouteRegistration;
  assert(ArchivePolicy::retained_bytes()==0);auto a=ArchivePolicy::Create(first,false),b=ArchivePolicy::Create(second,true);assert(a&&b);
  const auto bytes=ArchivePolicy::retained_bytes();auto shared=ArchivePolicy::Create(first,true);assert(shared&&ArchivePolicy::retained_bytes()==bytes&&ArchivePolicy::retained_archives()==2);
  assert(!ArchivePolicy::Create(third,false)&&ArchivePolicy::retained_bytes()==bytes);a.reset();assert(!ArchivePolicy::Create(third,false));shared.reset();auto admitted=ArchivePolicy::Create(third,false);assert(admitted);b.reset();admitted.reset();assert(ArchivePolicy::retained_bytes()==0&&ArchivePolicy::retained_policies()==0);
  std::vector<ArchivePolicy::Policy>descriptors;for(size_t i=0;i<4096;++i){auto policy=ArchivePolicy::Create(replay,false);assert(policy);descriptors.push_back(std::move(policy));}
  assert(ArchivePolicy::retained_policies()==4096&&ArchivePolicy::retained_archives()==1&&!ArchivePolicy::Create(replay,false));descriptors.clear();assert(ArchivePolicy::retained_policies()==0&&ArchivePolicy::retained_archives()==0);
 }

}
