// SPDX-License-Identifier: MIT
#include "runner_soft_assertions.h"
#include "hook_phases.h"
#include <cassert>
#include <iostream>
#include <sstream>
using namespace suitecut::runner;
std::vector<std::string> Fields(const std::string& text){std::istringstream stream(text);std::vector<std::string> result;std::string field;while(stream>>field)result.push_back(field);return result;}
struct Control {
  std::vector<std::string> packets;unsigned hard=0;bool forwarded=false;
  RunnerSoftAssertions soft{{[this](const auto& p){packets.push_back(p);},[](const auto&,double){assert(false);},[this](const auto&,const auto&,bool infrastructure){assert(infrastructure);++hard;},[](const auto&){assert(false);},[](uint64_t)->const StepRecord*{return nullptr;}}};
  StepOwner owner(std::string kind="afterEach")const{return {1,1,kind=="body"?0u:7u,1,1,1,1234,std::move(kind)};}
  std::vector<std::string> begin(const StepOwner& o)const{return {"SOFT_BEGIN","1",std::to_string(o.callback_token),"1","1","0","-","0","0","0"};}
  void fence(const StepOwner& o,double now){soft.Fence(o.callback_token,1,1,[this]{forwarded=true;},now);assert(forwarded&&soft.IdleReady());}
};
int main(){unsigned groups=0;
 {assert(SoftCallbackDeadline("body",200,100)==100&&SoftCallbackDeadline("beforeEach",200,100)==100&&SoftCallbackDeadline("body",60,100)==60);++groups;}
 {assert(SoftCallbackDeadline("afterEach",200,100)==200&&SoftCallbackDeadline("afterEach",60,100)==60&&SoftCallbackDeadline("afterEach",60)==60&&SoftCallbackDeadline("afterEach",200,100,150)==150);++groups;}
 {assert(!SoftAdmissionAllowed("body",true,true)&&!SoftAdmissionAllowed("beforeEach",true,true)&&SoftAdmissionAllowed("afterEach",true,true)&&!SoftAdmissionAllowed("afterEach",true,false)&&SoftAdmissionAllowed("body",false,false));++groups;}
 {Control c;const auto owner=c.owner();bool rejected=false;try{c.soft.Open(owner,50,60,100);}catch(const std::invalid_argument&){rejected=true;}assert(rejected&&c.packets.empty()&&c.soft.IdleReady());++groups;}
 {Control c;const auto owner=c.owner();c.soft.Open(owner,200,110,100);assert(c.packets[0].starts_with("SOFT_CONTEXT 7 1 1 "));c.soft.Packet(c.begin(owner),111,true,true);const auto admitted=Fields(c.packets.back());assert(admitted[0]=="SOFT_ADMITTED"&&admitted[5]=="1"&&admitted[6]=="1"&&admitted[7]=="89"&&c.soft.Physical()==1&&!c.hard);c.soft.Packet({"SOFT_SETTLED","1","7","1","1","1","P",StepWire("\"\"")},112,true,true);assert(!c.soft.Physical()&&c.packets.back().starts_with("SOFT_SETTLED_ACK 1 7 1 1 1 1"));c.fence(owner,113);++groups;}
 {Control c;const auto owner=c.owner("body");c.soft.Open(owner,200,10,100);c.soft.Packet(c.begin(owner),20,true,true);assert(c.hard==1&&Fields(c.packets.back())[6]=="0"&&!c.soft.Physical());c.fence(owner,21);++groups;}
 {Control c;const auto owner=c.owner("beforeEach");c.soft.Open(owner,200,10,100);c.soft.Packet(c.begin(owner),20,true,true);assert(c.hard==1&&!c.soft.Physical());c.fence(owner,21);++groups;}
 {Control c;const auto owner=c.owner();c.soft.Open(owner,200,10,100);c.soft.Packet(c.begin(owner),20,true,false);assert(c.hard==1&&!c.soft.Physical());c.fence(owner,21);++groups;}
 {Control c;const auto owner=c.owner();c.soft.Open(owner,200,10,100);c.soft.Cancel(7,1,1,"\"actual retirement\"",20);c.soft.Packet(c.begin(owner),21,true,true);assert(c.hard==1&&!c.soft.Physical());c.fence(owner,22);++groups;}
 {Control c;const auto owner=c.owner();c.soft.Open(owner,40,10,5);c.soft.Packet(c.begin(owner),41,true,true);assert(c.hard==1&&!c.soft.Physical());c.fence(owner,42);++groups;}
 {Control c;const auto owner=c.owner();c.soft.Open(owner,200,110,100);c.soft.Amend(7,1,1,180,111,100);c.soft.Packet(c.begin(owner),112,true,true);assert(Fields(c.packets.back())[7]=="68"&&!c.hard);c.soft.Packet({"SOFT_SETTLED","1","7","1","1","1","P",StepWire("\"\"")},113,true,true);c.fence(owner,114);++groups;}
 {Control c;const auto owner=c.owner();c.soft.Open(owner,200,110,100);c.soft.Packet(c.begin(owner),111,true,true);c.soft.Cancel(7,1,1,"\"actual force retirement\"",112);assert(c.soft.Physical()==1&&c.packets.back().starts_with("SOFT_ABORT "));c.soft.Packet({"SOFT_SETTLED","1","7","1","1","1","E",StepWire("\"actual abort reason\"")},113,true,false);assert(!c.soft.Physical()&&Fields(c.packets.back())[6]=="0");c.fence(owner,114);++groups;}
 {for(const bool interrupted:{false,true}){std::vector<HookPhases::Acquire> acquire;std::vector<HookPhases::Invoke> invoke;std::vector<HookPhases::Release> release;std::vector<HookPhases::Result> results;HookPhases phase({[&](const auto& q){acquire.push_back(q);},[&](const auto& q){invoke.push_back(q);},[](const auto&){},[&](const auto& q){release.push_back(q);},[&](const auto& q){results.push_back(q);},[](const auto&){assert(false);}});assert(phase.Begin(1,1,{{HookKind::Body,1,{}},{HookKind::AfterEach,2,{}}},100,0));auto admit=[&](double now){const auto q=acquire.back();phase.Acquired(q.request,1,1,true,"",now);return invoke.back();};const auto body=admit(1);if(interrupted)phase.Cancel("actual global stop",20);else phase.Tick(100);assert(!phase.NoteSoftFailure(body.token,1,1,interrupted?21:101));const auto after=admit(interrupted?22:102);assert(after.kind==HookKind::AfterEach&&phase.ActiveDeadline()==(interrupted?120:200));assert(!phase.NoteSoftFailure(after.token+1,1,1,interrupted?23:103));assert(phase.NoteSoftFailure(after.token,1,1,interrupted?23:103));phase.Settled(body.token,1,1,false,"actual author abort",interrupted?24:104);phase.Settled(after.token,1,1,true,"",interrupted?25:105);const auto q=release.back();phase.Released(q.request,1,1,true,"",interrupted?26:106);assert(results.size()==1&&!results[0].ok&&results[0].infrastructure_failure&&results[0].observation.status==(interrupted?PhaseStatus::Interrupted:PhaseStatus::TimedOut));}++groups;}
 std::cout<<"PASS "<<groups<<" native original-cleanup/soft admission controls (controlled phase receipts)\n";
}
