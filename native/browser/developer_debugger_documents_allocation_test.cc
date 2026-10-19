// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include "developer_debugger_documents.h"
namespace {long fail_after=-1;size_t allocations=0;}
[[gnu::noinline]] void*operator new(std::size_t size){
 if(fail_after>=0&&allocations++==static_cast<size_t>(fail_after))throw std::bad_alloc();
 if(void*memory=std::malloc(size?size:1))return memory;
 throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void*memory)noexcept{std::free(memory);}
[[gnu::noinline]] void operator delete(void*memory,std::size_t)noexcept{std::free(memory);}
[[gnu::noinline]] void*operator new[](std::size_t size){return ::operator new(size);}
[[gnu::noinline]] void operator delete[](void*memory)noexcept{::operator delete(memory);}
[[gnu::noinline]] void operator delete[](void*memory,std::size_t)noexcept{::operator delete(memory);}
using Documents=suitecut::FrameDebuggerDocuments;
using Scopes=suitecut::FrameDebuggerScopes;
int main(){size_t failures=0,groups=0;
 for(int operation=0;operation<4;++operation){bool completed=false;size_t operation_failures=0;
  for(long fault=0;fault<128&&!completed;++fault){
   Documents docs({std::string(64,'s'),1});std::string error;
   const Documents::Snapshot prior{"","root","actual-loader","actual-default",7};
   assert(docs.MainFrame("root",error)&&docs.DefaultCreated(prior,error));
   Scopes::Pin captured;assert(docs.Resolve("root",captured));
   const auto bytes=docs.Payload(),records=docs.Documents(),attachments=docs.Attachments();
   const std::string session(80,'s'),frame(180,'f');
   Documents::Snapshot incoming{"",operation==3?"root":frame,std::string(180,'l'),std::string(180,'u'),9};
   fail_after=fault;allocations=0;
   try{
    const bool ok=operation==0?docs.Attached(session,"",frame,error):operation==1?docs.MainFrame(frame,error):docs.DefaultCreated(incoming,error);
    fail_after=-1;assert(ok&&docs.Available());completed=true;
   }catch(const std::bad_alloc&){
    fail_after=-1;++operation_failures;++failures;
    // Failed observation cannot keep the old authority live. No metadata
    // capacity is published until its complete replacement node is available.
    assert(!docs.Available()&&!docs.Current(captured));
    assert(docs.Payload()==bytes&&docs.Documents()==records&&docs.Attachments()==attachments);
    docs.Closed();assert(!docs.Documents()&&!docs.Attachments());
   }
  }
  assert(completed&&operation_failures>=1);++groups;
 }
 std::cout<<"native observed document allocation "<<groups<<" groups / "<<failures<<" fault points passed\n";
}
