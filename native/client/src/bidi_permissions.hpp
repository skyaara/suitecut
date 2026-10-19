// SPDX-License-Identifier: MIT
#pragma once
#include "bidi_runtime.hpp"
#include <memory>
namespace suitecut::detail {
// Native per-user-context/origin permission grants. Exact original entries are
// retained before dispatch because Firefox's permission store is persistent.
class BidiPermissions {
 public:
  BidiPermissions();
  ~BidiPermissions();
  Json command(BidiRuntime&,int sdk_context,const std::string& native_user_context,
               const std::string& action,const Json& params,Deadline);
  void forget(int sdk_context);
  void close(BidiRuntime&,Deadline);
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
