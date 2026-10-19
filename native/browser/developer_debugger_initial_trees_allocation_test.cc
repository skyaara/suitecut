// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <new>
#include "developer_debugger_initial_trees.h"
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
int main(){size_t groups=0,failures=0;
 for(int operation=0;operation<3;++operation){bool completed=false;size_t operation_failures=0;
  for(long fault=0;fault<128&&!completed;++fault){
   suitecut::FrameDebuggerInitialTrees trees;std::string error;
   assert(trees.Prepare(10,"",1,error)&&trees.DefaultObserved("",1,"root",31,error)&&trees.Dispatching(10,"",1));
   const auto bytes=trees.Payload(),count=trees.Trees(),rows=trees.Frames();
   const std::string session(100,'s'),frame(200,'f');
   fail_after=fault;allocations=0;
   try{
    const bool ok=operation==0?trees.Prepare(20,session,2,error):operation==1?trees.DefaultObserved("",1,frame,32,error):trees.FrameInvalidated("",1,frame,error);
    fail_after=-1;assert(ok&&trees.Available());completed=true;
   }catch(const std::bad_alloc&){
    fail_after=-1;++operation_failures;++failures;
    assert(!trees.Available()&&!trees.InitialDocument(10,"",1,"root"));
    assert(trees.Payload()==bytes&&trees.Trees()==count&&trees.Frames()==rows);
    trees.Closed();assert(!trees.Trees()&&!trees.Frames()&&!trees.Payload());
   }
  }
  assert(completed&&operation_failures>=1);++groups;
 }
 std::cout<<"native initial-tree allocation "<<groups<<" groups / "<<failures<<" fault points passed\n";
}
