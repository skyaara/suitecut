// SPDX-License-Identifier: MIT
#include "native_continuation.h"
#include <cassert>
#include <string>
#include <vector>
#include <iostream>
using namespace suitecut::runner;
struct Guard {int& count;~Guard(){++count;}};
NativeTask<int> Inner(NativeContinuation& context,std::vector<int>& order,int& retired){Guard guard{retired};order.push_back(1);co_await context.Yield(20);order.push_back(3);co_return 42;}
NativeTask<> Outer(NativeContinuation& context,std::vector<int>& order,int& retired){const auto value=co_await Inner(context,order,retired);assert(value==42);order.push_back(4);co_await context.Yield(30);order.push_back(5);}
NativeTask<> Failure(NativeContinuation& context){co_await context.Yield(10);throw std::runtime_error("actual native exception");}
NativeTask<> Catch(NativeContinuation& context,bool& observed){try{co_await Failure(context);}catch(const std::runtime_error& error){observed=std::string(error.what())=="actual native exception";}co_await context.Yield(40);}
int main(){
  NativeContinuation first,second;std::vector<int> order;int retired=0;auto a=Outer(first,order,retired);bool caught=false;try{a.Result();}catch(const std::logic_error&){caught=true;}assert(caught);
  a.Resume(first);assert(!a.Done()&&first.wake==20&&retired==0);order.push_back(2);a.Resume(first);assert(retired==1&&first.wake==30);a.Resume(first);assert(a.Done());a.Result();assert((order==std::vector<int>{1,2,3,4,5}));
  bool observed=false;auto b=Catch(second,observed);b.Resume(second);assert(!observed&&second.wake==10);b.Resume(second);assert(observed&&!b.Done()&&second.wake==40);b.Resume(second);b.Result();
  {NativeContinuation context;auto c=Failure(context);c.Resume(context);c.Resume(context);bool failed=false;try{c.Result();}catch(const std::runtime_error&){failed=true;}assert(failed);}
  {NativeContinuation context;auto c=Outer(context,order,retired);c.Resume(context);assert(retired==1);}assert(retired==2); // destroys suspended child chain once
  std::cout<<"Native continuation controls passed\n";
}
