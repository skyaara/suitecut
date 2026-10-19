// SPDX-License-Identifier: MIT
#include "trace_background.h"
#include <atomic>
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
using namespace std::chrono_literals;

namespace {
template<class Predicate> void Await(Predicate ready){
  const auto deadline=std::chrono::steady_clock::now()+2s;
  while(!ready()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  assert(ready());
}
void CheckBusyWorkers(size_t workers){
  suitecut::TraceBackground jobs(workers,8);
  const size_t limit=jobs.WorkerLimit();
  assert(limit==(workers?workers:std::min(size_t(8),suitecut::AutomaticWorkerCount())));
  assert(limit>0);assert(jobs.WorkerCount()==0);assert(jobs.PhysicalCount()==0);
  std::mutex ids_mutex;std::set<std::thread::id> first_ids,second_ids;
  std::atomic_size_t active=0,peak=0;
  auto wave=[&](std::set<std::thread::id>& ids){
    const auto previous_workers=jobs.WorkerCount();
    std::promise<void> release;auto gate=release.get_future().share();
    for(size_t i=0;i<8;++i){
      assert(jobs.Submit([&ids,&ids_mutex,&active,&peak,gate]{
        {std::lock_guard lock(ids_mutex);ids.insert(std::this_thread::get_id());}
        const auto count=++active;auto previous=peak.load();
        while(previous<count&&!peak.compare_exchange_weak(previous,count)){}
        gate.wait();--active;
      }));
      assert(jobs.WorkerCount()==std::max(previous_workers,std::min(i+1,limit)));
    }
    Await([&]{return active.load()==limit;});
    assert(jobs.WorkerCount()==limit);assert(jobs.PhysicalCount()==8);
    assert(!jobs.Submit([]{}));assert(peak==limit);
    release.set_value();Await([&]{return jobs.PhysicalCount()==0;});
    assert(jobs.WorkerCount()==limit);
  };
  wave(first_ids);wave(second_ids);
  assert(first_ids.size()==limit);assert(first_ids==second_ids);
}
struct Held{
  std::promise<void>* entered;std::shared_future<void> gate;
  ~Held(){entered->set_value();gate.wait();}
};
}
int main(){
  {suitecut::TraceBackground jobs;const auto policy=suitecut::AutomaticWorkerPolicy();
    assert(jobs.WorkerLimit()==policy.workers);assert(jobs.Capacity()==policy.capacity);
    assert(jobs.WorkerCount()==0);assert(jobs.Available());assert(jobs.WorkerStartFailures()==0);}
  // Automatic pools start empty, use their host budget, and reuse the same
  // workers across bursts. Explicit internal overrides allow deterministic
  // coverage of genuinely concurrent busy jobs even on a one-core host.
  CheckBusyWorkers(0);CheckBusyWorkers(2);
  {suitecut::TraceBackground jobs(4,1);assert(jobs.WorkerLimit()==1);assert(jobs.WorkerCount()==0);}
  try{suitecut::TraceBackground invalid(1,0);assert(false);}catch(const std::invalid_argument&){}

  std::promise<void> release;auto gate=release.get_future().share();std::promise<void> started;
  std::atomic_bool canceled=false;std::atomic_int finished=0;
  {
    suitecut::TraceBackground jobs(1,2);
    assert(jobs.Submit([&]{started.set_value();gate.wait();++finished;}));
    started.get_future().wait();
    // Accepted canceled queued jobs execute their actual native retirement path.
    assert(jobs.Submit([&]{assert(canceled.load());++finished;}));
    assert(jobs.PhysicalCount()==2);assert(!jobs.Submit([]{}));canceled=true;
    release.set_value();Await([&]{return jobs.PhysicalCount()==0;});assert(finished==2);
    assert(jobs.Submit([]{throw std::runtime_error("actual worker failure");}));
    Await([&]{return jobs.PhysicalCount()==0;});assert(jobs.Failures()==1);
  }
  // Captured native ownership and its handler receipt survive logical task
  // completion, and retire before the physical reservation is released.
  std::promise<void> destroying,destroy;auto destroy_gate=destroy.get_future().share();
  {
    suitecut::TraceBackground jobs(1,1);
    suitecut::NativeWorkOrigin origin;origin.locator_handler_invocation=suitecut::NextNativeLocatorHandlerInvocation(1000);
    assert(origin.locator_handler_invocation);
    auto held=std::make_shared<Held>();held->entered=&destroying;held->gate=destroy_gate;
    {
      suitecut::NativeWorkOriginFrame frame(origin);
      assert(jobs.Submit([owner=std::move(held),origin]{assert(suitecut::CurrentNativeWorkOrigin()==origin);}));
    }
    destroying.get_future().wait();
    assert(jobs.PhysicalCount()==1);assert(!jobs.Submit([]{}));
    assert(!suitecut::NativeLocatorHandlerAsyncWork::Ready(origin.locator_handler_invocation));
    destroy.set_value();Await([&]{return jobs.PhysicalCount()==0;});
    assert(suitecut::NativeLocatorHandlerAsyncWork::Ready(origin.locator_handler_invocation));
  }
  // Shutdown drains queued work and joins while its captured owner is still
  // retiring; it cannot return merely because the task body has completed.
  {
    auto jobs=std::make_unique<suitecut::TraceBackground>(1,2);
    std::promise<void> started,release,retiring,retire;auto gate=release.get_future().share();
    auto held=std::make_shared<Held>();held->entered=&retiring;held->gate=retire.get_future().share();
    std::atomic_int drained=0;
    assert(jobs->Submit([&]{started.set_value();gate.wait();++drained;}));started.get_future().wait();
    assert(jobs->Submit([owner=held,&drained]{++drained;}));held.reset();
    auto shutdown=std::async(std::launch::async,[owner=std::move(jobs)]()mutable{owner.reset();});
    assert(shutdown.wait_for(10ms)==std::future_status::timeout);
    release.set_value();assert(retiring.get_future().wait_for(2s)==std::future_status::ready);
    assert(drained==2);assert(shutdown.wait_for(10ms)==std::future_status::timeout);
    retire.set_value();assert(shutdown.wait_for(2s)==std::future_status::ready);shutdown.get();
  }
}
