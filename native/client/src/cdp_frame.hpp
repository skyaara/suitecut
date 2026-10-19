// SPDX-License-Identifier: MIT
#pragma once
#include "cdp_internal.hpp"
#include <memory>
namespace suitecut::detail {
// Selects real CDP frame execution worlds. The scope restores the caller's
// session/world without discarding document-local references or input state.
class CdpFrameScope {
 public:
  CdpFrameScope(CdpRuntime&,CdpPage&,Deadline);
  ~CdpFrameScope();
  CdpFrameScope(const CdpFrameScope&)=delete;
  void enter_owner(const std::string& object_id,Deadline);
  void enter_frame(const std::string& frame_id,Deadline);
  Json map_point(double x,double y,bool force,Deadline);
  Json input(const Json&,Deadline);
  Json call_root(const std::string&,Json,Deadline);
  void insert_text(const std::string&,Deadline);
  bool selected() const;
  void refresh(Deadline);
  void restore_root();
  std::string root_session() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
// The query resolver lives in cdp_dom.cpp and uses the same authored query
// engine as locators, including semantic/shadow/nth/ref frame-owner queries.
std::unique_ptr<CdpFrameScope> cdp_resolve_frame(CdpRuntime&,CdpPage&,const Json&,Deadline);
} // namespace suitecut::detail
