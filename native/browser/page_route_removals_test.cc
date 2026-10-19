// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <iostream>
#include "page_route_removals.h"
#include "route_callback_leases.h"
using Control=suitecut::NativePageRouteRemovals;
struct Sender{
  std::deque<Control::Reply>pending;
  Control::Apply send(){return [this](double,auto reply){pending.push_back(std::move(reply));};}
  void Ack(bool ok=true){assert(!pending.empty());auto callback=std::move(pending.front());pending.pop_front();callback(ok,ok?"":"actual control ACK rejected");}
};
int main(){
 double now=1;
 {
  suitecut::NativeRouteCallbackLeases book;const auto scope=*book.Scope();auto policy=suitecut::NativeContextRouteRegistration::Create("actual-context","**/*",0);assert(policy);
  const auto original=*book.Issue(scope,"page-original"),unrelated=*book.Issue(scope,"context-original",policy);assert(book.Start(original,scope,"page-original")&&book.Start(unrelated,scope,"context-original"));
  std::vector<bool>events;Sender sender;bool deactivated=false;int replies=0;
  Control controls(scope,[&]{return now;},[&](bool ack,const auto&mutation,const auto&ids,const auto&selected){assert(deactivated&&mutation=="actual-mutation"&&ids==std::vector<std::string>{"actual-page"}&&selected.scope==scope&&selected.tokens==std::vector<uint64_t>{original});events.push_back(ack);});
  assert(controls.Remove(1,7,10,book.CapturePage(scope),{"actual-page"},"actual-mutation",true,100,[&]{deactivated=true;},sender.send(),[&](bool ok,const auto&){assert(ok);++replies;}));assert(events==std::vector<bool>{false}&&controls.retained_applies()==1&&controls.pending()==1);
  const auto future=*book.Issue(scope,"future-page");assert(book.Start(future,scope,"future-page"));sender.Ack();assert(events==std::vector<bool>({false,true})&&replies==0&&controls.retained_applies()==0);
  assert(book.Finish(original,scope,"page-original"));controls.Tick();assert(replies==1&&controls.pending()==0&&book.running()==2);assert(book.Finish(unrelated,scope,"context-original")&&book.Finish(future,scope,"future-page"));
 }
 {
  suitecut::NativeRouteCallbackLeases book;const auto scope=*book.Scope(),token=*book.Issue(scope,"held");assert(book.Start(token,scope,"held"));Sender sender;int reply=0;
  Control controls(scope,[&]{return now;},[](bool,const auto&,const auto&,const auto&){});
  assert(controls.Remove(1,7,1,book.CapturePage(scope),{},"old",true,100,[]{},sender.send(),[](bool,const auto&){assert(false);}));controls.Cancel(1);assert(controls.pending()==0&&controls.retained_applies()==1);
  assert(controls.Remove(1,7,2,book.CapturePage(scope),{},"new",true,100,[]{},sender.send(),[&](bool ok,const auto&){assert(ok);++reply;}));sender.Ack();assert(reply==0&&controls.pending()==1&&controls.retained_applies()==1);sender.Ack();assert(reply==0);assert(book.Finish(token,scope,"held"));controls.Tick();assert(reply==1&&controls.pending()==0);
 }
 {
  suitecut::NativeRouteCallbackLeases book;const auto scope=*book.Scope(),token=*book.Issue(scope,"deadline-held");assert(book.Start(token,scope,"deadline-held"));Sender sender;int reply=0,ack=0;
  Control controls(scope,[&]{return now;},[&](bool applied,const auto&,const auto&,const auto&){ack+=applied;});
  assert(controls.Remove(1,7,1,book.CapturePage(scope),{},"deadline",true,10,[]{},sender.send(),[&](bool ok,const auto&error){assert(!ok&&error.find("deadline")!=std::string::npos);++reply;}));now=10;controls.Tick();assert(reply==1&&controls.pending()==0&&controls.retained_applies()==1&&book.running()==1);sender.Ack();assert(ack==0&&controls.retained_applies()==0);assert(book.Finish(token,scope,"deadline-held"));now=1;
 }
 {
  suitecut::NativeRouteCallbackLeases book;const auto scope=*book.Scope();int replies=0,sent=0;std::unique_ptr<Control>controls;
  controls=std::make_unique<Control>(scope,[&]{return now;},[&](bool ack,const auto&,const auto&,const auto&){assert(!ack);controls.reset();});
  assert(controls->Remove(1,7,1,book.CapturePage(scope),{},"reentrant-before-send",true,100,[]{},[&](double,auto){++sent;},[&](bool ok,const auto&){assert(!ok);++replies;}));assert(!controls&&sent==0&&replies==1&&Control::retained_tasks()==0);
 }
 {
  suitecut::NativeRouteCallbackLeases book;const auto scope=*book.Scope();Sender sender;int replies=0;Control controls(scope,[&]{return now;},[](bool,const auto&,const auto&,const auto&){});
  assert(controls.Remove(1,7,1,book.CapturePage(scope),{},"disposed-after-send",false,100,[]{},sender.send(),[&](bool ok,const auto&){assert(!ok);++replies;}));controls.Clear();assert(replies==1&&controls.pending()==0&&controls.retained_applies()==1&&Control::retained_tasks()==1);sender.Ack();assert(replies==1&&controls.retained_applies()==0&&Control::retained_tasks()==0);
 }
 {
  suitecut::NativeRouteCallbackLeases book;const auto scope=*book.Scope();Sender sender;int replies=0,sends=0;Control controls(scope,[&]{return now;},[](bool,const auto&,const auto&,const auto&){});
  auto apply=[&](double,auto){++sends;};auto done=[&](bool,const auto&){++replies;};auto wrong=book.CapturePage(scope);++wrong.scope;
  assert(!controls.Remove(1,7,1,std::move(wrong),{},"wrong-owner",true,100,[]{},apply,done));assert(!controls.Remove(1,0,1,book.CapturePage(scope),{},"no-context",true,100,[]{},apply,done));
  assert(!controls.Remove(1,7,1,book.CapturePage(scope),{"reused","reused"},"duplicate-policy",true,100,[]{},apply,done));
  std::vector<std::string>oversized;for(int i=0;i<128;++i)oversized.push_back(std::to_string(i)+std::string(125,'\1'));assert(!controls.Remove(1,7,1,book.CapturePage(scope),std::move(oversized),"real-wire-bound",true,100,[]{},apply,done));assert(replies==0&&sends==0&&controls.pending()==0);
 }
 {
  suitecut::NativeRouteCallbackLeases book;std::vector<std::unique_ptr<Control>>owners;Sender sender;int replies=0,mutations=0;
  for(int owner=0;owner<2;++owner){const auto scope=*book.Scope();owners.push_back(std::make_unique<Control>(scope,[&]{return now;},[](bool,const auto&,const auto&,const auto&){}));for(int op=1;op<=64;++op)assert(owners.back()->Remove(op,7,1,book.CapturePage(scope),{},"bounded",false,0,[&]{++mutations;},sender.send(),[&](bool ok,const auto&){assert(!ok);++replies;}));}
  const auto scope=*book.Scope();owners.push_back(std::make_unique<Control>(scope,[&]{return now;},[](bool,const auto&,const auto&,const auto&){}));assert(!owners.back()->Remove(1,7,1,book.CapturePage(scope),{},"overflow",false,0,[&]{++mutations;},sender.send(),[](bool,const auto&){assert(false);}));assert(mutations==128&&Control::retained_tasks()==128&&Control::retained_payload()<=4194304);
  for(auto&owner:owners)owner->Clear();
  assert(replies==128&&Control::retained_tasks()==128);while(!sender.pending.empty())sender.Ack();assert(Control::retained_tasks()==0&&Control::retained_payload()==0);
 }
 {
  suitecut::NativeRouteCallbackLeases book;const auto scope=*book.Scope();Sender sender;int replies=0,applied=0;Control controls(scope,[&]{return now;},[&](bool ack,const auto&,const auto&,const auto&){applied+=ack;});
  assert(controls.Remove(1,7,1,book.CapturePage(scope),{},"ignore-deadline",false,10,[]{},sender.send(),[&](bool ok,const auto&error){assert(!ok&&error.find("deadline")!=std::string::npos);++replies;}));now=10;controls.Tick();assert(replies==1&&controls.retained_applies()==1);sender.Ack(false);assert(replies==1&&applied==0&&controls.retained_applies()==0);now=1;
  assert(controls.Remove(2,7,2,book.CapturePage(scope),{},"positive-ack-too-late",true,10,[]{},sender.send(),[&](bool ok,const auto&error){assert(!ok&&error.find("deadline")!=std::string::npos);++replies;}));now=10;sender.Ack();assert(replies==2&&applied==0&&controls.pending()==0);now=1;
 }
 assert(Control::retained_tasks()==0&&Control::retained_payload()==0);
 std::cout<<"Page removal observers: 8 owner/ACK/snapshot/cancel/deadline/reentrancy/capacity groups passed\n";
}
