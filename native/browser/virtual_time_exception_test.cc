// SPDX-License-Identifier: MIT
#include "virtual_time.h"
#include <cassert>
#include <string>
#include <vector>
using Clock=suitecut::NativeVirtualTime;
struct Result {int id;uint64_t generation;bool ok;std::string error;};
struct Fixture {
  std::vector<Clock::Dispatch> sent;std::vector<Result> done;std::vector<std::string> closed;
  uint64_t received=0;
  Clock clock{{[this](const auto& call){sent.push_back(call);},[this](int id,uint64_t generation,bool ok,const auto& error){done.push_back({id,generation,ok,error});},[this](const auto& error){closed.push_back(error);}}};
  void Ack(double now=1,std::optional<double> ticks=std::nullopt){clock.Acknowledge(sent.back().token,true,now,ticks,received);}
  void Install(){assert(clock.Begin({1,1,Clock::Action::InstallPaused,946684800000.,10000},0));Ack();Ack();assert(done.size()==1&&done.back().ok);}
  void Run(double deadline=10000){assert(clock.Begin({2,2,Clock::Action::RunFor,120,deadline,31},2));Ack(3,1000);assert(sent.back().kind==Clock::Kind::Advance);}
  bool Exception(uint64_t sequence,int context=31,bool synchronous=true,const std::string& text="Actual first error"){
    if(sequence>received)received=sequence;
    return clock.Exception({sequence,context,synchronous,text});
  }
  void ReachTarget(double now=8){clock.BudgetExpired(now);Ack(now+1,1120);assert(sent.back().kind==Clock::Kind::Pause);}
};
int main(){
  {
    assert(Clock::IsSynchronousException("Uncaught"));assert(Clock::IsSynchronousException("Uncaught Error: actual"));
    for(const auto text:{"","Uncaught (in promise)","Uncaught (in promise) Error: actual","Caught","Error: actual"})assert(!Clock::IsSynchronousException(text));
  }
  {
    Fixture f;f.Install();assert(!f.clock.Begin({2,2,Clock::Action::RunFor,120,10000},2));
    assert(!f.clock.State().active&&f.clock.State().paused);
  }
  {
    Fixture f;assert(!f.Exception(1));f.Install();
    assert(f.clock.Begin({2,2,Clock::Action::RunFor,120,10000,31},2));
    assert(!f.Exception(2));f.Ack(3,1000);
    assert(!f.Exception(1));assert(!f.Exception(2));
    assert(f.Exception(3));f.Ack(4);f.ReachTarget();
    assert(f.done.size()==1&&f.clock.State().physical);f.Ack(10);
    assert(f.done.size()==2&&!f.done.back().ok&&f.done.back().error.find("Actual first error")!=std::string::npos);
    assert(f.clock.State().paused&&!f.clock.State().active&&f.closed.empty());
  }
  {
    Fixture f;f.Install();f.Run();
    assert(!f.Exception(1,99));assert(!f.Exception(2,31,false));
    f.Ack(4);f.ReachTarget();assert(f.done.size()==1);f.Ack(10);
    assert(f.done.size()==2&&f.done.back().ok);
  }
  {
    Fixture f;f.Install();f.Run();assert(f.Exception(1));assert(!f.Exception(1));
    assert(!f.Exception(2,31,true,"Actual second error"));f.Ack(4);f.ReachTarget();f.Ack(10);
    assert(!f.done.back().ok&&f.done.back().error.find("Actual first error")!=std::string::npos&&f.done.back().error.find("Actual second error")==std::string::npos);
  }
  {
    Fixture f;f.Install();f.Run();f.Ack(4);f.clock.BudgetExpired(5);
    assert(f.Exception(1));f.Ack(6,1100);assert(f.done.size()==1);
    f.clock.BudgetExpired(7);f.Ack(8,1120);assert(f.sent.back().kind==Clock::Kind::Pause);
    assert(f.done.size()==1);f.Ack(9);assert(!f.done.back().ok);
  }
  {
    Fixture f;f.Install();f.Run();f.Ack(4);f.ReachTarget();
    assert(f.Exception(1,31,true,"Actual final-fence receipt"));assert(f.done.size()==1);
    f.Ack(10);assert(!f.done.back().ok&&f.done.back().error.find("Actual final-fence receipt")!=std::string::npos);
  }
  {
    Fixture f;f.Install();f.Run(10);assert(f.Exception(1));f.Ack(4);f.ReachTarget(6);f.Ack(10);
    assert(!f.done.back().ok&&f.done.back().error.find("Timeout")!=std::string::npos);
  }
  {
    Fixture f;f.Install();f.Run(10);assert(f.Exception(1));const auto physical=f.sent.back().token;
    f.clock.Tick(11);assert(f.done.size()==2&&!f.done.back().ok&&f.clock.State().physical);
    assert(f.done.back().error.find("Timeout")!=std::string::npos);
    assert(!f.Exception(2));f.clock.Acknowledge(physical,true,12,std::nullopt,f.received);
    assert(f.sent.back().kind==Clock::Kind::Pause);f.Ack(13);assert(f.done.size()==2);
  }
  {
    Fixture f;f.Install();f.Run();assert(f.Exception(1));const auto physical=f.sent.back().token;
    f.clock.Cancel(2,4);assert(f.clock.State().physical&&f.done.size()==1);assert(!f.Exception(2));
    f.clock.Acknowledge(physical,true,5,std::nullopt,f.received);assert(f.sent.back().kind==Clock::Kind::Pause);
    assert(f.done.size()==1);f.Ack(6);assert(f.done.size()==1&&f.clock.State().paused);
  }
  {
    Fixture f;f.Install();assert(f.clock.Begin({2,2,Clock::Action::Advance,120,10000},2));f.Ack(3,1000);
    assert(!f.Exception(1));f.Ack(4);f.ReachTarget();f.Ack(10);assert(f.done.back().ok);
  }
  {
    Fixture f;f.Install();f.Run();assert(f.Exception(1));f.Ack(4);f.ReachTarget();f.Ack(10);
    assert(!f.Exception(2)); // A newly observed but paused exception is outside a runFor.
    assert(f.clock.Begin({3,3,Clock::Action::RunFor,10,10000,31},11));f.Ack(12,1120);
    assert(!f.Exception(1));assert(!f.Exception(2));f.Ack(13);f.clock.BudgetExpired(14);f.Ack(15,1130);f.Ack(16);
    assert(f.done.back().id==3&&f.done.back().ok);
  }
  {
    Fixture f;f.Install();f.Run();assert(f.Exception(1,31,true,std::string(65537,'x')));f.Ack(4);f.ReachTarget();f.Ack(10);
    assert(!f.done.back().ok&&f.done.back().error.size()<200&&f.done.back().error.find("65536-byte")!=std::string::npos);
  }
  {
    Fixture f;f.Install();assert(f.clock.Begin({2,2,Clock::Action::RunFor,120,10000,31},2));
    assert(!f.Exception(5));f.clock.Acknowledge(f.sent.back().token,true,3,1000,4);
    assert(f.clock.State().closing&&f.closed.size()==1);
  }
  {
    Fixture f;f.Install();f.Run();assert(f.Exception(5));f.Ack(4);f.ReachTarget();
    f.clock.Acknowledge(f.sent.back().token,true,10,std::nullopt,4);
    assert(f.clock.State().closing&&f.clock.State().active&&f.closed.size()==1&&f.done.size()==1);
  }
  {
    Fixture f;f.Install();f.Run();assert(f.Exception(1));f.Ack(4);f.ReachTarget();
    f.clock.Acknowledge(f.sent.back().token,false,10,std::nullopt,f.received);
    assert(f.clock.State().closing&&f.clock.State().active&&f.closed.size()==1&&f.done.size()==1);
    f.clock.Closed();assert(!f.clock.State().active);
  }
  {
    Fixture f;f.Install();f.Run();
    assert(f.clock.Begin({3,3,Clock::Action::RunFor,10,10000,32},3));
    assert(!f.Exception(1,32));f.Ack(4);f.ReachTarget();f.Ack(10);
    assert(f.done.back().id==2&&f.done.back().ok);
    assert(f.sent.back().kind==Clock::Kind::ObservePerformance);f.Ack(11,1120);
    assert(!f.Exception(1,32));assert(f.Exception(2,32,true,"Actual queued budget error"));
    f.Ack(12);f.clock.BudgetExpired(13);f.Ack(14,1130);assert(f.done.size()==2);f.Ack(15);
    assert(f.done.back().id==3&&!f.done.back().ok&&f.done.back().error.find("Actual queued budget error")!=std::string::npos);
  }
}
