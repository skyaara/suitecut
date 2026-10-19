// SPDX-License-Identifier: MIT
#include "accepted_task_work.h"
#include <cassert>
#include <iostream>
using namespace suitecut;
using Work=AcceptedTaskWork;
int main(){size_t controls=0;Work work;const auto worker=NativeWorkOrigin::ResidentWorker(17);const auto command=NativeWorkOrigin::Command(17,{3,4});
 const auto monitor=work.Constructed(Work::Kind::WorkerMonitor,worker),barrier=work.Constructed(Work::Kind::BarrierCoordinator);assert(monitor&&barrier&&work.Entered(*barrier));
 assert(work.ReadyAtBarrier(barrier,17)&&work.Inspect().queued==1&&work.Inspect().executing==1);++controls;
 assert(!work.ReadyAtBarrier(barrier)&&!work.ReadyAtBarrier(barrier,18));++controls;
 const auto test_watchdog=work.Constructed(Work::Kind::WorkerMonitor,command);assert(test_watchdog&&!work.ReadyAtBarrier(barrier,17));assert(work.Destroyed(*test_watchdog)&&work.ReadyAtBarrier(barrier,17));++controls;
 const auto unknown=work.Constructed(Work::Kind::WorkerMonitor,{});assert(unknown&&!work.ReadyAtBarrier(barrier,17));assert(work.Destroyed(*unknown));++controls;
 const auto foreign=work.Constructed(Work::Kind::WorkerMonitor,NativeWorkOrigin::ResidentWorker(18));assert(foreign&&!work.ReadyAtBarrier(barrier,17));assert(work.Destroyed(*foreign));++controls;
 const auto ordinary=work.Constructed(Work::Kind::Work,worker);assert(ordinary&&!work.ReadyAtBarrier(barrier,17));assert(work.Destroyed(*ordinary));++controls;
 assert(work.Entered(*monitor)&&!work.ReadyAtBarrier(barrier,17));assert(work.Returned(*monitor,false)&&work.ReadyAtBarrier(barrier,17)&&work.Inspect().returned_held==1);++controls;
 assert(work.Returned(*barrier,false)&&!work.ReadyAtBarrier(barrier,17));assert(work.Destroyed(*barrier)&&work.Inspect().returned_held==1);assert(work.Destroyed(*monitor)&&work.Inspect().returned_held==0&&!work.Inspect().poisoned);++controls;
 assert(controls==8);std::cout<<"Native worker-startup monitor: "<<controls<<" exact source/origin/real stack and physical-destruction controls passed (no CEF proof)\n";
}
