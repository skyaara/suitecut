// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include "operation_registry.h"

struct Host {
  SuiteCutOperationRegistry operations_;
  std::map<int, std::string> expect_results_;
  double now = 0;
  int publications = 0;
  bool ok = false;
  std::string published, error;
  double Now() const { return now; }
  void Complete(int id, bool success, const std::string& cause, const std::string& json) {
    assert(operations_.Finish(id));
    ++publications; ok = success; error = cause; published = json;
    expect_results_.erase(id);
  }
  uint64_t Begin(double timeout = 40) {
    const auto admitted = operations_.Begin(1, now, timeout);
    assert(admitted); return admitted->generation;
  }
#include "expectation_deadline.inc"
};

int main() {
  const std::string original = R"({"result":{"value":{"matches":false,"actual":["primitive","actual"],"count":1}}})";
  // Actual Source seam: the mismatch was admitted before the original expiry;
  // selector cleanup ACK/retry arrives afterward, before the next timer tick.
  { Host host; const auto generation=host.Begin(); host.now=30;
    host.expect_results_[1]=original; host.now=41;
    assert(host.CompleteExpiredExpectation(1,generation));
    assert(host.ok && host.publications==1 && host.published==original && host.error.empty());
    assert(host.expect_results_.empty() && !host.operations_.Size()); }
  // An expired callback must retain the earlier observation, never replace it
  // with a response that arrived after the deadline.
  { Host host; const auto generation=host.Begin(); host.expect_results_[1]=original;
    host.now=40; const std::string late=R"({"matches":true})";
    assert(host.CompleteExpiredExpectation(1,generation));
    assert(host.published==original && host.published!=late); }
  { Host host; const auto generation=host.Begin(); host.now=41;
    assert(!host.CompleteExpiredExpectation(1,generation));
    assert(host.operations_.IsCurrent(1,generation) && !host.publications && host.expect_results_.empty()); }
  { Host host; const auto generation=host.Begin(); host.expect_results_[1]=original; host.now=39;
    assert(!host.CompleteExpiredExpectation(1,generation));
    assert(!host.publications && host.expect_results_.at(1)==original); }
  { Host host; const auto generation=host.Begin(); host.expect_results_[1]=original;
    assert(host.operations_.Finish(1)); host.now=41;
    assert(!host.CompleteExpiredExpectation(1,generation) && !host.publications); }
  { Host host; const auto old=host.Begin(); assert(host.operations_.Finish(1));
    host.now=50; const auto fresh=host.Begin(); host.expect_results_[1]=original; host.now=91;
    assert(!host.CompleteExpiredExpectation(1,old));
    assert(host.operations_.IsCurrent(1,fresh) && !host.publications);
    assert(host.CompleteExpiredExpectation(1,fresh) && host.published==original); }
  { Host host; const auto generation=host.Begin(0); host.now=1e9; host.expect_results_[1]=original;
    assert(!host.CompleteExpiredExpectation(1,generation) && !host.publications); }
  { Host host; const auto generation=host.Begin(); host.expect_results_[1]=original; host.now=50;
    assert(host.CompleteExpiredExpectation(1,generation));
    assert(!host.CompleteExpiredExpectation(1,generation) && host.publications==1); }
  std::cout << "PASS8 actual Source cached-expectation deadline publication controls\n";
}
