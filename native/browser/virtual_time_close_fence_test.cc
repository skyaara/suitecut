// SPDX-License-Identifier: MIT
#include "virtual_time_close_fence.h"
#include "virtual_time.h"
#include "event_predicates.h"
#include "operation_registry.h"
#include <cassert>
#include <limits>
#include <string>
#include <vector>
using Fence=suitecut::NativeVirtualTimeCloseFence;
using Clock=suitecut::NativeVirtualTime;
int main(){
  {Fence f;assert(!f.NeedsTick()&&!f.Post(0));f.Request();assert(f.NeedsTick()&&f.Post(0));assert(!f.Post(0));assert(f.EnterPosted(1));assert(!f.Post(2)&&!f.EnterPosted(3)&&!f.NeedsTick());f.Closed();assert(!f.Post(4));}
  {
    Fence f;assert(f.RendererTerminated({{85,7,3000}},100));f.Request();
    assert(f.HeldOwners()==1&&!f.Post(101));f.Finished(85,6);assert(!f.Post(200));
    // A timestamp is not a real Complete/Cancel/original-deadline receipt.
    assert(!f.Post(3000)&&!f.Post(10000));f.Finished(85,7);
    assert(f.Post(10001)&&f.EnterPosted(10002));
  }
  {
    Fence f;assert(f.RendererTerminated({{1,11,0}},100));f.Request();
    assert(!f.Post(5099));
    // Later registration and repeated renderer notices cannot renew grace.
    assert(f.RendererTerminated({{1,12,0},{2,22,99999}},5000));
    assert(f.HeldOwners()==1);assert(f.Post(5100)&&f.EnterPosted(5100));
  }
  {
    Fence f;assert(f.RendererTerminated({{1,11,0},{2,22,120000}},0));f.Request();
    assert(!f.Post(5000));f.Finished(2,21);assert(!f.Post(120000));
    f.Finished(2,22);assert(f.Post(120001)&&f.EnterPosted(120001));
  }
  {
    Fence f;f.Request();assert(f.Post(0));
    // Actual renderer death may precede execution of an already posted close.
    assert(f.RendererTerminated({{1,1,4000}},1));assert(!f.EnterPosted(2));
    assert(!f.Post(3));f.Finished(1,1);assert(f.Post(4)&&f.EnterPosted(5));
  }
  {Fence f;f.Request();assert(f.Post(0));f.Closed();assert(!f.EnterPosted(1)&&!f.Post(2)&&!f.NeedsTick());}
  {Fence f;f.Request();assert(f.Post(0));f.PostRejected();assert(f.Post(1));f.Request();assert(!f.Post(2));assert(f.EnterPosted(3));}
  {
    Fence f;std::vector<Fence::Owner> owners;
    for(int i=1;i<=64;++i)owners.push_back({i,static_cast<uint64_t>(i),1000});
    assert(f.RendererTerminated(owners,0));assert(f.HeldOwners()==64);
    f.Request();for(int i=1;i<64;++i)f.Finished(i,static_cast<uint64_t>(i));
    assert(!f.Post(2000));f.Finished(64,64);assert(f.Post(2001));
  }
  {
    Fence f;std::vector<Fence::Owner> too_many;
    for(int i=1;i<=65;++i)too_many.push_back({i,static_cast<uint64_t>(i),1000});
    assert(!f.RendererTerminated(too_many,0));assert(!f.RendererTerminated({{1,1,1},{1,2,2}},0));
    assert(!f.RendererTerminated({{0,1,1}},0));assert(!f.RendererTerminated({{1,0,1}},0));
    assert(!f.RendererTerminated({{1,1,-1}},0));assert(!f.RendererTerminated({{1,1,std::numeric_limits<double>::infinity()}},0));
    assert(!f.RendererTerminated({{1,1,1}},std::numeric_limits<double>::quiet_NaN()));
    assert(f.HeldOwners()==0&&f.RendererTerminated({{1,1,2000}},0));
  }
  {
    // Reproduce the proven native ownership ordering with real controller
    // components. This is a CEF-free state seam, not a fabricated browser run.
    SuiteCutOperationRegistry operations;
    const auto operation=operations.Begin(85,0,3000);assert(operation);
    SuiteCutEventPredicateController predicates;
    assert(predicates.Start(85,operation->generation,"actual-client","crash",0));
    Fence fence;std::vector<Clock::Dispatch> inspector;std::vector<std::string> errors;
    Clock clock{{[&](const auto& call){inspector.push_back(call);},[](int,uint64_t,bool,const auto&){},[&](const auto& error){errors.push_back(error);fence.Request();}}};
    assert(clock.Begin({1,1,Clock::Action::InstallPaused,946684800000.,10000},0));
    clock.Acknowledge(inspector.back().token,true,1);clock.Acknowledge(inspector.back().token,true,2);
    assert(clock.Begin({2,2,Clock::Action::Advance,100,10000},3));const auto old_physical=inspector.back().token;
    assert(fence.RendererTerminated({{85,operation->generation,operation->deadline_ms}},10));
    clock.TargetChanged();assert(errors.size()==1&&clock.State().closing&&clock.State().physical);
    assert(predicates.Offer(85,operation->generation,1,"crash","{}")==SuiteCutEventPredicateController::Status::CandidateReady);
    assert(!fence.Post(11));
    const auto candidate=predicates.Pending(85,operation->generation);assert(candidate);const auto token=candidate->token;
    std::string matched;
    assert(predicates.Answer(85,operation->generation,token,"actual-client",true,matched)==SuiteCutEventPredicateController::Decision::Matched);
    assert(matched=="{}");assert(operations.Finish(85));fence.Finished(85,operation->generation);
    assert(fence.Post(12)&&fence.EnterPosted(13));
    // Accepted Inspector work is untouched until actual page destruction.
    clock.Acknowledge(old_physical,true,14,1000);assert(clock.State().physical);
    fence.Closed();clock.Closed();assert(!clock.State().physical);
  }
  {
    SuiteCutOperationRegistry operations;const auto operation=operations.Begin(85,0,3000);assert(operation);
    SuiteCutEventPredicateController predicates;assert(predicates.Start(85,operation->generation,"client","crash",0));
    Fence fence;assert(fence.RendererTerminated({{85,operation->generation,operation->deadline_ms}},1000));fence.Request();
    assert(predicates.Offer(85,operation->generation,1,"crash","{}")==SuiteCutEventPredicateController::Status::CandidateReady);
    const auto token=predicates.Pending(85,operation->generation)->token;std::string matched;
    assert(predicates.Answer(85,operation->generation,token,"client",false,matched)==SuiteCutEventPredicateController::Decision::Retry);
    assert(operations.Expired(2999).empty()&&!fence.Post(2999));
    const auto expired=operations.Expired(3000);assert(expired.size()==1&&expired[0]==85);
    assert(!fence.Post(3000));predicates.Cancel(85);assert(operations.Finish(85));fence.Finished(85,operation->generation);
    assert(fence.Post(3000)&&fence.EnterPosted(3001));
  }
}
