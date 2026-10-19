// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "trace_phase_controls.h"
int main(){
  using Controls=suitecut::TracePhaseControls;using Lease=suitecut::TraceJournal::Lease;
  double now=1;uint64_t session=1;bool busy=true,synchronous=false;int completed=0,captured=0,errors=0;std::vector<Controls::Reply> physical;std::vector<double> deadlines;
  Controls::Hooks hooks;hooks.now=[&]{return now;};hooks.current=[&](Lease lease){return lease.session==session;};hooks.error=[&](Lease,const std::string&){++errors;};hooks.captured=[&](const auto&,auto result){assert(!result.png.empty());++captured;};
  hooks.read=[&](const Controls::Request& request,Controls::Reply reply){if(busy)return false;deadlines.push_back(request.deadline);if(synchronous)reply(true,{{1,2,3},{}},"");else physical.push_back(std::move(reply));return true;};
  const auto queue=std::make_shared<Controls>(hooks);auto submit=[&](double deadline=10){return queue->Submit({{1,session,1},0,1,"before",deadline,true,false},[&]{++completed;});};
  assert(submit());now=3;queue->Tick();assert(queue->Size()==1&&deadlines.empty());busy=false;queue->Tick();assert(deadlines==std::vector<double>{10});
  now=10;queue->Tick();assert(queue->Size()==0&&completed==1&&captured==0&&errors==1);
  // The Source still owns the issued real callback. Its late result may retire
  // physical resources, but the logical queue cannot publish it into a new job.
  now=11;assert(submit(30));physical[0](true,{{1,2,3},{}},"");assert(captured==0&&queue->Size()==1);physical[1](true,{{4,5,6},{}},"");assert(captured==1&&completed==2);
  assert(submit(30));++session;queue->Tick();assert(completed==3&&errors==1);physical[2](true,{{7,8,9},{}},"");assert(captured==1);
  synchronous=true;assert(submit(30));assert(completed==4&&captured==2&&queue->Size()==0);
  synchronous=false;assert(submit(30));now=31;physical.back()(true,{{1,2,3},{}},"");assert(errors==2&&captured==2&&completed==5);
  now=32;busy=true;for(int i=0;i<32;++i)assert(submit(50));assert(!submit(50));assert(queue->Size()==32);queue->Clear();assert(completed==37&&queue->Size()==0&&!submit(50));
}
