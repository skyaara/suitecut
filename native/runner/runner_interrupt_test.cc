// SPDX-License-Identifier: MIT
#include "runner_interrupt.h"
#include <cassert>
#include <iostream>
namespace {volatile std::sig_atomic_t called=0;void Previous(int)noexcept{called=1;}}
double Now(){struct timespec now{};assert(clock_gettime(CLOCK_MONOTONIC,&now)==0);return static_cast<double>(now.tv_sec)*1000+static_cast<double>(now.tv_nsec)/1000000;}
int main(){
  struct sigaction original{},previous{};previous.sa_handler=Previous;sigemptyset(&previous.sa_mask);assert(sigaction(SIGINT,&previous,&original)==0);
  {
    suitecut::runner::NativeInterruptLatch owner;assert(owner.Signal()==0);assert(!owner.TimeMilliseconds());
    bool duplicate=false;try{suitecut::runner::NativeInterruptLatch second;}catch(const std::logic_error&){duplicate=true;}assert(duplicate);
    const auto before=Now();errno=EDOM;assert(raise(SIGINT)==0);assert(errno==EDOM);const auto after=Now();
    assert(owner.Signal()==SIGINT);assert(owner.TimeMilliseconds());assert(*owner.TimeMilliseconds()>=before&&*owner.TimeMilliseconds()<=after);assert(!called);
    const auto first=*owner.TimeMilliseconds();assert(raise(SIGTERM)==0);assert(owner.Signal()==SIGINT);assert(*owner.TimeMilliseconds()==first);
  }
  assert(raise(SIGINT)==0);assert(called);assert(sigaction(SIGINT,&original,nullptr)==0);
  {suitecut::runner::NativeInterruptLatch fresh;assert(!fresh.Signal());assert(!fresh.TimeMilliseconds());}
  std::cout<<"Native real signal/time/errno/single-owner/restoration proof passed\n";
}
