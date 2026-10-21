// SPDX-License-Identifier: MIT
#pragma once
#include "bidi_runtime.hpp"
#include <functional>
#include <memory>
namespace suitecut::detail {
// Observers only update bounded state. The backend's existing continuation
// worker calls drain; this helper never creates a thread or calls from observe.
class BidiHar {
 public:
  BidiHar();
  ~BidiHar();
  void set_wakeup(std::function<void()>);
  void setup(BidiRuntime&, BidiPage&, Deadline);
  void observe(const Json&, const std::string& owner_context);
  bool drain(BidiRuntime&, Deadline);
  Json command(BidiRuntime&, BidiPage&, const Json&, Deadline);
  void forget(const std::string& owner_context);
  void cleanup(BidiRuntime&, Deadline);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// Protocol conversion is separate so synthetic timing/redirect/binary cases
// can be tested without launching a browser.
Json bidi_har_entry(const Json& request_event, const Json& response_event,
                    const Json& request_body, const Json& response_body,
                    const std::string& body_error = {});
}
