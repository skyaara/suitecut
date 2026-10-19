// SPDX-License-Identifier: MIT
// Authored and UNCOMPILED. Actual Source classification/Engine barrier is absent.
#include "accepted_work_origin.h"
#include <cassert>
#include <iostream>
#include <stdexcept>
using namespace suitecut;
int main(){
 size_t controls=0;
 assert(CurrentNativeWorkOrigin().kind==NativeWorkOrigin::Kind::Unknown);++controls;
 assert(NativeWorkOrigin::Command(0,{1,1}).kind==NativeWorkOrigin::Kind::Unknown);++controls;
 assert(NativeWorkOrigin::Command(8,{0,1}).kind==NativeWorkOrigin::Kind::Unknown);++controls;
 assert(NativeWorkOrigin::Command(8,{1,0}).kind==NativeWorkOrigin::Kind::Unknown);++controls;
 assert(NativeWorkOrigin::ResidentWorker(0).kind==NativeWorkOrigin::Kind::Unknown);++controls;
 AcceptedReaderWork reader;
 const auto ticket=reader.Admit();assert(ticket);
 const auto origin=NativeWorkOrigin::Command(8,*ticket);
 NativeWorkContinuation continuation;
 {
  const NativeWorkOriginFrame command(origin);assert(CurrentNativeWorkOrigin()==origin);
  continuation=NativeWorkContinuation();
  {
   const auto worker=NativeWorkOrigin::ResidentWorker(8);const NativeWorkOriginFrame resident(worker);
   assert(CurrentNativeWorkOrigin()==worker);
  }
  assert(CurrentNativeWorkOrigin()==origin);
 }
 assert(CurrentNativeWorkOrigin().kind==NativeWorkOrigin::Kind::Unknown);++controls;
 {
  const NativeWorkOriginFrame worker(NativeWorkOrigin::ResidentWorker(8));
  {
   const NativeWorkOriginFrame authentic_callback(continuation.Origin());
   assert(CurrentNativeWorkOrigin()==origin);
  }
  assert(CurrentNativeWorkOrigin().kind==NativeWorkOrigin::Kind::ResidentWorker);
 }
 ++controls;
 try{const NativeWorkOriginFrame command(origin);throw std::runtime_error("actual callback unwind");}catch(const std::runtime_error&){}
 assert(CurrentNativeWorkOrigin().kind==NativeWorkOrigin::Kind::Unknown);++controls;
 const auto cut=reader.Cut();assert(reader.KnownCut(cut));
 assert(origin.PriorTo(8,reader,cut));++controls;
 assert(!origin.PriorTo(9,reader,cut));++controls;
 auto foreign=cut;++foreign.instance;assert(!origin.PriorTo(8,reader,foreign));++controls;
 auto forged=cut;++forged.nonce;assert(!origin.PriorTo(8,reader,forged));++controls;
 assert(!NativeWorkOrigin::ResidentWorker(8).PriorTo(8,reader,cut));++controls;
 const auto later=reader.Admit();assert(later);assert(!NativeWorkOrigin::Command(8,*later).PriorTo(8,reader,cut));++controls;
 // Attribution did not invent posting/route/full-write or physical retirement.
 assert(reader.Inspect().pending==2&&!reader.Through(cut));++controls;
 assert(reader.NeverPosted(*ticket));assert(reader.Through(cut));assert(reader.Inspect().pending==1);++controls;
 assert(reader.NeverPosted(*later));assert(reader.ReleaseCut(cut));
 // A standalone browser handler has no fixture/source lease. Its native
 // invocation tag must survive real nested continuation scopes without
 // granting Command/ResidentWorker or reader-cut authority.
 NativeWorkOrigin standalone;standalone.locator_handler_invocation=71;
 assert(standalone.kind==NativeWorkOrigin::Kind::Unknown&&!standalone.source_lease&&!standalone.command.instance&&!standalone.command.sequence);++controls;
 assert(standalone!=NativeWorkOrigin{}&&NativeWorkOrigin::Command(8,*ticket).locator_handler_invocation==0&&NativeWorkOrigin::ResidentWorker(8).locator_handler_invocation==0);++controls;
 {const NativeWorkOriginFrame frame(standalone);NativeWorkContinuation kept;assert(kept.Origin()==standalone);
  {const NativeWorkOriginFrame untagged(origin);assert(CurrentNativeWorkOrigin().locator_handler_invocation==0);}
  assert(CurrentNativeWorkOrigin()==standalone);
  try{const NativeWorkOriginFrame restored(kept.Origin());throw std::runtime_error("actual tagged scope unwind");}catch(const std::runtime_error&){}
  assert(CurrentNativeWorkOrigin()==standalone);
 }assert(CurrentNativeWorkOrigin().locator_handler_invocation==0);++controls;
 std::cout<<"native origin controls "<<controls<<" PASS\n";
}
