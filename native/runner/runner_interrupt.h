// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <string>
#include <limits>
#include <optional>
#include <signal.h>
#include <time.h>
namespace suitecut::runner {
namespace interrupt_detail {
inline volatile std::sig_atomic_t received=0;
inline volatile std::sig_atomic_t seconds=-1,nanos=0;
inline std::atomic_flag installed=ATOMIC_FLAG_INIT;
inline void Record(int signal)noexcept{
  if(received)return;
  const int previous_errno=errno;
  struct timespec now{};
  // POSIX clock_gettime is async-signal-safe. Only sig_atomic_t fields cross
  // this boundary; both interrupt signals are blocked while writing them.
  if(clock_gettime(CLOCK_MONOTONIC,&now)==0&&now.tv_sec>=0&&now.tv_sec<=std::numeric_limits<std::sig_atomic_t>::max()){
    seconds=static_cast<std::sig_atomic_t>(now.tv_sec);nanos=static_cast<std::sig_atomic_t>(now.tv_nsec);
  }
  received=signal;
  errno=previous_errno;
}
}
// The handler records one actual OS signal only. All callback cancellation,
// physical worker teardown and report work runs in the native scheduler.
class NativeInterruptLatch {
 public:
  NativeInterruptLatch(){
    if(interrupt_detail::installed.test_and_set())throw std::logic_error("Native runner signal owner already exists");
    interrupt_detail::received=0;
    interrupt_detail::seconds=-1;interrupt_detail::nanos=0;
    struct sigaction action{};action.sa_handler=interrupt_detail::Record;sigemptyset(&action.sa_mask);sigaddset(&action.sa_mask,SIGINT);sigaddset(&action.sa_mask,SIGTERM);
    if(sigaction(SIGINT,&action,&previous_int_)<0){interrupt_detail::installed.clear();throw std::runtime_error("Native runner SIGINT handler installation failed");}
    if(sigaction(SIGTERM,&action,&previous_term_)<0){sigaction(SIGINT,&previous_int_,nullptr);interrupt_detail::installed.clear();throw std::runtime_error("Native runner SIGTERM handler installation failed");}
  }
  ~NativeInterruptLatch(){sigaction(SIGINT,&previous_int_,nullptr);sigaction(SIGTERM,&previous_term_,nullptr);interrupt_detail::installed.clear();}
  NativeInterruptLatch(const NativeInterruptLatch&)=delete;
  NativeInterruptLatch& operator=(const NativeInterruptLatch&)=delete;
  int Signal()const noexcept{return interrupt_detail::received;}
  std::optional<double> TimeMilliseconds()const noexcept{if(!Signal()||interrupt_detail::seconds<0)return std::nullopt;return static_cast<double>(interrupt_detail::seconds)*1000+static_cast<double>(interrupt_detail::nanos)/1000000;}
  std::string Reason()const{return Signal()==SIGINT?"Native runner interrupted by actual SIGINT":Signal()==SIGTERM?"Native runner interrupted by actual SIGTERM":"Native runner has no recorded interrupt";}
 private:struct sigaction previous_int_{},previous_term_{};
};
}
