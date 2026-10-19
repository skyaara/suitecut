// SPDX-License-Identifier: MIT
#include "virtual_time.h"
#include <cassert>
#include <string>
#include <vector>
using Clock=suitecut::NativeVirtualTime;
struct Receipt {int id;uint64_t generation;bool ok;};
struct Fixture {
  std::vector<Clock::Dispatch> sent;
  std::vector<Receipt> completed;
  std::vector<std::string> close;
  Clock clock{{
    [this](const auto& call){sent.push_back(call);},
    [this](int id,uint64_t generation,bool ok,const auto&){completed.push_back({id,generation,ok});},
    [this](const auto& error){close.push_back(error);}
  }};
  void Ack(double now=1,std::optional<double> performance=std::nullopt){clock.Acknowledge(sent.back().token,true,now,performance);}
  void BeginInstall(){assert(clock.Begin({1,1,Clock::Action::InstallPaused,946684800000.,10000},0));}
  void Install(){BeginInstall();Ack();Ack();}
  void Invalidate(bool physical){
    const auto before=sent.size(),done=completed.size();
    const auto token=sent.empty()?0:sent.back().token;
    assert(clock.OwnsTarget());clock.TargetChanged();
    assert(close.size()==1&&clock.State().closing&&clock.OwnsTarget());
    assert(clock.State().physical==physical);
    assert(!clock.Begin({50,50,Clock::Action::Pause,0,10000},5));
    // ACKs and nonce-less events after renderer/document invalidation cannot
    // transfer the old owner into a new document or fabricate completion.
    clock.Acknowledge(token,true,6,100000);clock.BudgetExpired(7);clock.Tick(8);
    assert(sent.size()==before&&completed.size()==done&&close.size()==1);
    clock.Tick(20000);
    for(size_t i=done;i<completed.size();++i)assert(!completed[i].ok);
    assert(sent.size()==before&&close.size()==1);
    assert(clock.State().physical==physical&&clock.OwnsTarget());
    clock.Closed();assert(!clock.State().active&&!clock.State().physical&&clock.State().queued==0);
    assert(!clock.State().installed&&clock.State().closing);
  }
};
int main(){
  {Fixture f;assert(!f.clock.OwnsTarget());assert(!f.clock.Begin({1,1,Clock::Action::Advance,0,10000},0));assert(!f.clock.OwnsTarget());}
  // Genuine native controller transitions; the Source event selection itself
  // still requires matching-build browser receipts.
  {Fixture f;f.BeginInstall();assert(!f.clock.State().installed);f.Invalidate(true);}
  {Fixture f;f.BeginInstall();f.Ack();assert(!f.clock.State().installed&&f.sent.back().kind==Clock::Kind::Pause);f.Invalidate(true);}
  {Fixture f;f.Install();f.Invalidate(false);}
  {Fixture f;f.Install();assert(f.clock.Begin({2,2,Clock::Action::Advance,100,10000},2));f.Invalidate(true);}
  {Fixture f;f.Install();assert(f.clock.Begin({2,2,Clock::Action::Advance,100,10000},2));f.Ack(3,1000);f.Invalidate(true);}
  {Fixture f;f.Install();assert(f.clock.Begin({2,2,Clock::Action::Advance,100,10000},2));f.Ack(3,1000);f.Ack(4);f.Invalidate(false);}
  {Fixture f;f.Install();assert(f.clock.Begin({2,2,Clock::Action::Advance,100,10000},2));f.Ack(3,1000);f.Ack(4);f.clock.BudgetExpired(5);f.Ack(6,1100);assert(f.sent.back().kind==Clock::Kind::Pause);f.Invalidate(true);}
  {Fixture f;f.BeginInstall();assert(f.clock.Begin({2,2,Clock::Action::Pause,0,10000},1));assert(f.clock.State().queued==1);f.Invalidate(true);}
  {Fixture f;f.Install();assert(f.clock.Begin({2,2,Clock::Action::Advance,100,10},2));f.clock.Tick(11);assert(f.completed.size()==2&&!f.completed.back().ok);f.Invalidate(true);}
}
