// SPDX-License-Identifier: MIT
#include "retained_source_leases.h"
#include <cassert>
#include <iostream>
#include <limits>
using namespace suitecut::runner;
using Policy=RetainedSourceLeases;
// Controlled native receipt policy. No Source, OS process, IPC or browser
// result is fabricated/claimed by these literal receipt controls.
struct Fixture {
 Policy pool{10000};Policy::Owner owner{{1,1,101,91},{1,2,3,4,5}};
 Policy::Executable executable{11,12};Policy::Process process{201,101,93,executable};
 Policy::Ticket ticket=*pool.Reserve(owner,executable,100,0);
 Policy::Startup Startup()const{return {ticket,1,process,owner.host,process.pid};}
 void Start(){assert(pool.Started(Startup(),1));}
 Policy::HostRetirement Retirement()const{return {owner.host,9,true,true,true};}
};
int main(){size_t groups=0;const auto test=[&](auto action){Fixture f;action(f);++groups;};
 test([](Fixture& f){assert(f.pool.Outstanding()==1&&f.pool.LiveChildren()==0&&!f.pool.IdleReady(f.ticket));f.Start();assert(f.pool.LiveChildren()==1&&!f.pool.IdleReady(f.ticket));});
 test([](Fixture& f){auto s=f.Startup();++s.ticket.owner.fixture.generation;assert(!f.pool.Started(s,1));s=f.Startup();++s.ticket.owner.fixture.declaration;assert(!f.pool.Started(s,1));s=f.Startup();++s.ticket.owner.fixture.catalog;assert(!f.pool.Started(s,1));f.Start();assert(!f.pool.Started(f.Startup(),2));});
 test([](Fixture& f){auto s=f.Startup();++s.credential_pid;assert(!f.pool.Started(s,1));s=f.Startup();++s.process.parent;assert(!f.pool.Started(s,1));s=f.Startup();++s.observed_parent.start;assert(!f.pool.Started(s,1));s=f.Startup();++s.process.executable.inode;assert(!f.pool.Started(s,1));f.Start();});
 test([](Fixture& f){assert(!f.pool.Reserve({},f.executable,100,1));assert(!f.pool.Reserve(f.owner,{},100,1));assert(!f.pool.Reserve(f.owner,f.executable,10001,1));assert(!f.pool.Reserve(f.owner,f.executable,1,1));assert(!f.pool.Reserve(f.owner,f.executable,100,std::numeric_limits<double>::quiet_NaN()));});
 test([](Fixture& f){for(size_t i=1;i<Policy::kLive;++i){auto owner=f.owner;owner.fixture.token+=i;assert(f.pool.Reserve(owner,f.executable,100,1));}assert(!f.pool.Reserve(f.owner,f.executable,100,1));assert(f.pool.Outstanding()==8);f.pool.Stop(2);assert(f.pool.Outstanding()==8&&!f.pool.Reserve(f.owner,f.executable,100,3));});
 test([](Fixture& f){auto owner=f.owner;++owner.host.lease;assert(!f.pool.Reserve(owner,f.executable,100,1));});
 test([](Fixture& f){f.Start();auto owner=f.owner;++owner.fixture.token;const auto second=*f.pool.Reserve(owner,f.executable,100,2);auto s=f.Startup();s.ticket=second;assert(!f.pool.Started(s,3));++s.process.pid;++s.credential_pid;assert(f.pool.Started(s,3)&&f.pool.LiveChildren()==2);});
 test([](Fixture& f){f.pool.Expire(100);assert(f.pool.Outstanding()==1&&!f.pool.IdleReady(f.ticket));assert(f.pool.Started(f.Startup(),101));assert(f.pool.LiveChildren()==1&&!f.pool.RequestIdle(f.ticket,200,102));});
 test([](Fixture& f){f.Start();assert(!f.pool.Accepted(f.ticket,3,1,2));assert(f.pool.Accepted(f.ticket,2,1,2));assert(!f.pool.Accepted(f.ticket,3,1,3));assert(!f.pool.RequestIdle(f.ticket,100,3));assert(!f.pool.OperationRetired(f.ticket,3,2,3));assert(f.pool.OperationRetired(f.ticket,3,1,3));assert(!f.pool.OperationRetired(f.ticket,4,1,4));});
 test([](Fixture& f){f.Start();uint64_t emission=1;for(size_t i=1;i<=Policy::kAccepted;++i)assert(f.pool.Accepted(f.ticket,++emission,i,2));assert(!f.pool.Accepted(f.ticket,emission+1,65,3));assert(f.pool.Records()[0].pending==64);for(size_t i=Policy::kAccepted;i;--i)assert(f.pool.OperationRetired(f.ticket,++emission,i,4));assert(f.pool.Records()[0].pending==0);});
 test([](Fixture& f){f.Start();const auto idle=*f.pool.RequestIdle(f.ticket,100,2);assert(!f.pool.ObserveIdle({f.ticket,2,idle.nonce+1,0},3));assert(!f.pool.ObserveIdle({f.ticket,2,idle.nonce,1},3));assert(f.pool.ObserveIdle({f.ticket,2,idle.nonce,0},3));assert(f.pool.IdleReady(f.ticket)&&f.pool.LiveChildren()==1&&f.pool.Outstanding()==1);assert(!f.pool.ObserveIdle({f.ticket,3,idle.nonce,0},4));});
 test([](Fixture& f){f.Start();const auto idle=*f.pool.RequestIdle(f.ticket,100,2);assert(f.pool.Accepted(f.ticket,2,1,3));assert(!f.pool.ObserveIdle({f.ticket,3,idle.nonce,1},4));assert(f.pool.OperationRetired(f.ticket,3,1,4));assert(f.pool.ObserveIdle({f.ticket,4,idle.nonce,1},5)&&f.pool.IdleReady(f.ticket));assert(f.pool.Accepted(f.ticket,5,2,6)&&!f.pool.IdleReady(f.ticket));});
 test([](Fixture& f){f.Start();assert(!f.pool.RequestIdle(f.ticket,1003,2));const auto idle=*f.pool.RequestIdle(f.ticket,20,2);f.pool.Expire(20);assert(f.pool.ObserveIdle({f.ticket,2,idle.nonce,0},21));assert(!f.pool.IdleReady(f.ticket)&&f.pool.Records()[0].canceled_idle_ack&&f.pool.LiveChildren()==1);assert(!f.pool.ObserveIdle({f.ticket,3,idle.nonce,0},22));});
 test([](Fixture& f){f.Start();const auto idle=*f.pool.RequestIdle(f.ticket,20,2);f.pool.Stop(3);assert(f.pool.ObserveIdle({f.ticket,2,idle.nonce,0},4)&&!f.pool.IdleReady(f.ticket));});
 test([](Fixture& f){f.Start();const auto close=*f.pool.RequestClose(f.ticket,100,2);assert(!f.pool.RequestClose(f.ticket,101,3));assert(!f.pool.ObserveClose({f.ticket,2,close.nonce+1,0},3));assert(f.pool.ObserveClose({f.ticket,2,close.nonce,0},3));assert(f.pool.LiveChildren()==1&&f.pool.Outstanding()==1&&!f.pool.Retire(f.ticket,f.Retirement(),4));});
 test([](Fixture& f){f.Start();assert(f.pool.Accepted(f.ticket,2,1,2));const auto close=*f.pool.RequestClose(f.ticket,100,3);assert(!f.pool.ObserveClose({f.ticket,3,close.nonce,1},4));assert(f.pool.OperationRetired(f.ticket,3,1,4));assert(f.pool.ObserveClose({f.ticket,4,close.nonce,1},5));assert(f.pool.LiveChildren()==1);});
 test([](Fixture& f){f.Start();assert(f.pool.Accepted(f.ticket,2,1,2));f.pool.Stop(3);assert(f.pool.Records()[0].pending==1);auto foreign=f.process;++foreign.start;assert(!f.pool.ObservedExit(f.ticket,foreign,4));assert(f.pool.ObservedExit(f.ticket,f.process,4));assert(!f.pool.Accepted(f.ticket,3,2,5));assert(!f.pool.OperationRetired(f.ticket,3,1,5));assert(f.pool.Retire(f.ticket,f.Retirement(),5));assert(f.pool.Records()[0].pending==1&&!f.pool.Records()[0].close_ack&&f.pool.LiveChildren()==0&&f.pool.Outstanding()==0);});
 test([](Fixture& f){f.Start();assert(f.pool.ObservedExit(f.ticket,f.process,2));for(unsigned i=0;i<4;++i){auto r=f.Retirement();if(i==0)r.actual_waitpid=false;if(i==1)r.channel_drained=false;if(i==2)r.descendants_retired=false;if(i==3)++r.host.start;assert(!f.pool.Retire(f.ticket,r,3));}assert(f.pool.Retire(f.ticket,f.Retirement(),3));assert(!f.pool.Retire(f.ticket,f.Retirement(),4));assert(!f.pool.Started(f.Startup(),4));});
 test([](Fixture& f){f.pool.Stop(1);assert(f.pool.Retire(f.ticket,f.Retirement(),2));assert(f.pool.LiveChildren()==0&&f.pool.Outstanding()==0&&!f.pool.Started(f.Startup(),3));});
 test([](Fixture& f){f.Start();assert(!f.pool.RequestIdle(f.ticket,10,0));assert(!f.pool.Accepted(f.ticket,2,1,0));assert(!f.pool.ObservedExit(f.ticket,f.process,0));assert(!f.pool.Retire(f.ticket,f.Retirement(),0));});
 test([](Fixture& f){f.Start();f.pool.Expire(10000);assert(!f.pool.RequestIdle(f.ticket,10020,10001));const auto close=*f.pool.RequestClose(f.ticket,10020,10001);assert(f.pool.ObserveClose({f.ticket,2,close.nonce,0},10002)&&f.pool.LiveChildren()==1);});
 test([](Fixture& f){assert(f.pool.Retire(f.ticket,f.Retirement(),1));assert(!f.pool.Reserve(f.owner,f.executable,100,2));for(size_t i=1;i<Policy::kHistory;++i){auto owner=f.owner;owner.host.lease+=i;owner.host.worker+=static_cast<unsigned>(i);owner.host.pid+=static_cast<int64_t>(i);owner.host.start+=i;const auto ticket=*f.pool.Reserve(owner,f.executable,100,2);assert(f.pool.Retire(ticket,{owner.host,i+1,true,true,true},3));}assert(!f.pool.Reserve(f.owner,f.executable,100,4)&&f.pool.Records().size()==Policy::kHistory);});
 assert(groups==22);std::cout<<"Retained Source lease policy: "<<groups<<" controlled registration/accepted-work/idle/close/retirement groups passed; no Source/OS proof\n";
}
