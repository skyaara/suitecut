// SPDX-License-Identifier: MIT
#pragma once
#include "cdp_internal.hpp"
#include "cdp_network.hpp"
#include <functional>
#include <memory>

namespace suitecut::detail {
// The connection observer only enqueues acknowledgements and bounded JPEG work.
// One fixed worker writes source frames; encoders run only while stopping.
class CdpMedia {
 public:
  explicit CdpMedia(std::function<void(CdpAutomatic)>);
  ~CdpMedia();
  // True only for a recorder-owned frame consumed and acknowledged here.
  bool observe(const Json&);
  Json command(CdpRuntime&, CdpPage&, const Json&, Deadline);
  // Nonblocking cancellation may run before joining the command worker.
  void cancel_all();
  // Bounded best-effort protocol cleanup, before closing the wire.
  void shutdown(CdpRuntime&, Deadline);
  void forget(const std::string& session);
  void close();
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace suitecut::detail
