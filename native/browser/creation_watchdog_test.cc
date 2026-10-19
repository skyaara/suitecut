// SPDX-License-Identifier: MIT
// Real C++ task/capture ownership; no CEF scheduling or Engine proof.
#include "creation_watchdog.h"
#include <cassert>
#include <iostream>
#include <string>
using namespace suitecut;
using Work=AcceptedTaskWork;
using Watchdog=NativeCreationWatchdog;

static bool Ready(Work& work,uint64_t owner=17){
  AcceptedTaskReceipt barrier(Work::Kind::BarrierCoordinator,work);
  const AcceptedTaskFrame frame(barrier);
  return work.ReadyAtBarrier(CurrentNativeTask().ticket,owner);
}
struct Capture {
  Work& work;bool& destroyed;
  std::function<void()> on_destroy;
  Capture(Work& owner,bool& observed,std::function<void()> done):work(owner),destroyed(observed),on_destroy(std::move(done)){}
  ~Capture(){
    const auto physical=work.Inspect();
    assert(physical.queued+physical.executing+physical.returned_held>0);
    if(on_destroy)on_destroy();
    destroyed=true;
  }
};
static auto Captured(Work& work,bool& destroyed,std::function<void()> done={}){
  return std::make_shared<Capture>(work,destroyed,std::move(done));
}
int main(){
  size_t controls=0;
  const auto command=NativeWorkOrigin::Command(17,{3,4});
  // Completion really destroys the Source-like captures while work is charged;
  // the scheduler may continue holding the same now-inert state indefinitely.
  {
    Work work;bool destroyed=false;int calls=0;
    auto capture=Captured(work,destroyed);destroyed=false;
    auto queued=std::make_shared<Watchdog>([capture,&calls]{++calls;},command,work);
    capture.reset();assert(queued->Pending()&&!destroyed&&!Ready(work));++controls;
    assert(queued->Cancel()&&destroyed&&!queued->Pending()&&Ready(work));++controls;
    const auto after=work.Inspect();queued->Execute();queued->Execute();
    assert(calls==0&&!queued->Cancel()&&work.Inspect().admitted==after.admitted&&Ready(work));++controls;
  }
  // A genuinely still-unready watchdog fires once under its original command
  // ancestry; only actual callback return and capture destruction discharge it.
  {
    Work work;bool destroyed=false;int calls=0;
    auto capture=Captured(work,destroyed);destroyed=false;
    auto queued=std::make_shared<Watchdog>([capture,&calls,&work,command]{
      ++calls;assert(CurrentNativeWorkOrigin()==command);
      assert(work.Inspect().executing==1&&!Ready(work));
    },command,work);
    capture.reset();queued->Execute();assert(calls==1&&destroyed&&Ready(work));
    queued->Execute();assert(calls==1&&!work.Inspect().poisoned);++controls;
  }
  // Reentrant completion/cancellation cannot erase a running callback lease.
  {
    Work work;bool destroyed=false;std::shared_ptr<Watchdog> queued;
    auto capture=Captured(work,destroyed);destroyed=false;
    queued=std::make_shared<Watchdog>([capture,&queued,&work,&destroyed]{
      assert(!queued->Pending()&&!queued->Cancel()&&!destroyed);
      queued->Execute();assert(work.Inspect().executing==1&&!Ready(work));
    },command,work);
    capture.reset();queued->Execute();assert(destroyed&&Ready(work));++controls;
  }
  // Dropping a refused/unposted scheduler wrapper destroys its actual callback;
  // there is no strong Source->watchdog->Source cycle or invented execution.
  {
    Work work;bool destroyed=false;auto capture=Captured(work,destroyed);destroyed=false;
    auto queued=std::make_shared<Watchdog>([capture]{},command,work);
    std::weak_ptr<Watchdog> handle=queued;capture.reset();queued.reset();
    assert(handle.expired()&&destroyed&&work.Inspect().never_executed==1&&Ready(work));++controls;
  }
  {
    Work work;bool destroyed=false;
    struct SourceOwner {std::weak_ptr<Watchdog> watchdog;};
    auto source=std::make_shared<SourceOwner>();std::weak_ptr<SourceOwner> observed=source;
    auto capture=Captured(work,destroyed);destroyed=false;
    auto queued=std::make_shared<Watchdog>([source,capture]{},command,work);
    source->watchdog=queued;source.reset();capture.reset();assert(!observed.expired());
    assert(queued->Cancel()&&observed.expired()&&destroyed&&Ready(work));++controls;
  }
  // Capture destructors can reenter cancellation but cannot discharge the
  // receipt early; pending_.reset has already removed the actual callback.
  {
    Work work;bool destroyed=false;std::weak_ptr<Watchdog> handle;
    auto capture=Captured(work,destroyed,[&]{const auto actual=handle.lock();assert(actual&&!actual->Cancel()&&!Ready(work));});destroyed=false;
    auto queued=std::make_shared<Watchdog>([capture]{},command,work);handle=queued;
    capture.reset();assert(queued->Cancel()&&destroyed&&Ready(work));++controls;
  }
  // Exception unwinding retains captures through the real frame; the original
  // accepted-work poison is preserved rather than turning failure into idle.
  {
    Work work;bool destroyed=false;auto capture=Captured(work,destroyed);destroyed=false;
    auto queued=std::make_shared<Watchdog>([capture]{throw std::runtime_error("original watchdog failure");},command,work);
    capture.reset();try{queued->Execute();assert(false);}catch(const std::runtime_error& error){assert(std::string(error.what())=="original watchdog failure");}
    assert(destroyed&&work.Inspect().poisoned&&!Ready(work));++controls;
  }
  // Initial-document and settings monitors have independent ownership.
  {
    Work work;auto initial=std::make_shared<Watchdog>([]{},command,work),settings=std::make_shared<Watchdog>([]{},command,work);
    assert(initial->Cancel()&&!Ready(work)&&settings->Pending());
    assert(settings->Cancel()&&Ready(work));++controls;
  }
  // Only existing genuine resident-worker policy may ignore a pending resident
  // monitor; this owner does not reclassify Command/unknown/foreign origins.
  {
    Work work;auto resident=std::make_shared<Watchdog>([]{},NativeWorkOrigin::ResidentWorker(17),work);
    assert(Ready(work)&&resident->Pending()&&!Ready(work,18));
    auto unknown=std::make_shared<Watchdog>([]{},NativeWorkOrigin{},work);
    assert(!Ready(work));assert(unknown->Cancel()&&Ready(work));
    assert(resident->Cancel()&&Ready(work));++controls;
  }
  // Work posted by an actually firing watchdog inherits its exact command and
  // remains independently charged after the completed watchdog releases.
  {
    Work work;std::unique_ptr<AcceptedTaskReceipt> posted;
    const auto prior=NativeWorkOrigin::ResidentWorker(18);
    const NativeWorkOriginFrame outside(prior);
    auto queued=std::make_shared<Watchdog>([&]{posted=std::make_unique<AcceptedTaskReceipt>(Work::Kind::Work,work);},command,work);
    queued->Execute();assert(CurrentNativeWorkOrigin()==prior&&posted&&posted->Origin()==command&&!Ready(work));
    posted.reset();assert(Ready(work));++controls;
  }
  assert(controls==12);
  std::cout<<"Native creation watchdog: "<<controls<<" real callback/capture/receipt lifecycle controls passed (no CEF runtime claim)\n";
}
