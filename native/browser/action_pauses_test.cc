// SPDX-License-Identifier: MIT
#include "action_pauses.h"
#include <cstdlib>
#include <memory>
#include <random>
#include <string>

static void Check(bool value) { if (!value) std::abort(); }
int main(int argc, char** argv) {
  const unsigned iterations = argc > 1 ? std::stoul(argv[1]) : 10000;
  SuiteCutActionPauseRegistry pauses;
  int resumed = 0;
  auto first = pauses.Pause(1,1,"first",[&] { ++resumed; });
  Check(first.has_value());
  Check(!pauses.Pause(1,2,"duplicate",[]{}));
  Check(!pauses.Take(1,2,*first,"first"));
  Check(!pauses.Take(1,1,*first+1,"first"));
  Check(!pauses.Take(1,1,*first,"wrong"));
  Check(pauses.Size()==1);
  auto continuation = pauses.Take(1,1,*first,"first");
  Check(continuation.has_value()); (*continuation)(); Check(resumed==1);
  Check(!pauses.Take(1,1,*first,"first"));
  Check(!pauses.Pause(0,1,"invalid",[]{}));
  Check(!pauses.Pause(1,0,"invalid",[]{}));
  Check(!pauses.Pause(1,1,"",[]{}));
  Check(!pauses.Pause(1,1,std::string(129,'x'),[]{}));
  for (int id=1;id<=64;++id) Check(pauses.Pause(id,id,"capacity",[]{}).has_value());
  Check(!pauses.Pause(65,65,"capacity",[]{}));
  pauses.Cancel(1); Check(pauses.Pause(65,65,"recovery",[]{}).has_value());
  pauses.Clear(); Check(pauses.Size()==0);
  auto owner = std::make_shared<int>(1); std::weak_ptr<int> weak = owner;
  Check(pauses.Pause(1,1,"lifetime",[owner] {}).has_value());
  owner.reset(); Check(!weak.expired()); pauses.Cancel(1); Check(weak.expired());
  std::mt19937_64 random(0x5a17c0de);
  for (unsigned i=0;i<iterations;++i) {
    const int id=static_cast<int>(random()%64)+1;
    const auto generation=static_cast<std::uint64_t>(i)+2;
    const auto token=pauses.Pause(id,generation,"fuzz",[&] { ++resumed; });
    Check(token.has_value());
    Check(!pauses.Take(id,generation-1,*token,"fuzz"));
    Check(!pauses.Take(id,generation,*token+1,"fuzz"));
    if (random()%2) {
      auto resume=pauses.Take(id,generation,*token,"fuzz"); Check(resume.has_value()); (*resume)();
      Check(!pauses.Take(id,generation,*token,"fuzz"));
    } else {
      pauses.Cancel(id); Check(!pauses.Take(id,generation,*token,"fuzz"));
    }
    auto reused=pauses.Pause(id,generation+1,"new",[]{}); Check(reused.has_value());
    Check(!pauses.Take(id,generation,*token,"fuzz")); Check(pauses.Contains(id));
    pauses.Cancel(id); Check(pauses.Size()==0);
  }
}
