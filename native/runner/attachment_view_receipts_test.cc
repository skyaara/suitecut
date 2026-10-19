// SPDX-License-Identifier: MIT
#include "attachment_view_receipts.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void RejectViewReceipt(F&& action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){
  unsigned controls=0;const StepOwner owner{1,1,0,1,1,1,101,"body"};
  AttachmentRecord actual{17,3,owner,"\"real artifact fixture\"","\"x\"","bytes","/native/owned.dat",3,7,9,102,false,0,0,1,2,8};
  AttachmentViewReceipts::Budget budget;unsigned expired=0,closed=0;
  AttachmentViewReceipts receipts({[&](const auto& r){assert(r.expired&&!r.end);++expired;},[&](const auto& r){assert(r.expired&&r.close_requested&&!r.end);++closed;}},&budget);
  const auto ticket=receipts.Issue(actual,100,2);assert(ticket==1&&receipts.PendingCount()==1&&budget.pending==1&&receipts.PendingOwner(owner));++controls;
  auto foreign=owner;foreign.attempt_generation=2;
  RejectViewReceipt([&]{receipts.Pending(ticket,foreign,17);});
  RejectViewReceipt([&]{receipts.Pending(ticket,owner,18);});
  RejectViewReceipt([&]{receipts.Issue(actual,100,2);});assert(receipts.PendingCount()==1);++controls;
  receipts.Tick(99);assert(!expired&&!closed&&receipts.NeedsTick());
  receipts.Tick(100);receipts.Tick(101);assert(expired==1&&!closed&&receipts.PendingCount()==1&&receipts.PendingOwner(owner));++controls;
  receipts.Tick(6100);receipts.Tick(6200);
  assert(closed==1&&receipts.PendingCount()==1&&budget.pending==1&&!receipts.NeedsTick()&&!receipts.Find(ticket)->end);++controls;
  receipts.Acknowledge(ticket,owner,17,50,false,6201);
  assert(receipts.PendingCount()==0&&budget.pending==0&&receipts.Find(ticket)->end==AttachmentViewReceipts::End::Acknowledged&&!receipts.Find(ticket)->accepted&&receipts.Find(ticket)->expired);
  RejectViewReceipt([&]{receipts.Acknowledge(ticket,owner,17,51,true,6202);});++controls;
  auto second=actual;second.id=18;second.committed=6299;second.received=6300;
  const auto next=receipts.Issue(second,7000,6300);
  receipts.WorkerExited(2,101,6301);receipts.WorkerExited(1,103,6301);assert(receipts.PendingCount()==1);
  receipts.WorkerExited(1,101,6302);assert(receipts.PendingCount()==0&&receipts.Find(next)->end==AttachmentViewReceipts::End::WorkerExited&&!receipts.Find(next)->accepted);++controls;
  auto third=actual;third.id=19;third.committed=6399;third.received=6400;
  const auto success=receipts.Issue(third,7000,6400);
  assert(receipts.PendingStep(owner,8,[](uint64_t child,uint64_t parent){return child==parent;}));
  receipts.Acknowledge(success,owner,19,52,true,6401);
  assert(receipts.Find(success)->accepted&&!receipts.PendingStep(owner,8,[](uint64_t,uint64_t){return true;}));++controls;
  // Actual callback ingress may synchronously report genuine owner exit from
  // the close hook. Neither tick nor the close request invents that receipt.
  AttachmentViewReceipts* current=nullptr;
  AttachmentViewReceipts reentrant({[](const auto&){},[&](const auto& record){current->WorkerExited(record.owner.worker_index,record.owner.pid,6200);}});
  current=&reentrant;reentrant.Issue(actual,100,2);reentrant.Tick(6200);
  assert(reentrant.PendingCount()==0&&reentrant.Records()[0].end==AttachmentViewReceipts::End::WorkerExited);++controls;
  StepRecord actual_step;actual_step.id=actual.step_id;actual_step.owner=owner;actual_step.deadline=80;
  assert(AttachmentViewReceipts::OriginalDeadline(actual,100,&actual_step)==80);
  assert(AttachmentViewReceipts::OriginalDeadline(actual,50,&actual_step)==50);
  RejectViewReceipt([&]{AttachmentViewReceipts::OriginalDeadline(actual,100);});
  auto foreign_step=actual_step;foreign_step.owner.pid=999;
  RejectViewReceipt([&]{AttachmentViewReceipts::OriginalDeadline(actual,100,&foreign_step);});
  actual_step.deadline=actual.received;
  RejectViewReceipt([&]{AttachmentViewReceipts::OriginalDeadline(actual,100,&actual_step);});
  auto without_step=actual;without_step.step_id=0;
  assert(AttachmentViewReceipts::OriginalDeadline(without_step,100)==100);++controls;
  std::cout<<"PASS "<<controls<<" publication-view ACK/death/deadline controls; native metadata fixtures, no file or host-process receipt claim\n";
}
