// SPDX-License-Identifier: MIT
#include "context_authentication_controls.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <deque>
#include <iostream>
#include <thread>
#include <type_traits>
using Controls=suitecut::NativeContextAuthenticationControls;
using Credentials=suitecut::NativeContextCredentials;
static_assert(!std::is_default_constructible_v<Controls>);
int main(){
  double clock=1;Controls controls([&]{return clock;});Credentials initial{{"old","password","http://actual.test",false}},latest{{"new","secret","http://actual.test",true}};
  assert(controls.Seed(7,initial));auto io=controls.Get(7);assert(io&&io->Get()->at(0).username=="old");
  std::deque<Controls::Result> physical;int ready=0,replies=0,delivered=0;
  auto apply=[&](Controls::Value value,Controls::Result done){assert(value);++delivered;physical.push_back(std::move(done));};
  assert(controls.Attach(7,"page:1",apply,[&](bool ok,const std::string&){assert(ok);++ready;}));assert(physical.size()==1&&controls.physical()==1);
  Controls::Result preparation;assert(controls.Set(1,7,latest,100,[&](auto done){preparation=std::move(done);},[&](bool ok,const std::string&){assert(ok);++replies;}));
  assert(controls.physical()==2&&controls.pending()==1&&io->Get()->at(0).username=="new");
  assert(controls.Attach(7,"broker:7",apply,[&](bool ok,const std::string&){assert(ok);++ready;}));assert(physical.size()==1);
  auto first=std::move(physical.front());physical.pop_front();first(true,"");assert(ready==0&&physical.empty()&&controls.physical()==1);
  preparation(true,"");assert(physical.size()==2&&controls.physical()==2&&replies==0);
  auto one=std::move(physical.front());physical.pop_front();one(true,"");assert(ready==1&&replies==0);
  auto two=std::move(physical.front());physical.pop_front();two(true,"");assert(ready==2&&replies==1&&controls.pending()==0&&controls.physical()==0);
  // Original native timeout returns once while real browser controls remain
  // owned. No subsequent update can overtake the unacknowledged configuration.
  assert(controls.Set(2,7,initial,200,[&](auto done){preparation=std::move(done);},[&](bool ok,const std::string&error){assert(!ok&&error.find("deadline")!=std::string::npos);++replies;}));preparation(true,"");assert(physical.size()==2);
  controls.Tick(200);assert(replies==2&&controls.logical()==0&&controls.pending()==1&&controls.physical()==2);
  assert(!controls.Set(3,7,latest,300,[](auto done){done(true,"");},[](bool,const auto&){}));
  controls.Cancel(2);while(!physical.empty()){auto done=std::move(physical.front());physical.pop_front();done(true,"");}assert(replies==2&&controls.pending()==0&&controls.physical()==0);
  // Exact target close retires only that target. A stale old callback cannot
  // acknowledge a replacement target using the same public key.
  assert(controls.Set(4,7,latest,0,[](auto done){done(true,"");},[&](bool ok,const auto&){assert(ok);++replies;}));assert(physical.size()==2);auto stale=std::move(physical.front());physical.pop_front();controls.Detach("broker:7");assert(controls.Attach(7,"broker:7",apply,[&](bool ok,const auto&){assert(ok);++ready;}));stale(true,"");assert(controls.pending()==1);while(!physical.empty()){auto done=std::move(physical.front());physical.pop_front();done(true,"");}assert(replies==3&&controls.physical()==0);
  // Immutable readers are safe across real native IO/UI concurrency.
  std::thread reader([&]{for(int i=0;i<100000;++i){auto value=io->Get();assert(value&&value->size()==1);assert(value->at(0).username=="old"||value->at(0).username=="new");}});
  for(int round=0;round<10000;++round){assert(controls.Set(10,7,round%2?initial:latest,0,[](auto done){done(true,"");},[](bool ok,const auto&){assert(ok);}));while(!physical.empty()){auto done=std::move(physical.front());physical.pop_front();done(true,"");}}
  reader.join();assert(controls.physical()==0&&controls.pending()==0);
  const int previous=delivered;assert(!controls.Set(11,7,initial,std::numeric_limits<double>::quiet_NaN(),[](auto done){done(true,"");},[](bool,const auto&){}));assert(delivered==previous);
  controls.Closing(7);assert(!io->Get());controls.Closed(7);assert(controls.targets()==0&&!controls.Get(7));
  // A pending real completion remains owned after logical close. The close
  // guard cannot report readiness until the original completion is received.
  assert(controls.Seed(8,initial));assert(controls.Set(50,8,latest,0,[&](auto done){preparation=std::move(done);},[](bool ok,const std::string&){assert(!ok);}));
  assert(!controls.Ready(8)&&controls.physical()==1);controls.Closing(8);assert(!controls.Ready(8)&&controls.physical()==1);preparation(true,"");assert(controls.Ready(8)&&controls.physical()==0);controls.Closed(8);
  // Manager final removal is already inside its context-closed hook. Native
  // target retirement here must not recursively request the same removal.
  Controls removal([&]{return clock;});assert(removal.Seed(9,initial));int removal_notifications=0;
  removal.SetSettled([&](int){++removal_notifications;removal.Closed(9);});
  assert(removal.Attach(9,"page:9",[](auto,auto){},[](bool ok,const auto&){assert(!ok);}));
  removal.Closed(9);assert(removal_notifications==0&&removal.targets()==0&&removal.physical()==0);
  // Actual ACKs can arrive after the original deadline before any timer runs.
  // The accepted configuration is preserved and each physical callback remains
  // owned until its genuine completion, while the caller cannot succeed late.
  {
    double now=1;Controls receipts([&]{return now;});assert(receipts.Seed(1,initial));
    std::deque<Controls::Result> callbacks;auto apply=[&](auto,auto done){callbacks.push_back(std::move(done));};
    assert(receipts.Attach(1,"page:a",apply,[](bool ok,const auto&){assert(ok);}));
    assert(receipts.Attach(1,"page:b",apply,[](bool ok,const auto&){assert(ok);}));
    while(!callbacks.empty()){auto done=std::move(callbacks.front());callbacks.pop_front();done(true,"");}
    int timed_out=0,succeeded=0;
    assert(receipts.Set(100,1,latest,10,[](auto done){done(true,"");},[&](bool ok,const std::string&error){assert(!ok&&error==Controls::DeadlineError());++timed_out;}));
    assert(receipts.physical()==2&&receipts.pending()==1);now=9;
    auto first=std::move(callbacks.front());callbacks.pop_front();first(true,"");assert(receipts.physical()==1&&timed_out==0);
    now=10;auto last=std::move(callbacks.front());callbacks.pop_front();last(true,"");
    assert(timed_out==1&&receipts.pending()==0&&receipts.physical()==0&&receipts.Ready(1));assert(receipts.LastReceipt().owner==100&&receipts.LastReceipt().deadline==10&&receipts.LastReceipt().at==10);
    assert(receipts.Get(1)->Get()->at(0).username=="new");
    now=11;assert(receipts.Set(101,1,initial,20,[](auto done){done(true,"");},[&](bool ok,const std::string&error){assert(ok&&error.empty());++succeeded;}));
    now=19;while(!callbacks.empty()){auto done=std::move(callbacks.front());callbacks.pop_front();done(true,"");}assert(succeeded==1&&timed_out==1);assert(receipts.LastReceipt().owner==101&&receipts.LastReceipt().deadline==20&&receipts.LastReceipt().at==19);
    assert(receipts.Set(102,1,latest,30,[](auto done){done(true,"");},[](bool,const auto&){assert(false);}));receipts.Cancel(102);now=31;
    assert(receipts.physical()==2);while(!callbacks.empty()){auto done=std::move(callbacks.front());callbacks.pop_front();done(true,"");}assert(receipts.physical()==0&&receipts.pending()==0&&receipts.Get(1)->Get()->at(0).username=="new");
    Controls::Result held;assert(receipts.Set(103,1,initial,40,[&](auto done){held=std::move(done);},[&](bool ok,const std::string&error){assert(!ok&&error==Controls::DeadlineError());++timed_out;}));
    now=40;receipts.Tick(now);assert(timed_out==2&&receipts.physical()==1&&!receipts.Ready(1));receipts.Closing(1);assert(receipts.physical()==1);receipts.Detach("page:a");receipts.Detach("page:b");assert(receipts.physical()==1&&!receipts.Ready(1));held(true,"");
    while(!callbacks.empty()){auto done=std::move(callbacks.front());callbacks.pop_front();done(true,"");}assert(timed_out==2&&receipts.physical()==0&&receipts.Ready(1));receipts.Closed(1);
  }
  Controls missing_clock(nullptr);assert(missing_clock.Seed(1,initial));
  assert(!missing_clock.Set(1,1,latest,100,[](auto){assert(false);},[](bool,const auto&){assert(false);}));
  assert(missing_clock.Get(1)->Get()->at(0).username=="old"&&missing_clock.physical()==0&&missing_clock.pending()==0);
  std::cout<<"Context credentials genuine receipt ledger: initial race, future broker, timeout/cancel physical hold, target generation, IO/UI snapshots, explicit matching clock, 10000 updates passed\n";
}
