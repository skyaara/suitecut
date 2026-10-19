// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include "route_callback_leases.h"
#include "route_callback_waits.h"
using Leases=suitecut::NativeRouteCallbackLeases;
using Waits=suitecut::NativeRouteCallbackWaits;
using Policy=suitecut::NativeContextRouteRegistration;
int main(){
  double now=1;auto manager=std::make_shared<bool>(true);
  auto policy=Policy::Create("context-one","**/*",0),other=Policy::Create("context-two","**/*",0);assert(policy&&other&&policy->Claim(manager,1)&&other->Claim(manager,2));
  auto selected=[&](const Policy&entry){return entry.ClaimedBy(manager,1);};
  {
    Leases leases;Waits waits([&]{return now;});const auto source=*leases.Scope(),broker=*leases.Scope();
    const auto running=*leases.Issue(source,"running",policy),queued=*leases.Issue(broker,"queued",policy),unrelated=*leases.Issue(source,"unrelated",other);
    assert(leases.Start(running,source,"running"));auto snapshot=leases.Capture(selected);assert(snapshot.size()==2);int delivered=0;
    assert(waits.Wait(1,1,std::move(snapshot),100,[&](bool ok,const auto&){assert(ok);++delivered;}));
    leases.Close(broker);waits.Tick();assert(delivered==0&&leases.running()==1&&leases.size()==2);assert(!leases.Start(queued,broker,"queued"));
    assert(leases.Finish(running,source,"running"));waits.Tick();assert(delivered==1&&waits.pending()==0&&leases.size()==1);
    assert(leases.Finish(unrelated,source,"unrelated"));
  }
  {
    Leases leases;Waits waits([&]{return now;});const auto scope=*leases.Scope();const auto token=*leases.Issue(scope,"physical",policy);assert(leases.Start(token,scope,"physical"));
    assert(waits.Wait(2,1,leases.Capture(selected),0,[](bool,const auto&){assert(false);}));waits.Cancel(2);assert(waits.pending()==0&&leases.running()==1);
    int result=0;assert(waits.Wait(2,1,leases.Capture(selected),10,[&](bool ok,const auto&error){assert(!ok&&error.find("deadline")!=std::string::npos);++result;}));now=10;waits.Tick();assert(result==1&&leases.running()==1);
    assert(leases.Finish(token,scope,"physical"));
    // A real callback receipt arriving at the original close deadline cannot
    // turn a late logical reply into success just because Tick has not run.
    assert(waits.Wait(3,1,leases.Capture(selected),10,[&](bool ok,const auto&){assert(!ok);++result;}));assert(result==2);
  }
  {
    now=1;Leases leases;Waits waits([&]{return now;});const auto scope=*leases.Scope();const auto original=*leases.Issue(scope,"original",policy);
    auto captured=leases.Capture(selected);const auto future=*leases.Issue(scope,"future",policy);int result=0;
    assert(waits.Wait(4,1,std::move(captured),0,[&](bool ok,const auto&){assert(ok);++result;}));assert(leases.Finish(original,scope,"original"));
    // Completion proof survives eviction of the book's small idempotence cache.
    for(int i=0;i<300;++i){const auto token=*leases.Issue(scope,"later",other);assert(leases.Finish(token,scope,"later"));}
    waits.Tick();assert(result==1&&leases.size()==1);assert(leases.Finish(future,scope,"future"));
  }
  {
    Leases leases;Waits waits([&]{return now;});const auto scope=*leases.Scope();const auto token=*leases.Issue(scope,"not-completed",policy);assert(leases.Start(token,scope,"not-completed"));int result=0;
    assert(waits.Wait(5,1,leases.Capture(selected),0,[&](bool ok,const auto&error){assert(!ok&&error.find("not proven")!=std::string::npos);++result;}));
    leases.Clear();waits.Tick();assert(result==1); // Logical book disposal is not a physical receipt.
  }
  {
    Leases leases;const auto scope=*leases.Scope();const auto token=*leases.Issue(scope,"context-close",policy);assert(leases.Start(token,scope,"context-close"));int result=0;auto waits=std::make_unique<Waits>([&]{return now;});
    assert(waits->Wait(6,1,leases.Capture(selected),0,[&](bool ok,const auto&){assert(!ok);++result;}));waits->Closed(1);assert(result==1&&leases.running()==1);assert(leases.Finish(token,scope,"context-close"));
    // Reentrant destruction after first delivery does not consume a newly
    // reused operation or strand any already extracted callback delivery.
    const auto one=*leases.Issue(scope,"one",policy),two=*leases.Issue(scope,"two",policy);
    assert(waits->Wait(7,1,leases.Capture(selected),0,[&](bool ok,const auto&){assert(ok);++result;waits.reset();}));
    assert(leases.Finish(one,scope,"one")&&leases.Finish(two,scope,"two"));waits->Tick();assert(!waits&&result==2);
  }
  {
    Leases leases;Waits waits([&]{return now;});const auto scope=*leases.Scope(),token=*leases.Issue(scope,"apply-barrier",policy);int delivered=0;
    assert(waits.Reserve(8,1,10,leases.Capture(selected),0,[](bool,const auto&){assert(false);}));waits.Cancel(8);
    assert(waits.Reserve(8,1,11,leases.Capture(selected),0,[&](bool ok,const auto&){assert(ok);++delivered;}));
    assert(leases.Finish(token,scope,"apply-barrier"));waits.Tick();assert(delivered==0&&waits.pending()==1);
    assert(!waits.Applied(8,10));waits.Failed(8,10,"old rejected ACK");assert(waits.pending()==1&&delivered==0);
    assert(waits.Applied(8,11));assert(delivered==1&&waits.pending()==0);
  }
  {
    Waits waits([&]{return now;});suitecut::NativeRouteCallbackSnapshot excessive;excessive.reserve(8193);
    assert(!waits.Reserve(9,1,1,std::move(excessive),0,[](bool,const auto&){assert(false);}));assert(waits.bytes()==0&&waits.pending()==0);
    for(int operation=1;operation<=64;++operation)assert(waits.Reserve(operation,1,1,{},0,[](bool ok,const auto&){assert(ok);}));
    assert(!waits.Reserve(65,1,1,{},0,[](bool,const auto&){assert(false);}));
    for(int operation=1;operation<=64;++operation)assert(waits.Applied(operation,1));
    assert(waits.pending()==0&&waits.bytes()==0);
  }
  std::cout<<"Native route callback waits: 7 exact-receipt/deadline/lifetime/capacity groups passed\n";
}
