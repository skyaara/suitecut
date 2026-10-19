// SPDX-License-Identifier: MIT
#include "attachment_mutations.h"
#include <cassert>
#include <iostream>

using namespace suitecut::runner;
template<class F>void RejectMutation(F&& action){bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}assert(rejected);}

int main(){
  unsigned controls=0;
  const StepOwner body{1,1,0,1,1,1,101,"body"};
  AttachmentDeclarations declarations;declarations.Open(body,1000,0);
  const auto pending=declarations.Declare(body,1,"\"pending bytes\"","\"text/plain\"",AttachmentDeclarations::Source::Body,std::nullopt,3,1);
  const auto path=declarations.Declare(body,2,"\"real authored \\ud800\"","\"image/png\"",AttachmentDeclarations::Source::PathReference,"\"missing\\ud800/file\"",std::nullopt,1);
  const auto absent=declarations.Declare(body,3,"\"absent\"","\"\"",AttachmentDeclarations::Source::Absent,std::nullopt,std::nullopt,1);
  AttachmentMutations::Budget budget;
  {
    AttachmentMutations journal(body,&budget);journal.Open(body,100,0);
    using Ref=AttachmentMutations::Reference;using Kind=AttachmentMutations::Kind;using Slots=std::vector<AttachmentMutations::Slot>;
    const auto a=journal.AdmitDeclaration(declarations,pending),b=journal.AdmitDeclaration(declarations,path),c=journal.AdmitDeclaration(declarations,absent);
    assert(journal.Projection().empty()&&journal.Revision()==0&&journal.References().size()==3);++controls;
    assert(journal.Apply(body,10,0,{a,b,c},2)==1&&journal.Projection()==Slots({a,b,c}));++controls;
    // Splice withdraws the visible body while its actual native declaration is
    // still pending. No physical receipt can be manufactured by this policy.
    journal.Apply(body,11,1,{b,c},3);
    assert(declarations.Find(pending)->receipt==AttachmentDeclarations::Receipt::Pending&&journal.Find(a)&&journal.Changes()[0].slots==Slots({a,b,c}));++controls;
    journal.Apply(body,12,2,{c,b},4);assert(journal.Projection()==Slots({c,b}));++controls;
    journal.Apply(body,13,3,{std::nullopt,b},5);
    assert(journal.Projection().size()==2&&!journal.Projection()[0]&&journal.Projection()[1]==b);++controls;
    journal.Apply(body,14,4,{a,a,c},6);assert(journal.Projection()==Slots({a,a,c}));++controls;
    journal.Apply(body,15,5,{},7);assert(journal.Projection().empty()&&journal.References().size()==3&&declarations.Records().size()==3);++controls;
    const auto revision=journal.Revision(),history=journal.Changes().size(),reserved=budget.metadata;
    RejectMutation([&]{journal.Apply(body,16,revision-1,{a},8);});
    RejectMutation([&]{journal.Apply(body,15,revision,{a},8);});
    RejectMutation([&]{journal.Apply(body,0,revision,{a},8);});
    assert(journal.Revision()==revision&&journal.Changes().size()==history&&budget.metadata==reserved);++controls;
    RejectMutation([&]{journal.Apply(body,16,revision,{Ref{Kind::Declaration,999}},8);});
    RejectMutation([&]{journal.Apply(body,16,revision,{Ref{Kind::Artifact,pending}},8);});
    RejectMutation([&]{journal.Apply(body,16,revision,{Ref{static_cast<Kind>(99),pending}},8);});++controls;
    auto foreign=body;foreign.attempt_generation=2;
    RejectMutation([&]{journal.Apply(foreign,16,revision,{a},8);});
    RejectMutation([&]{journal.Open(foreign,100,8);});
    foreign=body;foreign.pid=102;RejectMutation([&]{journal.Open(foreign,100,8);});++controls;
    RejectMutation([&]{journal.AdmitDeclaration(declarations,pending);});
    RejectMutation([&]{journal.AdmitDeclaration(declarations,999);});
    assert(journal.References().size()==3&&budget.metadata==reserved);++controls;
    const auto rejected=declarations.Declare(body,4,"\"native rejection\"","\"\"",AttachmentDeclarations::Source::Absent,std::nullopt,std::nullopt,8,"\"actual admission rejection\"");
    RejectMutation([&]{journal.AdmitDeclaration(declarations,rejected);});++controls;
    declarations.BindArtifact(body,pending,41);
    AttachmentRecord actual{41,1,body,"\"pending bytes\"","\"text/plain\"","bytes","/native/owned.dat",3,7,9,102,false,0,1,9,10,0};
    declarations.Committed(body,pending,actual);
    const std::vector<AttachmentRecord> native_artifacts{actual};
    const auto artifact=journal.AdmitArtifact(native_artifacts,41);
    assert(artifact!=a&&journal.Projection().empty()&&declarations.Find(pending)->artifact_id==41);
    RejectMutation([&]{journal.Apply(body,16,revision,{artifact,a},11);});
    journal.Publish(body,16,revision,native_artifacts,41,100,11);
    assert(journal.Find(artifact)->request==1&&journal.Find(artifact)->owner==body);++controls;
    auto forged=actual;forged.writer_exit=1;
    RejectMutation([&]{journal.AdmitArtifact({forged},41);});
    RejectMutation([&]{journal.AdmitArtifact(native_artifacts,99);});++controls;
    auto hook=body;hook.callback_token=5;hook.scope=2;hook.callback_kind="afterEach";
    journal.Open(hook,80,11);
    StepRecord step;step.id=8;step.owner=hook;step.status=StepStatus::Running;step.receipt=StepReceipt::Pending;step.deadline=70;
    journal.Apply(hook,17,journal.Revision(),{b,artifact},12,&step);
    assert(journal.Changes().back().owner==hook&&journal.Changes().back().step_id==8&&journal.Find(artifact)->owner==body);++controls;
    step.callback_received=true;RejectMutation([&]{journal.Apply(hook,18,journal.Revision(),{},13,&step);});
    step.callback_received=false;step.owner=body;RejectMutation([&]{journal.Apply(hook,18,journal.Revision(),{},13,&step);});++controls;
    const auto before_budget=budget.metadata;budget.metadata=AttachmentMutations::kMetadata;
    RejectMutation([&]{journal.Apply(hook,18,journal.Revision(),{a},13);});
    assert(journal.Changes().back().request==17&&journal.Projection()==Slots({b,artifact}));budget.metadata=before_budget;
    journal.Apply(hook,18,journal.Revision(),{a},13);++controls;
    Slots oversized(AttachmentMutations::kSlots+1,a);
    RejectMutation([&]{journal.Apply(hook,19,journal.Revision(),oversized,14);});
    RejectMutation([&]{journal.Apply(hook,19,journal.Revision(),{a},NAN);});
    RejectMutation([&]{journal.Apply(hook,19,journal.Revision(),{a},12);});++controls;
    journal.Amend(hook,20,14);RejectMutation([&]{journal.Apply(hook,19,journal.Revision(),{},20);});
    RejectMutation([&]{journal.Amend(hook,100,20);});++controls;
    journal.Close(body);journal.Retire(body);
    RejectMutation([&]{journal.Open(body,100,21);});
    RejectMutation([&]{journal.Apply(body,19,journal.Revision(),{},21);});
    assert(journal.Find(a)&&declarations.Find(pending)->receipt==AttachmentDeclarations::Receipt::Committed);++controls;
    journal.Close(hook);journal.Retire(hook);
    assert(budget.metadata==journal.ReservedMetadata());
  }
  assert(budget.metadata==0);++controls;
  // Failed/withdrawn native declarations remain the genuine original error.
  AttachmentDeclarations failed;failed.Open(body,100,0);
  const auto id=failed.Declare(body,1,"\"failure\"","\"x\"",AttachmentDeclarations::Source::Body,std::nullopt,1,1);
  AttachmentMutations failure(body);failure.Open(body,100,0);const auto ref=failure.AdmitDeclaration(failed,id);
  failure.Apply(body,1,0,{ref},2);failure.Apply(body,2,1,{},3);
  failed.Failed(body,id,"\"original native writer error \\ud800\"",4);
  assert(failed.Find(id)->error_json=="\"original native writer error \\ud800\""&&failed.Find(id)->receipt==AttachmentDeclarations::Receipt::Failed&&failure.Changes().front().slots.front()==ref);++controls;
  AttachmentMutations quota(body);quota.Open(body,10000,0);const auto q=quota.AdmitDeclaration(declarations,path);
  for(size_t index=0;index<AttachmentMutations::kChanges;++index)quota.Apply(body,index+1,index,{q},index+1);
  RejectMutation([&]{quota.Apply(body,999,quota.Revision(),{},999);});
  assert(quota.Changes().size()==AttachmentMutations::kChanges&&quota.Revision()==AttachmentMutations::kChanges);++controls;
  AttachmentMutations publication(body);publication.Open(body,100,0);publication.Close(body);
  AttachmentRecord published{99,8,body,"\"ordinary attach\"","\"x\"","bytes","/native/owned.dat",3,7,9,102,false,0,1,9,10,7};
  const std::vector<AttachmentRecord> committed{published};
  RejectMutation([&]{publication.Apply(body,1,0,{},11);});
  assert(publication.Publish(body,1,0,committed,99,100,11)==1&&publication.Changes().back().publication_artifact==99&&publication.Changes().back().step_id==7);++controls;
  RejectMutation([&]{publication.Publish(body,2,1,committed,99,100,12);});
  publication.Reject(body,2,1,"\"actual duplicate publication receipt\"",12,7);
  assert(publication.Rejections().size()==1&&publication.Revision()==1&&publication.Rejections().back().error_json=="\"actual duplicate publication receipt\"");++controls;
  auto late=published;late.id=100;late.received=100;
  RejectMutation([&]{publication.Publish(body,3,1,{late},100,100,100);});
  publication.Reject(body,3,1,"\"original deadline expired\"",100,7);
  assert(publication.Projection().size()==1&&publication.Rejections().size()==2);
  RejectMutation([&]{publication.Apply(body,3,1,{},101);});
  publication.Retire(body);RejectMutation([&]{publication.Publish(body,4,1,{late},100,100,101);});++controls;
  std::cout<<"PASS "<<controls<<" native attachment mutation projection/owner/deadline/history/quota controls; no file/process receipt claim\n";
}
