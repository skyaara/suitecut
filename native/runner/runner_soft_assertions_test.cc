// SPDX-License-Identifier: MIT
#include "runner_soft_assertions.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void RejectSoftPacket(F&& f){bool caught=false;try{f();}catch(const std::exception&){caught=true;}assert(caught);}
std::vector<std::string> BeginSoft(uint64_t request=1,uint64_t step=0){return {"SOFT_BEGIN",std::to_string(request),"0","1","1",std::to_string(step),"-","0","0","0"};}
std::vector<std::string> EndSoft(uint64_t request=1,uint64_t id=1,std::string raw="S",std::string error="\"actual mismatch\""){return {"SOFT_SETTLED",std::to_string(request),"0","1","1",std::to_string(id),std::move(raw),StepWire(error)};}
struct Fixture {
 StepOwner owner{1,1,0,1,1,1,101,"body"};std::vector<std::string> packets,hard,kills;std::vector<bool> infrastructure;std::vector<SoftAssertionRecord> mismatches;StepRecord step;RunnerSoftAssertions journal;
 Fixture():journal({[this](const auto& p){packets.push_back(p);},[this](const auto& r,double){mismatches.push_back(r);},[this](const auto&,const auto& e,bool infra){hard.push_back(e);infrastructure.push_back(infra);},[this](const auto& e){kills.push_back(e);},[this](auto id){return id==step.id?&step:nullptr;}}){step.id=7;step.owner=owner;step.deadline=80;}
 void Open(double deadline=100){journal.Open(owner,deadline,0);}
};
int main(){unsigned groups=0;
 {Fixture f;f.Open();assert(f.packets.at(0).starts_with("SOFT_CONTEXT 0 1 1 1 1 1 101 body"));f.journal.Packet(BeginSoft(),1,false);assert(f.journal.Physical()==1&&f.packets.back()=="SOFT_ADMITTED 1 0 1 1 1 1 99 -");f.journal.Packet(EndSoft(1,1,"P","\"\""),2,false);assert(!f.journal.Physical()&&!f.journal.FailedAttempt(1)&&f.journal.Records().empty());bool forwarded=false;f.journal.Fence(0,1,1,[&]{forwarded=true;},3);assert(forwarded&&f.journal.IdleReady()&&f.packets.back()=="SOFT_CONTEXT_RETIRED 0 1 1");++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,false);bool raw=false;f.journal.Fence(0,1,1,[&]{raw=true;},2);assert(!raw&&!f.journal.IdleReady());f.journal.Packet(EndSoft(),3,false);assert(raw&&f.journal.IdleReady()&&f.mismatches.size()==1&&f.journal.FailedAttempt(1));assert(f.packets.at(f.packets.size()-2)=="SOFT_SETTLED_ACK 1 0 1 1 1 1 -");++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(1,7),1,false);bool file=false;f.journal.FenceStep(f.owner,7,[&]{file=true;},2);assert(!file);f.step.callback_received=2;f.journal.Packet(EndSoft(),3,false);assert(file&&f.mismatches.at(0).step==7);++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,false);f.journal.Packet(EndSoft(1,1,"E","\"raw author exception\""),2,false);assert(f.hard.size()==1&&!f.infrastructure[0]&&f.hard[0]==RuntimeSkipReason(StepWire("\"raw author exception\""))&&!f.journal.FailedAttempt(1)&&f.journal.Records().at(0).receipt==SoftReceipt::OtherFailure);++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,false);bool raw=false;f.journal.Fence(0,1,1,[&]{raw=true;},2,"\"actual outer hard\"");assert(!raw&&f.journal.Physical()==1&&f.packets.back().starts_with("SOFT_ABORT 1 0 1 1 1 "));f.journal.Packet(EndSoft(),3,false);assert(raw&&!f.journal.FailedAttempt(1)&&f.packets.at(f.packets.size()-2).starts_with("SOFT_SETTLED_ACK 1 0 1 1 1 0 "));++groups;}
 {Fixture f;f.Open(10);f.journal.Packet(BeginSoft(),1,false);f.journal.Tick(10);assert(f.journal.Physical()==1&&f.packets.back().starts_with("SOFT_ABORT "));f.journal.Packet(EndSoft(),11,false);assert(!f.journal.FailedAttempt(1)&&f.journal.Records().at(0).receipt==SoftReceipt::Late);++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,false);auto foreign=EndSoft();foreign[3]="2";RejectSoftPacket([&]{f.journal.Packet(foreign,2,false);});assert(f.journal.Physical()==1);f.journal.Packet(EndSoft(),3,false);RejectSoftPacket([&]{f.journal.Packet(EndSoft(),4,false);});++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,false);RejectSoftPacket([&]{f.journal.Packet(BeginSoft(),2,false);});f.journal.Packet(EndSoft(),3,false);auto bad=BeginSoft(2);bad[6]=StepWire("\"explicit-file\"");bad[7]="1";bad[8]="2";bad[9]="2";f.journal.Packet(bad,4,false);assert(f.hard.size()==1&&f.journal.Physical()==0);++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(1,88),1,false);assert(f.hard.size()==1&&f.infrastructure[0]&&!f.journal.Physical()&&f.packets.back().starts_with("SOFT_ADMITTED 1 0 1 1 0 0 "));++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,true);assert(f.hard.size()==1&&f.infrastructure[0]&&!f.journal.Physical());++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,false);assert(!f.journal.CanSkip(0,1,1));f.journal.Packet(EndSoft(),2,false);assert(!f.journal.CanSkip(0,1,1));++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,false);f.journal.WorkerExited(2,101,2);assert(f.journal.Physical()==1);f.journal.Packet(EndSoft(),3,false);assert(f.journal.FailedAttempt(1));f.journal.Packet(BeginSoft(2),4,false);f.journal.WorkerExited(1,101,5);assert(f.journal.IdleReady()&&f.journal.Records().back().receipt==SoftReceipt::WorkerExited&&f.journal.FailedAttempt(1));++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,false);bool raw=false;f.journal.Fence(0,1,1,[&]{raw=true;},2);f.journal.Tick(1002);assert(!raw&&f.kills.size()==1&&f.journal.Physical()==1);f.journal.Tick(1003);assert(f.kills.size()==1);++groups;}
 {Fixture f;f.Open(2);f.journal.Packet(BeginSoft(),1.75,false);assert(f.packets.back()=="SOFT_ADMITTED 1 0 1 1 1 1 1 -");++groups;}
 {Fixture f;auto all=f.owner;all.callback_kind="beforeAll";all.callback_token=2;f.journal.Open(all,100,0);assert(f.journal.IdleReady()&&f.packets.empty());bool raw=false;f.journal.Fence(2,1,1,[&]{raw=true;},1);assert(raw);++groups;}
 {Fixture f;f.Open();f.journal.Packet(BeginSoft(),1,false);f.journal.Amend(0,1,1,30,2);f.journal.Tick(30);assert(f.journal.Physical()==1&&f.packets.back().starts_with("SOFT_ABORT "));++groups;}
 {Fixture f;f.Open();auto begin=BeginSoft();begin[6]=StepWire("\"/actual/source.ts\"");begin[7]="9";begin[8]="3";begin[9]="1";f.journal.Packet(begin,1,false);f.journal.Packet(EndSoft(),2,false);assert(f.mismatches.at(0).location->file_json==RuntimeSkipReason(StepWire("\"/actual/source.ts\""))&&!f.mismatches.at(0).location->explicit_location);++groups;}
 std::cout<<"PASS "<<groups<<" native soft protocol/fence controls (controlled callbacks, no OS or matcher claim)\n";
}
