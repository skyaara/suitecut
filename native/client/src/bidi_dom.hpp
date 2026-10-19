// SPDX-License-Identifier: MIT
#pragma once
#include "bidi_runtime.hpp"
#include <memory>
namespace suitecut::detail {
// Resolves frame owners through real BiDi WindowProxy context identities.
// Sandboxes and document references stay scoped to the selected frame.
class BidiFrameScope {
 public:
  BidiFrameScope(BidiRuntime&,BidiPage&);
  ~BidiFrameScope();
  BidiFrameScope(const BidiFrameScope&)=delete;
  void enter_owner(Json,Deadline);
  void enter_frame(const std::string&,Deadline);
  Json map_point(double,double,bool,Deadline);
  bool ancestors_visible(Deadline);
  Json input(const Json&,Deadline);
  void restore_root();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
std::unique_ptr<BidiFrameScope> bidi_resolve_frame(BidiRuntime&,BidiPage&,const Json&,Deadline);
Json bidi_dom_command(BidiRuntime&,BidiPage&,const Json&,Deadline);
}
