// SPDX-License-Identifier: MIT
// AUTHORING ONLY / UNCOMPILED. Runs actual native owner classes, not CEF.
#include "accepted_inspector_work.h"
#include "accepted_task_work.h"
#include <cassert>
#include <iostream>
using namespace suitecut;
int main(){
 size_t controls=0;
 AcceptedReaderWork reader;
 const auto command=reader.Admit();assert(command);
 const auto origin=NativeWorkOrigin::Command(17,*command);
 AcceptedInspectorWork inspector;
 const auto channel=inspector.Open(91,AcceptedInspectorWork::Kind::Page);assert(channel);
 std::optional<AcceptedInspectorWork::Ticket> raw;
 {
  const NativeWorkOriginFrame actual_route(origin);
  raw=inspector.Reserve(*channel,1,"session");assert(raw);
 }
 assert(inspector.ReplyOrigin(*channel,1,"session").kind==NativeWorkOrigin::Kind::Unknown);++controls;
 assert(inspector.Dispatch(*raw,"session"));
 assert(inspector.ReplyOrigin(*channel,1,"foreign").kind==NativeWorkOrigin::Kind::Unknown);++controls;
 assert(inspector.ReplyOrigin(*channel,1,"session")==origin);++controls;
 AcceptedTaskWork tasks;
 std::optional<AcceptedTaskWork::Ticket> delayed;
 {
  const NativeWorkOriginFrame unrelated_worker(NativeWorkOrigin::ResidentWorker(17));
  const NativeWorkOriginFrame authentic_raw_reply(inspector.ReplyOrigin(*channel,1,"session"));
  assert(inspector.ActualReply(*channel,1,"session")==AcceptedInspectorWork::Reply::Observed);
  delayed=tasks.Constructed(AcceptedTaskWork::Kind::Work);
  assert(delayed&&tasks.Origin(*delayed)==origin);
 }
 assert(inspector.Inspect().submitted==1); // actual send stack is still alive
 assert(inspector.SendReturned(*raw,true));assert(inspector.ReceiptLaneReady());++controls;
 assert(inspector.ReplyOrigin(*channel,1,"session").kind==NativeWorkOrigin::Kind::Unknown);++controls;
 assert(tasks.Inspect().queued==1);assert(tasks.Entered(*delayed));assert(tasks.Returned(*delayed,false));
 assert(tasks.Inspect().returned_held==1&&tasks.Origin(*delayed)==origin);++controls;
 assert(tasks.Destroyed(*delayed)&&!tasks.Origin(*delayed));++controls;
 {
  const NativeWorkOriginFrame actual_route(origin);
  AcceptedTaskReceipt task(AcceptedTaskWork::Kind::Work,tasks);
  {
   const NativeWorkOriginFrame unrelated_worker(NativeWorkOrigin::ResidentWorker(18));
   {
    const AcceptedTaskFrame actual_callback(task);
    assert(CurrentNativeWorkOrigin()==origin);
   }
   assert(CurrentNativeWorkOrigin()==NativeWorkOrigin::ResidentWorker(18));
  }
  assert(tasks.Inspect().returned_held==1);
 }
 assert(tasks.Inspect().queued==0&&tasks.Inspect().returned_held==0);++controls;
 // No current scope is never upgraded to Worker, no missing owner inferred.
 const auto unknown=inspector.Reserve(*channel,2,"");assert(unknown);
 assert(inspector.Dispatch(*unknown,""));assert(inspector.ReplyOrigin(*channel,2,"").kind==NativeWorkOrigin::Kind::Unknown);++controls;
 assert(inspector.ActualChannelClosed(*channel));assert(inspector.Inspect().submitted==1);
 assert(inspector.SendReturned(*unknown,true));assert(inspector.ReceiptLaneReady());++controls;
 assert(reader.NeverPosted(*command));
 std::cout<<"native origin integration controls "<<controls<<" PASS\n";
}
