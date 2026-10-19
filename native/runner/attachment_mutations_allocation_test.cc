// SPDX-License-Identifier: MIT
#include "attachment_mutations.h"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>

// Exercise real C++ allocation refusal at the native transaction boundary.
// No host sender, file callback or completion receipt is substituted.
static int64_t fail_after=-1;
[[gnu::noinline]] void* operator new(std::size_t size){
  if(fail_after==0){fail_after=-1;throw std::bad_alloc();}
  if(fail_after>0)--fail_after;
  if(auto* value=std::malloc(size?size:1))return value;
  throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* value)noexcept{std::free(value);}
[[gnu::noinline]] void operator delete(void* value,std::size_t)noexcept{std::free(value);}

int main(){
  using namespace suitecut::runner;
  const StepOwner owner{1,1,0,1,1,1,101,"body"};
  AttachmentMutations::Budget budget;
  for(int64_t point=0;point<4;++point){
    fail_after=point;bool failed=false;
    try{AttachmentMutations journal(owner,&budget);}catch(const std::bad_alloc&){failed=true;}
    fail_after=-1;assert(failed&&budget.metadata==0);
  }
  AttachmentDeclarations native;native.Open(owner,100,0);
  const auto id=native.Declare(owner,1,"\"actual accepted body declaration\"","\"x\"",AttachmentDeclarations::Source::Body,std::nullopt,3,1);
  {
    AttachmentMutations journal(owner,&budget);journal.Open(owner,100,0);
    const auto reference=journal.AdmitDeclaration(native,id);
    const std::vector<AttachmentMutations::Slot> proposed{reference};
    const auto reserved=budget.metadata;
    fail_after=0;bool failed=false;
    try{journal.Apply(owner,7,0,proposed,2);}catch(const std::bad_alloc&){failed=true;}
    fail_after=-1;
    assert(failed&&journal.Revision()==0&&journal.Projection().empty()&&journal.Changes().empty());
    assert(budget.metadata==reserved&&journal.References().size()==1&&native.Find(id)->receipt==AttachmentDeclarations::Receipt::Pending);
    assert(journal.Apply(owner,7,0,proposed,2)==1);
    journal.Apply(owner,8,1,{},3);
    assert(native.Find(id)->receipt==AttachmentDeclarations::Receipt::Pending&&journal.Changes().front().slots==proposed);
  }
  assert(budget.metadata==0);
  std::cout<<"PASS 5 real allocation-refusal transaction boundaries and exact retry; no physical receipt claim\n";
}
