// SPDX-License-Identifier: MIT
#pragma once
#include "cdp_internal.hpp"
#include <functional>
#include <memory>
namespace suitecut::detail {
struct CdpAutomatic { std::string method, session; Json params; };
// Bounded internal observer state; public CDP events remain in the wire queue.
// No observer method performs I/O or waits for a CDP response.
class CdpNetwork {
 public:
  explicit CdpNetwork(std::function<void(CdpAutomatic)>);
  ~CdpNetwork();
  void observe(const Json&);
  void setup(CdpRuntime&, CdpPage&, Deadline);
  void forget(const std::string& session);
  void forget_context(int);
  Json command(CdpRuntime&, CdpPage&, const Json&, Deadline);
  bool idle(const std::string&, std::chrono::milliseconds) const;
  Json events(const std::string&,std::uint64_t after_sequence=0) const;
  int execution_context(const std::string& session, const std::string& frame) const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
