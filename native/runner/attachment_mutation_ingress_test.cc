// SPDX-License-Identifier: MIT
#include "runner_attachments.h"
#include <cassert>
#include <iostream>
using namespace suitecut::runner;
template<class F>void RejectMutationIngress(F&& action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}assert(rejected);}
int main(){
  unsigned controls=0;const auto now=DedicatedRealMilliseconds();
  const StepOwner body{1,1,0,1,1,1,static_cast<int64_t>(getpid()),"body"};
  std::vector<std::string> sent,failures;
  RunnerAttachments::Budget budget;
  RunnerAttachments native("/tmp",{[&](const auto& packet){sent.push_back(packet);},[&](const auto& error){failures.push_back(error);},{},[&](const auto&,const auto& error){failures.push_back(error);}},&budget);
  native.BeginCallback(body,now+10000,now);
  assert(native.Packet({"ATTACH_MUTATE","1","0","1","1","0","0","S","0","-"},now+1,false));
  assert(sent.back()=="ATTACH_MUTATION_RESULT 1 0 1 1 1 1 -"&&native.Physical()==0);++controls;
  const auto name=StepWire("\"exact native reference \\ud800\""),type=StepWire("\"x\""),path=StepWire("\"does/not/exist\\ud800\"");
  native.Packet({"ATTACH_APPEND_BEGIN","2","0","1","1",name,"R","0",type,path},now+2,false);
  native.Packet({"ATTACH_MUTATE","3","0","1","1","1","0","S","0","d2"},now+3,false);
  assert(native.Declarations().size()==1&&native.Declarations()[0].author_path_json&&StepWire(*native.Declarations()[0].author_path_json)==path&&native.Records().empty()&&native.Physical()==0);++controls;
  native.Packet({"ATTACH_MUTATE","4","0","1","1","2","0","S","0","h,d2,d2"},now+4,false);
  assert(sent.back()=="ATTACH_MUTATION_RESULT 4 0 1 1 3 1 -");
  native.Packet({"ATTACH_MUTATE","5","0","1","1","3","0","S","0","-"},now+5,false);
  assert(native.Declarations().size()==1&&native.Declarations()[0].receipt==AttachmentDeclarations::Receipt::ReferenceAccepted);++controls;
  RejectMutationIngress([&]{native.ForEachAttachmentProjection([](const auto&){});});
  bool returned=false;native.FenceWithAppendFailure(0,1,1,[&](const auto& error){assert(!error);returned=true;},now+6);
  assert(returned&&native.IdleReady()&&native.Physical()==0&&failures.empty());
  size_t journals=0;
  native.ForEachAttachmentProjection([&](const auto& journal){
    ++journals;assert(journal.Revision()==4&&journal.Projection().empty()&&journal.Changes()[2].slots.size()==3&&!journal.Changes()[2].slots[0]&&journal.References().size()==1);
  });assert(journals==1);++controls;
  auto hook=body;hook.callback_token=9;hook.scope=2;hook.callback_kind="afterEach";
  native.BeginCallback(hook,now+10000,now+7);
  native.Packet({"ATTACH_MUTATE","6","9","2","1","4","0","S","0","d2"},now+8,false);
  native.Packet({"ATTACH_MUTATE","7","9","2","1","5","0","S","0","d999"},now+9,false);
  assert(sent.back().starts_with("ATTACH_MUTATION_RESULT 7 9 2 1 5 0 "));
  std::optional<std::string> native_error;
  native.FenceWithAppendFailure(9,2,1,[&](const auto& error){native_error=error;},now+10);
  assert(native_error&&native_error->find("no actual declaration request")!=std::string::npos&&native.IdleReady());
  native.ForEachAttachmentProjection([&](const auto& journal){assert(journal.Revision()==5&&journal.Rejections().size()==1&&journal.Rejections()[0].owner==hook&&journal.Projection().size()==1);});++controls;
  // Closed author admission rejects new authored changes but preserves both
  // the old accepted projection and that real native failure on its callback.
  auto closing=body;closing.callback_token=10;closing.scope=3;closing.callback_kind="afterEach";
  native.BeginCallback(closing,now+10000,now+11);native.CloseAdmission(10,3,1);
  native.Packet({"ATTACH_MUTATE","8","10","3","1","5","0","S","0","-"},now+12,false);
  native.FenceWithAppendFailure(10,3,1,[&](const auto& error){assert(error&&error->find("callback closed")!=std::string::npos);},now+13);
  assert(native.IdleReady()&&native.Physical()==0&&native.Declarations().size()==1&&native.Records().empty());++controls;
  assert(budget.physical==0&&budget.accepted==0&&budget.declarations.records==1&&budget.mutations.metadata>0&&budget.mutations.metadata<=AttachmentMutations::kMetadata);++controls;
  auto later_body=body;later_body.scope=4;native.BeginCallback(later_body,now+10000,now+14);
  native.Packet({"ATTACH_MUTATE","9","0","4","1","5","0","S","0","d999"},now+15,false);
  assert(!native.CanAcceptTestSkip(0,4,1,10,now+16));
  native.FenceWithAppendFailure(0,4,1,[&](const auto& error){assert(error&&error->find("no actual declaration request")!=std::string::npos);},now+17);
  assert(native.IdleReady());++controls;
  std::cout<<"PASS "<<controls<<" actual included native attachment mutation ingress controls; no writer/host child process started\n";
}
