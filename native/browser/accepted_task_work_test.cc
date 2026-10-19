// SPDX-License-Identifier: MIT
#include "accepted_task_work.h"
#include <cassert>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>
using Work=suitecut::AcceptedTaskWork;
struct OwnedTask {
 suitecut::AcceptedTaskReceipt receipt; // Must outlive the captured callback.
 std::function<void()> callback;
 OwnedTask(Work& work,std::function<void()> fn,Work::Kind kind=Work::Kind::Work):receipt(kind,work),callback(std::move(fn)){}
 void Execute(){const suitecut::AcceptedTaskFrame frame(receipt);callback();}
};
struct Captured {
 Work& work;bool& destroyed;
 Captured(Work& owner,bool& value):work(owner),destroyed(value){}
 ~Captured(){const auto count=work.Inspect();assert(count.queued+count.executing+count.returned_held>0);destroyed=true;}
};
int main(){
 size_t controls=0;auto work=std::make_shared<Work>();assert(work->Inspect().queued==0&&!work->ReadyAtBarrier(std::nullopt));++controls;
 {OwnedTask task(*work,[]{});assert(work->Inspect().queued==1&&work->Inspect().executing==0&&!work->ReadyAtBarrier(task.receipt.Ticket()));++controls;
 task.Execute();assert(work->Inspect().returned_held==1&&work->Inspect().executing==0&&work->Inspect().returned==1);++controls;
 }assert(work->Inspect().destroyed==1&&work->Inspect().returned_held==0&&work->Inspect().never_executed==0);++controls;
 bool captured_destroyed=false;{auto capture=std::make_shared<Captured>(*work,captured_destroyed);captured_destroyed=false;auto task=std::make_unique<OwnedTask>(*work,[capture]{});capture.reset();assert(!captured_destroyed);task->Execute();assert(!captured_destroyed&&work->Inspect().returned_held==1);task.reset();assert(captured_destroyed&&work->Inspect().returned_held==0);}++controls;
 {OwnedTask not_posted(*work,[]{});assert(work->Inspect().queued==1);}assert(work->Inspect().never_executed==1&&work->Inspect().queued==0);++controls;
 auto sibling=std::make_unique<OwnedTask>(*work,[]{});bool observed=false;{OwnedTask barrier(*work,[&]{const auto actual=suitecut::CurrentNativeTask();assert(actual.work==work.get()&&!work->ReadyAtBarrier(actual.ticket));sibling.reset();assert(work->ReadyAtBarrier(actual.ticket));observed=true;},Work::Kind::BarrierCoordinator);barrier.Execute();assert(observed&&!work->ReadyAtBarrier(barrier.receipt.Ticket()));}++controls;
 assert(!suitecut::CurrentNativeTask().work&&!suitecut::CurrentNativeTask().ticket);++controls;
 {OwnedTask ordinary(*work,[&]{assert(!work->ReadyAtBarrier(suitecut::CurrentNativeTask().ticket));});ordinary.Execute();}++controls;
 auto other=std::make_shared<Work>();const auto foreign=*other->Constructed(Work::Kind::BarrierCoordinator);assert(!work->Entered(foreign)&&work->Inspect().poisoned&&other->Inspect().queued==1);assert(other->Destroyed(foreign));++controls;
 auto nested=std::make_shared<Work>();bool nested_done=false;{OwnedTask outer(*nested,[&]{const auto before=suitecut::CurrentNativeTask();{OwnedTask inner(*nested,[&]{assert(suitecut::CurrentNativeTask().ticket!=before.ticket);nested_done=true;});inner.Execute();}assert(suitecut::CurrentNativeTask().ticket==before.ticket);});outer.Execute();}assert(nested_done&&nested->Inspect().destroyed==2&&!nested->Inspect().poisoned);++controls;
 auto throwing=std::make_shared<Work>();{OwnedTask task(*throwing,[]{throw std::runtime_error("original callback failure");});try{task.Execute();assert(false);}catch(const std::runtime_error& e){assert(std::string(e.what())=="original callback failure");}assert(throwing->Inspect().returned_held==1&&throwing->Inspect().poisoned);}assert(throwing->Inspect().destroyed==1&&!suitecut::CurrentNativeTask().work);++controls;
 auto duplicate=std::make_shared<Work>();{OwnedTask task(*duplicate,[]{});task.Execute();task.Execute();assert(duplicate->Inspect().poisoned);}++controls;
 auto destroyed=std::make_shared<Work>();const auto exact=*destroyed->Constructed(Work::Kind::Work);assert(destroyed->Destroyed(exact)&&!destroyed->Destroyed(exact)&&destroyed->Inspect().never_executed==1);++controls;
 auto late=std::make_shared<Work>();const auto stale=*late->Constructed(Work::Kind::Work);assert(late->Destroyed(stale));const auto current=*late->Constructed(Work::Kind::Work);assert(current!=stale&&!late->Destroyed(stale)&&late->Inspect().queued==1);assert(late->Destroyed(current));++controls;
 auto bad_stack=std::make_shared<Work>();const auto live=*bad_stack->Constructed(Work::Kind::Work);assert(bad_stack->Entered(live)&&bad_stack->Destroyed(live)&&bad_stack->Inspect().poisoned);++controls;
 auto bounds=std::make_shared<Work>();std::vector<Work::Ticket> tickets;for(size_t n=0;n<Work::kPending;++n)tickets.push_back(*bounds->Constructed(Work::Kind::Work));assert(!bounds->Constructed(Work::Kind::Work)&&bounds->Inspect().poisoned&&bounds->Inspect().queued==Work::kPending);for(const auto ticket:tickets)assert(bounds->Destroyed(ticket));assert(bounds->Inspect().queued==0&&bounds->Inspect().never_executed==Work::kPending);++controls;
 const auto before=suitecut::NativeProcessTaskWork().Inspect();assert(before.queued==0&&before.executing==0&&before.returned_held==0&&!suitecut::NativeTaskBarrierReady());++controls;
 {suitecut::AcceptedTaskReceipt receipt(Work::Kind::BarrierCoordinator);{suitecut::AcceptedTaskFrame frame(receipt);assert(suitecut::NativeTaskBarrierReady());}assert(!suitecut::NativeTaskBarrierReady());}assert(suitecut::NativeProcessTaskWork().Inspect().destroyed==before.destroyed+1);++controls;
 {suitecut::AcceptedTaskReceipt receipt;const suitecut::AcceptedTaskFrame frame(receipt);assert(!suitecut::NativeTaskBarrierReady());}++controls;
 // Only actual matching task rows block the handler-scoped observation;
 // the old global barrier still rejects unrelated live work.
 auto scoped=std::make_shared<Work>();suitecut::NativeWorkOrigin handler;handler.locator_handler_invocation=61;
 auto unrelated=std::make_unique<OwnedTask>(*scoped,[]{});
 assert(!scoped->LocatorHandlerReady(0)&&scoped->LocatorHandlerReady(61));++controls;
 std::unique_ptr<OwnedTask> tagged;
 {const suitecut::NativeWorkOriginFrame frame(handler);tagged=std::make_unique<OwnedTask>(*scoped,[&]{assert(!scoped->LocatorHandlerReady(61)&&suitecut::CurrentNativeWorkOrigin()==handler);});}
 assert(tagged->receipt.Origin()==handler&&!scoped->LocatorHandlerReady(61)&&scoped->LocatorHandlerReady(62));tagged->Execute();
 assert(!scoped->LocatorHandlerReady(61)&&scoped->Inspect().returned_held==1);tagged.reset();
 assert(scoped->LocatorHandlerReady(61)&&scoped->Inspect().queued==1);++controls;
 {OwnedTask coordinator(*scoped,[&]{assert(scoped->LocatorHandlerReady(61)&&!scoped->ReadyAtBarrier(suitecut::CurrentNativeTask().ticket));},Work::Kind::BarrierCoordinator);coordinator.Execute();}
 unrelated.reset();assert(scoped->LocatorHandlerReady(61));++controls;
 for(const auto kind:{Work::Kind::BarrierCoordinator,Work::Kind::WorkerMonitor}){
  const auto row=*scoped->Constructed(kind,handler);assert(!scoped->LocatorHandlerReady(61));
  assert(scoped->Entered(row)&&!scoped->LocatorHandlerReady(61)&&scoped->Returned(row,false)&&!scoped->LocatorHandlerReady(61));
  assert(scoped->Destroyed(row)&&scoped->LocatorHandlerReady(61));
 }++controls;
 {const suitecut::NativeWorkOriginFrame frame(handler);OwnedTask never_executed(*scoped,[]{});assert(!scoped->LocatorHandlerReady(61));}
 assert(scoped->LocatorHandlerReady(61));++controls;
 {const suitecut::NativeWorkOriginFrame frame(handler);suitecut::AcceptedTaskReceipt receipt;
  assert(!suitecut::NativeLocatorHandlerTasksReady(61)&&!suitecut::NativeLocatorHandlerTasksReady(0));
  {const suitecut::AcceptedTaskFrame actual(receipt);assert(!suitecut::NativeLocatorHandlerTasksReady(61));}
  assert(!suitecut::NativeLocatorHandlerTasksReady(61));
 }assert(suitecut::NativeLocatorHandlerTasksReady(61)&&!suitecut::NativeLocatorHandlerTasksReady(0));++controls;
 scoped->Unsupported();assert(!scoped->LocatorHandlerReady(61)&&!scoped->LocatorHandlerReady(62));++controls;
 // Reader tasks are genuinely constructed before callback authentication.
 // Late binding marks only the exact current live task, not a fresh receipt.
 auto binding=std::make_shared<Work>();
 {OwnedTask reader(*binding,[&]{const auto current=suitecut::CurrentNativeTask();const auto original=*binding->Origin(*current.ticket);
   assert(original.locator_handler_invocation==0&&binding->BindLocatorHandlerInvocation(*current.ticket,81));
   auto expected=original;expected.locator_handler_invocation=81;assert(binding->Origin(*current.ticket)==expected);
   assert(binding->BindLocatorHandlerInvocation(*current.ticket,81)&&!binding->LocatorHandlerReady(81));
   assert(!suitecut::BindCurrentNativeLocatorHandlerTask(81));
  });
  assert(binding->LocatorHandlerReady(81));reader.Execute();assert(!binding->LocatorHandlerReady(81)&&binding->Inspect().returned_held==1);
 }assert(binding->LocatorHandlerReady(81)&&!binding->Inspect().poisoned);++controls;
 auto queued_binding=std::make_shared<Work>();const auto queued_ticket=*queued_binding->Constructed(Work::Kind::Work);
 assert(!queued_binding->BindLocatorHandlerInvocation(queued_ticket,81)&&queued_binding->Inspect().poisoned&&queued_binding->Origin(queued_ticket)->locator_handler_invocation==0);
 assert(queued_binding->Destroyed(queued_ticket));++controls;
 auto returned_binding=std::make_shared<Work>();{OwnedTask reader(*returned_binding,[]{});reader.Execute();
  assert(!returned_binding->BindLocatorHandlerInvocation(*reader.receipt.Ticket(),81)&&returned_binding->Inspect().poisoned&&returned_binding->Origin(*reader.receipt.Ticket())->locator_handler_invocation==0);
 }++controls;
 auto conflicting=std::make_shared<Work>();{OwnedTask reader(*conflicting,[&]{const auto actual=*suitecut::CurrentNativeTask().ticket;
   assert(conflicting->BindLocatorHandlerInvocation(actual,81)&&!conflicting->BindLocatorHandlerInvocation(actual,82));
   assert(conflicting->Origin(actual)->locator_handler_invocation==81&&conflicting->Inspect().poisoned);
  });reader.Execute();}++controls;
 auto foreign_binding=std::make_shared<Work>();auto foreign_other=std::make_shared<Work>();const auto foreign_queued=*foreign_other->Constructed(Work::Kind::Work);
 {OwnedTask reader(*foreign_binding,[&]{assert(!foreign_binding->BindLocatorHandlerInvocation(foreign_queued,81));});reader.Execute();}
 assert(foreign_binding->Inspect().poisoned&&foreign_other->Origin(foreign_queued)->locator_handler_invocation==0&&foreign_other->Destroyed(foreign_queued));++controls;
 auto zero_binding=std::make_shared<Work>();{OwnedTask reader(*zero_binding,[&]{const auto actual=*suitecut::CurrentNativeTask().ticket;assert(!zero_binding->BindLocatorHandlerInvocation(actual,0)&&!zero_binding->BindLocatorHandlerInvocation(actual,81)&&zero_binding->Origin(actual)->locator_handler_invocation==0);});reader.Execute();}
 assert(zero_binding->Inspect().poisoned&&!suitecut::BindCurrentNativeLocatorHandlerTask(0)&&!suitecut::BindCurrentNativeLocatorHandlerTask(81));++controls;
 {suitecut::AcceptedTaskReceipt real_process_reader;
  {const suitecut::AcceptedTaskFrame actual(real_process_reader);assert(suitecut::BindCurrentNativeLocatorHandlerTask(81));
   const auto exact=suitecut::NativeProcessTaskWork().Origin(*real_process_reader.Ticket());assert(exact&&exact->kind==suitecut::NativeWorkOrigin::Kind::Unknown&&exact->source_lease==0&&exact->locator_handler_invocation==81);
   assert(!suitecut::NativeLocatorHandlerTasksReady(81));
  }assert(!suitecut::NativeLocatorHandlerTasksReady(81));
 }assert(suitecut::NativeLocatorHandlerTasksReady(81));++controls;
 auto nested_binding=std::make_shared<Work>();{OwnedTask outer_task(*nested_binding,[&]{const auto outer_ticket=*suitecut::CurrentNativeTask().ticket;
  OwnedTask inner_task(*nested_binding,[&]{assert(!nested_binding->BindLocatorHandlerInvocation(outer_ticket,81)&&nested_binding->Origin(outer_ticket)->locator_handler_invocation==0);});inner_task.Execute();
 });outer_task.Execute();}assert(nested_binding->Inspect().poisoned);++controls;
 // Viewer-only passive metadata timers retain their real task receipts. They
 // never relax the default Runner barrier or an executing/reentrant callback.
 auto passive=std::make_shared<Work>();
 {OwnedTask monitor(*passive,[&]{OwnedTask barrier(*passive,[&]{const auto actual=suitecut::CurrentNativeTask().ticket;assert(!passive->ReadyAtBarrier(actual,0,true));},Work::Kind::BarrierCoordinator);barrier.Execute();},Work::Kind::PassiveNetworkMonitor);
  auto inspect=[&]{OwnedTask barrier(*passive,[&]{const auto actual=suitecut::CurrentNativeTask().ticket;assert(!passive->ReadyAtBarrier(actual)&&passive->ReadyAtBarrier(actual,0,true));
    {OwnedTask physical(*passive,[]{});assert(!passive->ReadyAtBarrier(actual,0,true));physical.Execute();assert(!passive->ReadyAtBarrier(actual,0,true));}
    assert(passive->ReadyAtBarrier(actual,0,true));},Work::Kind::BarrierCoordinator);barrier.Execute();};
  inspect();monitor.Execute();inspect();
 }assert(!passive->Inspect().queued&&!passive->Inspect().returned_held&&!passive->Inspect().poisoned);++controls;
 {suitecut::AcceptedTaskReceipt monitor(Work::Kind::PassiveNetworkMonitor);
  {suitecut::AcceptedTaskReceipt coordinator(Work::Kind::BarrierCoordinator);const suitecut::AcceptedTaskFrame frame(coordinator);assert(!suitecut::NativeTaskBarrierReady()&&suitecut::NativeTaskBarrierReady(0,true));}
 }++controls;
 assert(controls==37);std::cout<<"Accepted native task work: "<<controls<<" C++ ownership/stack/captured-state controls passed; no CEF/global Engine idle proof\n";
}
