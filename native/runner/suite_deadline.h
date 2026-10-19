// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
namespace suitecut::runner {
// Native monotonic suite budget. Expiration requests real cleanup; it never
// represents an OS signal or releases a physical callback/process lease.
class NativeSuiteDeadline {
 public:
  NativeSuiteDeadline(uint64_t timeout,double start):timeout_(timeout){
    if(timeout>2147483647||!std::isfinite(start))throw std::invalid_argument("Invalid native suite global timeout");
    if(timeout)deadline_=start+static_cast<double>(timeout);
  }
  bool Observe(double now){
    if(!std::isfinite(now))throw std::invalid_argument("Invalid native suite clock observation");
    if(deadline_&&now>=*deadline_)expired_=true;
    return expired_;
  }
  bool Expired()const{return expired_;}
  std::optional<double> OriginalDeadline()const{return deadline_;}
  std::optional<double> Time()const{return expired_?deadline_:std::nullopt;}
  std::string Reason()const{return "Native suite original global timeout of "+std::to_string(timeout_)+"ms exceeded";}
 private:
  uint64_t timeout_;std::optional<double>deadline_;bool expired_=false;
};
}
