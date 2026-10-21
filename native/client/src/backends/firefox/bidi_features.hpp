// SPDX-License-Identifier: MIT
#pragma once
#include "bidi_runtime.hpp"
#include <memory>

namespace suitecut::detail {
// Event observation is bounded, performs no I/O, and does not consume poll().
class BidiFeatures {
 public:
  BidiFeatures();
  ~BidiFeatures();
  BidiFeatures(const BidiFeatures&)=delete;
  BidiFeatures& operator=(const BidiFeatures&)=delete;
  void observe(const Json&);
  void setup(BidiRuntime&,BidiPage&,Deadline);
  void forget(const std::string& browsing_context);
  void forget_context(int context_id);
  Json command(BidiRuntime&,BidiPage&,const Json&,Deadline);
  // Native overrides belong to the BiDi session and reset at session.end.
  // Call before ending that session; this method does no protocol/file I/O.
  void close(BidiRuntime&,Deadline);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
