// SPDX-License-Identifier: MIT
#include "virtual_time.h"
#include <cassert>
#include <limits>
#include <vector>
using Clock=suitecut::NativeVirtualTime;
struct Completed{int id;uint64_t generation;bool ok;std::string error;};
struct Fixture{
 std::vector<Clock::Dispatch> wire;std::vector<Completed> done;std::vector<std::string> closed;
 Clock clock{{[this](const auto& packet){wire.push_back(packet);},[this](int id,uint64_t generation,bool ok,const std::string& error){done.push_back({id,generation,ok,error});},[this](const auto& error){closed.push_back(error);}}};
 void Install(){assert(clock.Begin({1,7,Clock::Action::InstallPaused,946684800000.,10000},0));assert(wire.back().kind==Clock::Kind::EnableMetrics);Ack();assert(wire.back().kind==Clock::Kind::Pause);assert(wire.back().epoch_ms==946684800000.);Ack();assert(done.size()==1&&done.back().ok);}
 void Ack(bool ok=true,std::optional<double> observation=std::nullopt,double now=1){auto token=wire.back().token;clock.Acknowledge(token,ok,now,observation);}
};
int main(){
 {
 // Original pinned Performance.Timestamp transport: native TimeTicks::Now()
 // since_origin().InSecondsF(), then Source multiplies seconds by 1000.
 const auto timestamp=[](uint64_t native_ticks){return static_cast<double>(native_ticks)/1000000.*1000.;};
 constexpr uint64_t original=227000000033;
 constexpr double wider_request_bound=9007199254740.;
 const double old_target=timestamp(original)+wider_request_bound;
 const double old_receipt=timestamp(original+static_cast<uint64_t>(wider_request_bound)*1000);
 // This illustrates quantization, not an old liveness failure: adding the
 // old tolerance also rounds upward here. Its failed contrary assertion is
 // preserved in the private proof; no existing browser defect is inferred.
 assert(old_receipt<old_target&&old_receipt+0.001>=old_target);
 for(const auto tail:{uint64_t{0},uint64_t{1},uint64_t{33},uint64_t{999}}){
  const uint64_t begin=original+tail;
  constexpr uint64_t horizon=(uint64_t{1}<<50)-1;
  const double budget=static_cast<double>((horizon-begin-1000)/1000);
  const uint64_t end=begin+static_cast<uint64_t>(budget)*1000;
  Fixture f;f.Install();assert(f.clock.Begin({2,8,Clock::Action::RunFor,budget,10000,31},2));
  f.Ack(true,timestamp(begin),3);assert(f.wire.back().kind==Clock::Kind::Advance);
  f.Ack(true,std::nullopt,4);f.clock.BudgetExpired(5);f.Ack(true,timestamp(end-1),6);
  assert(f.wire.back().kind==Clock::Kind::ObservePerformance&&f.done.size()==1);
  f.clock.BudgetExpired(7);f.Ack(true,timestamp(end),8);
  assert(f.wire.back().kind==Clock::Kind::Pause&&f.done.size()==1);
  f.Ack(true,std::nullopt,9);assert(f.done.back().ok&&f.clock.State().paused);
 }
 }
 {
 // Public runFor durations are not limited to one day. Rejected native
 // requests must leave both the physical dispatch and paused state unchanged.
 Fixture f;f.Install();const auto before=f.wire.size();
 constexpr double maximum=static_cast<double>(((uint64_t{1}<<50)-1)/1000);
 for(const auto bad:{0.,-1.,0.5,maximum+1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
   assert(!f.clock.Begin({2,8,Clock::Action::RunFor,bad,10000,31},2));
   assert(f.wire.size()==before&&!f.clock.State().active&&f.clock.State().paused);
 }
 // The largest admitted budget is checked against actual origin before the
 // first advance. Here origin zero fits the native precision horizon exactly.
 assert(f.clock.Begin({2,8,Clock::Action::RunFor,maximum,10000,31},2));
 f.Ack(true,0,3);assert(f.wire.back().kind==Clock::Kind::Advance&&f.wire.back().budget==maximum);
 const auto token=f.wire.back().token;f.clock.Cancel(2,4);f.clock.Acknowledge(token,true,5);
 assert(f.wire.back().kind==Clock::Kind::Pause);f.Ack(true,std::nullopt,6);
 assert(!f.clock.State().active&&f.clock.State().paused);
 }
 {
 Fixture f;f.Install();assert(!f.clock.Begin({2,8,Clock::Action::Advance,90000000,10000},2));
 assert(f.clock.Begin({2,8,Clock::Action::RunFor,90000000,10000,31},2));
 f.Ack(true,1000,3);assert(f.wire.back().kind==Clock::Kind::Advance&&f.wire.back().budget==90000000);
 f.Ack(true,std::nullopt,4);f.clock.BudgetExpired(5);f.Ack(true,90001000,6);
 assert(f.wire.back().kind==Clock::Kind::Pause&&f.done.size()==1);
 f.Ack(true,std::nullopt,7);assert(f.done.size()==2&&f.done.back().ok&&f.clock.State().paused);
 }
 {
 Fixture f;f.Install();assert(f.clock.Begin({2,8,Clock::Action::RunFor,1000,10000,31},2));
 const auto count=f.wire.size();f.Ack(true,static_cast<double>(((uint64_t{1}<<50)-1)/1000),3);
 // A genuine observed target overflow rejects before any advance and keeps
 // the acknowledged paused clock usable by subsequent real owners.
 assert(f.wire.size()==count&&f.done.size()==2&&!f.done.back().ok&&f.closed.empty());
 assert(!f.clock.State().active&&!f.clock.State().physical&&f.clock.State().paused);
 assert(f.clock.Begin({3,9,Clock::Action::RunFor,1,10000,31},4));
 f.Ack(true,1000,5);assert(f.wire.back().budget==1);
 }
 {
 Fixture f;f.Install();assert(f.clock.Begin({2,8,Clock::Action::RunFor,359999000,10,31},2));
 f.Ack(true,1000,3);const auto token=f.wire.back().token;f.clock.Tick(10);
 assert(!f.done.back().ok&&f.done.back().error.find("Timeout")!=std::string::npos&&f.clock.State().physical);
 f.clock.Acknowledge(token,true,11);assert(f.wire.back().kind==Clock::Kind::Pause);
 f.Ack(true,std::nullopt,12);assert(f.clock.State().paused&&!f.clock.State().active&&f.closed.empty());
 }
 {
 Fixture f;f.Install();const auto before=f.wire.size();
 // Zero cannot be sent as an Inspector virtual-time budget: pinned Chromium
 // skips GrantVirtualTimeBudget(0) entirely and removes the advance fence.
 for(const auto budget:{0.,-0.,0.5,-0.5}){
  assert(!f.clock.Begin({2,8,Clock::Action::Advance,budget,10000},2));
  const auto state=f.clock.State();assert(state.installed&&state.paused&&!state.active&&!state.physical&&state.queued==0);
  assert(f.wire.size()==before&&f.done.size()==1);
 }
 }

 {
 const std::pair<std::string_view,double> valid[]={
  {"01",1000},{"08",8000},{"59",59000},{"01:00",60000},
  {"59:59",3599000},{"00:00:01",1000},
  {"02:34:10",9250000},{"23:59:59",86399000},{"24:00:00",86400000},{"24:00:01",86401000},{"25:00:00",90000000},{"99:59:59",359999000}
 };
 for(const auto& [text,expected]:valid){const auto actual=Clock::ParseDuration(text);assert(actual&&*actual==expected);}
 const std::string_view invalid[]={"1","8","1:00","01:0","2:34:10","02:4:10","02:34:1","","0","00","00:00","00:00:00","60","99","60:00","00:60","01:60:00","00:00:60","100:00:00","12:02:34:10",":01","01:","1::1"," 08","08 ","+08","-1","1.5","0.5","1e3","0x08","01\n","01: 2","\xEF\xBC\x91"};
 for(const auto text:invalid)assert(!Clock::ParseDuration(text));
 // The string view may contain an embedded NUL; it must not be prefix parsed.
 assert(!Clock::ParseDuration(std::string_view("08\0",3)));
 }
 {
 Fixture f;f.Install();const auto budget=Clock::ParseDuration("02:34:10");assert(budget);
 assert(f.clock.Begin({2,8,Clock::Action::Advance,*budget,50},2));
 f.Ack(true,1000,3);assert(f.wire.back().budget==9250000);
 const auto physical=f.wire.back().token;
 assert(f.clock.Begin({3,9,Clock::Action::Advance,*Clock::ParseDuration("08"),20},4));
 f.clock.Tick(21);assert(f.done.size()==2&&f.done.back().id==3&&!f.done.back().ok);
 f.clock.Tick(51);assert(f.done.size()==3&&f.done.back().id==2&&!f.done.back().ok);
 assert(f.clock.State().physical&&f.clock.State().active);
 f.clock.Acknowledge(physical,true,52);assert(f.wire.back().kind==Clock::Kind::Pause);
 assert(f.clock.State().active);f.Ack(true,std::nullopt,53);assert(!f.clock.State().active&&f.clock.State().paused);
 }
 {
 Fixture f;f.Install();assert(f.clock.Begin({2,8,Clock::Action::Advance,*Clock::ParseDuration("08"),10000},2));
 f.Ack(true,1000,3);f.Ack(true,std::nullopt,4);f.clock.BudgetExpired(5);
 f.Ack(true,8999,6);assert(f.done.size()==1); // genuine timestamp is still short
 f.clock.BudgetExpired(7);f.Ack(true,9000,8);assert(f.wire.back().kind==Clock::Kind::Pause);
 assert(f.done.size()==1);f.Ack(true,std::nullopt,9);assert(f.done.size()==2&&f.done.back().ok);
 }
 {
 Fixture f;f.Install();assert(f.clock.Begin({2,8,Clock::Action::Advance,*Clock::ParseDuration("01:00"),10000},2));
 const auto physical=f.wire.back().token;f.clock.Cancel(2,3);assert(f.clock.State().physical);
 f.clock.Acknowledge(physical,true,4,1000);assert(f.wire.back().kind==Clock::Kind::Pause&&f.clock.State().active);
 f.Ack(true,std::nullopt,5);assert(f.done.size()==1&&!f.clock.State().active&&f.clock.State().paused);
 }

 {
 Fixture f;f.Install();assert(f.clock.Begin({2,8,Clock::Action::Advance,1000,10000},2));assert(f.wire.back().kind==Clock::Kind::ObservePerformance);f.Ack(true,1000,3);assert(f.wire.back().kind==Clock::Kind::Advance&&f.wire.back().budget==1000);
 f.clock.BudgetExpired(4);f.Ack(true,std::nullopt,5);assert(f.wire.back().kind==Clock::Kind::ObservePerformance);
 // An old nonce-less event cannot settle a later native budget early.
 f.Ack(true,1500,6);assert(f.done.size()==1);f.clock.BudgetExpired(7);f.Ack(true,2000,8);assert(f.wire.back().kind==Clock::Kind::Pause);assert(f.clock.Begin({3,9,Clock::Action::Pause,0,10000},8));auto before=f.wire.size();assert(f.clock.State().queued==1);f.Ack(true,std::nullopt,9);assert(f.done.size()==3&&f.done[1].id==2&&f.done[1].ok&&f.done.back().id==3&&f.done.back().ok);assert(f.wire.size()==before&&!f.clock.State().physical&&f.clock.State().paused);
 }
 {
 Fixture f;f.Install();assert(f.clock.Begin({2,8,Clock::Action::Advance,100,10},2));const auto blocked=f.wire.back().token;assert(f.clock.Begin({3,9,Clock::Action::Pause,0,3000},3));f.clock.Tick(11);assert(f.done.size()==2&&!f.done.back().ok);assert(f.clock.State().physical&&f.clock.State().queued==1);f.clock.Acknowledge(blocked-1,true,12,100);assert(f.clock.State().physical);f.clock.Acknowledge(blocked,true,12,100);assert(f.wire.back().kind==Clock::Kind::Pause);f.clock.Tick(900);assert(f.closed.empty());f.Ack(true,std::nullopt,901);assert(!f.clock.State().physical&&f.clock.State().paused);assert(f.done.size()==3&&f.done.back().id==3&&f.done.back().ok);
 }
 {
 Fixture f;f.Install();assert(f.clock.Begin({2,8,Clock::Action::Advance,100,10000},2));f.Ack(true,1000,3);f.Ack(true,std::nullopt,4);f.clock.Cancel(2,5);assert(f.wire.back().kind==Clock::Kind::Pause);f.clock.BudgetExpired(6);f.Ack(false,std::nullopt,7);assert(f.closed.size()==1&&f.clock.State().closing);assert(!f.clock.Begin({3,9,Clock::Action::Pause,0,10000},8));f.clock.Tick(2000);assert(f.closed.size()==1);f.clock.Closed();assert(!f.clock.State().physical&&f.clock.State().queued==0);
 }
 {
 Fixture f;f.Install();assert(f.clock.Begin({2,8,Clock::Action::Advance,100,10000},2));f.clock.Cancel(2,3);f.clock.Tick(1004);assert(f.closed.size()==1&&f.clock.State().physical);f.clock.Closed();assert(!f.clock.State().physical);
 }
 {
 Fixture f;f.Install();assert(f.clock.Begin({100,100,Clock::Action::Advance,100,10000},2));for(int i=2;i<=64;++i)assert(f.clock.Begin({i,static_cast<uint64_t>(i),Clock::Action::Pause,0,10000},2));assert(!f.clock.Begin({65,65,Clock::Action::Pause,0,10000},2));f.clock.Closed();assert(f.clock.State().queued==0);
 }
 {
 Fixture f;assert(!f.clock.Begin({1,1,Clock::Action::InstallPaused,0,100},std::numeric_limits<double>::quiet_NaN()));assert(!f.clock.OwnsTarget()&&f.wire.empty()&&f.done.empty());assert(!f.clock.Begin({1,1,Clock::Action::Advance,std::numeric_limits<double>::infinity(),100},0));assert(!f.clock.Begin({1,1,Clock::Action::Advance,0,100},0));assert(!f.clock.Begin({1,1,Clock::Action::InstallPaused,0,std::numeric_limits<double>::infinity()},0));assert(f.clock.Begin({1,1,Clock::Action::Pause,0,100},0));assert(f.wire.empty()&&f.done.size()==1&&!f.done.back().ok);
 }
}
