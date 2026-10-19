// SPDX-License-Identifier: MIT
#include "callback_retry_schedule.h"
#include <cassert>
#include <iostream>
#include <limits>

namespace {
using Schedule=suitecut::runner::CallbackRetrySchedule;
using Verdict=Schedule::Verdict;
using Phase=Schedule::Phase;
using Raw=Schedule::Raw;
using Fence=Schedule::Fence;
Schedule::Identity Identity(){return{10,0,{1,7,0,1,7,2,501,"body"}};}
Schedule Make(uint64_t timeout=0,double deadline=10000,double now=100){return{Identity(),{timeout,std::nullopt},deadline,now};}
template<class F>void Reject(F&& callback){bool rejected=false;try{callback();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
void ThrowAndDrain(Schedule& schedule,Schedule::Invocation invocation,double raw,double physical){schedule.RawSettled(invocation,Raw::Threw,raw);schedule.PhysicallySettled(invocation,physical);}
}

int main(){
  unsigned controls=0;
  {
    auto s=Make();const auto first=s.Start(10,100);
    s.RawSettled(first,Raw::Threw,101);
    assert(s.Physical()&&!s.Ready(200));
    s.PhysicallySettled(first,250);
    assert(!s.Ready(349)&&s.Ready(350));
    const auto second=s.Start(13,350);s.RawSettled(second,Raw::Returned,351);
    assert(s.Result()==Verdict::Pending&&s.Physical());
    s.PhysicallySettled(second,400);
    assert(s.Result()==Verdict::Passed&&!s.Physical()&&s.Attempts()==2);++controls;
  }
  {
    auto s=Make();double now=100;uint64_t step=10;
    for(const double interval:{100,250,500,1000,1000}){
      const auto call=s.Start(step++,now);ThrowAndDrain(s,call,now+1,now+2);
      assert(s.NextWake()==now+2+interval);
      now+=2+interval;
    }
    assert(s.Attempts()==5);++controls;
  }
  {
    auto s=Make(500,2000);const auto first=s.Start(10,100);
    ThrowAndDrain(s,first,110,120);
    assert(s.AmendOwner(9000,130)&&s.OwnDeadline()==600&&s.Deadline()==600);
    const auto second=s.Start(11,220);s.RawSettled(second,Raw::Returned,599);
    s.PhysicallySettled(second,600);
    assert(s.Result()==Verdict::TimedOut&&!s.Physical());++controls;
  }
  {
    auto s=Make(0,200);assert(!s.OwnDeadline());
    const auto call=s.Start(10,100);s.Tick(200);
    assert(s.Result()==Verdict::TimedOut&&s.Physical());
    s.RawSettled(call,Raw::Returned,201);s.PhysicallySettled(call,202);
    assert(s.Result()==Verdict::TimedOut&&s.Actual().receipt==Schedule::Receipt::Settled);++controls;
  }
  {
    const std::array<uint64_t,1> huge{Schedule::kSafeDuration};
    Schedule s(Identity(),{0,std::span<const uint64_t>(huge)},1000,100);
    ThrowAndDrain(s,s.Start(10,100),101,102);
    assert(s.NextWake()==1000&&!s.Ready(999));
    s.Tick(1000);assert(s.Result()==Verdict::TimedOut&&!s.Physical());++controls;
  }
  {
    const std::array<uint64_t,1> zero{0};
    Schedule s(Identity(),{0,std::span<const uint64_t>(zero)},1000,100);
    const auto call=s.Start(10,100);
    Reject([&]{s.Start(11,100);});
    ThrowAndDrain(s,call,100,100);assert(s.Ready(100));
    const auto next=s.Start(11,100);assert(next.nonce==2&&s.Physical());++controls;
  }
  {
    auto s=Make();const auto call=s.Start(10,100);
    Reject([&]{s.PhysicallySettled(call,101);});
    assert(s.Physical()&&!s.Actual().raw_received);++controls;
  }
  {
    auto s=Make();const auto old=s.Start(10,100);ThrowAndDrain(s,old,101,102);
    const auto current=s.Start(12,202);
    Reject([&]{s.RawSettled(old,Raw::Returned,203);});
    auto foreign=current;foreign.assertion=99;
    Reject([&]{s.RawSettled(foreign,Raw::Returned,204);});
    assert(s.Physical()&&!s.Actual().raw_received);++controls;
  }
  {
    auto s=Make(20);const auto call=s.Start(10,100);s.Tick(120);
    assert(s.Result()==Verdict::TimedOut&&s.Physical());
    s.RawSettled(call,Raw::Returned,121);assert(s.Physical());
    s.PhysicallySettled(call,122);assert(s.Result()==Verdict::TimedOut&&!s.Physical());++controls;
  }
  {
    auto s=Make();const auto call=s.Start(10,100);
    assert(!s.ConfirmedWorkerExit(3,501,101)&&s.Physical());
    assert(!s.ConfirmedWorkerExit(2,502,102)&&s.Physical());
    assert(s.ConfirmedWorkerExit(2,501,103)&&!s.Physical());
    assert(s.Result()==Verdict::InfrastructureFailure&&!s.Actual().raw_received&&s.Actual().receipt==Schedule::Receipt::WorkerExited);
    Reject([&]{s.RawSettled(call,Raw::Returned,104);});++controls;
  }
  {
    auto s=Make();const auto call=s.Start(10,100);
    assert(s.FailInfrastructure(101));
    s.RawSettled(call,Raw::Returned,102);s.PhysicallySettled(call,103);
    assert(s.Result()==Verdict::InfrastructureFailure&&!s.Physical());++controls;
  }
  {
    auto s=Make();const auto call=s.Start(10,100);s.RawSettled(call,Raw::Returned,101);
    assert(s.FailInfrastructure(102)&&s.Physical());
    s.PhysicallySettled(call,103);assert(s.Result()==Verdict::InfrastructureFailure);++controls;
  }
  {
    auto s=Make();const auto call=s.Start(10,100);assert(s.Cancel(101)&&s.Physical());
    s.RawSettled(call,Raw::Threw,102);s.PhysicallySettled(call,103);
    assert(s.Result()==Verdict::Canceled&&!s.Physical());++controls;
  }
  {
    auto s=Make(500,300);assert(s.AmendOwner(200,150)&&s.Deadline()==200);
    s.Tick(200);assert(!s.AmendOwner(1000,200)&&s.Result()==Verdict::TimedOut);++controls;
  }
  {
    const std::array<uint64_t,1> intervals{1000};
    Schedule s(Identity(),{0,std::span<const uint64_t>(intervals)},250,100);
    ThrowAndDrain(s,s.Start(10,100),101,102);assert(s.NextWake()==250);
    assert(s.AmendOwner(2000,200)&&s.NextWake()==1102);
    assert(!s.Ready(1101)&&s.Ready(1102));++controls;
  }
  {
    auto s=Make();const auto call=s.Start(10,100);
    s.RawSettled(call,Raw::Returned,101);s.PhysicallySettled(call,102,Fence::AuthoredFailure);
    assert(s.Result()==Verdict::Pending&&s.State()==Phase::Backoff&&s.Ready(202));++controls;
  }
  {
    auto s=Make();const auto call=s.Start(10,100);
    s.RawSettled(call,Raw::Returned,101);s.PhysicallySettled(call,102,Fence::InfrastructureFailure);
    assert(s.Result()==Verdict::InfrastructureFailure&&!s.Physical());++controls;
  }
  {
    auto s=Make();const auto call=s.Start(10,100);
    s.RawSettled(call,Raw::Returned,101);s.PhysicallySettled(call,102);
    Reject([&]{s.RawSettled(call,Raw::Threw,103);});
    Reject([&]{s.PhysicallySettled(call,104);});
    assert(!s.FailInfrastructure(105)&&s.Result()==Verdict::Passed);++controls;
  }
  {
    auto s=Make();s.Tick(101);
    Reject([&]{s.Tick(100);});
    Reject([&]{s.Tick(std::numeric_limits<double>::infinity());});++controls;
  }
  {
    std::array<uint64_t,65> large{};std::array<uint64_t,1> unsafe{Schedule::kSafeDuration+1};
    Reject([&]{Schedule s(Identity(),{0,std::span<const uint64_t>{}},1000,100);});
    Reject([&]{Schedule s(Identity(),{0,std::span<const uint64_t>(large)},1000,100);});
    Reject([&]{Schedule s(Identity(),{0,std::span<const uint64_t>(unsafe)},1000,100);});
    Reject([&]{Schedule s(Identity(),{Schedule::kSafeDuration+1,std::nullopt},1000,100);});++controls;
  }
  {
    for(const auto* kind:{"beforeAll","afterAll","fixture"}){
      auto identity=Identity();identity.owner.callback_kind=kind;identity.owner.callback_token=8;
      Reject([&]{Schedule s(identity,{},1000,100);});
    }
    for(const auto* kind:{"beforeEach","afterEach"}){
      auto identity=Identity();identity.owner.callback_kind=kind;identity.owner.callback_token=8;
      Schedule s(identity,{},1000,100);assert(s.Ready(100));
    }
    auto called_body=Identity();called_body.owner.callback_token=8;
    Schedule actual_body_call(called_body,{},1000,100);assert(actual_body_call.Ready(100));
    ++controls;
  }
  std::cout<<"callback retry scheduling controls "<<controls<<" passed\n";
}
