// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <vector>
namespace suitecut::runner {
// Owned by one mutable matching budget, never by an immutable compiled pattern.
// NFA state capacity is2048; retained vectors grow only to that pattern bound.
struct GrepNFAScratch {
  std::vector<size_t> current,next,stack;
  std::vector<uint32_t> seen;
  bool active=false;
};
struct GrepWork {
  uint64_t remaining=64*1024*1024;
  // Runner installs the original suite deadline/interrupt check. Pure policy
  // callers may leave it empty; no clock, timeout or cancellation is invented.
  std::function<void()> check_stopped={};
  uint64_t until_poll=4096;
  GrepNFAScratch nfa_scratch={};
  void Charge(uint64_t amount=1){
    if(check_stopped){
      if(amount>=until_poll){until_poll=4096-((amount-until_poll)%4096);check_stopped();}
      else until_poll-=amount;
    }
    if(amount>remaining){remaining=0;throw std::invalid_argument("Native grep match work capacity64Mi exceeded");}
    remaining-=amount;
  }
};
}
