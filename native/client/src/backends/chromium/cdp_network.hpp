// SPDX-License-Identifier: MIT
#pragma once
#include "cdp_internal.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>
namespace suitecut::detail {
struct CdpAutomatic {
  std::string method, session;
  Json params;
};
// Internal observer state; public CDP events remain in the wire queue.
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
  // Retained browser-level events whose method starts with the prefix and whose sequence is after
  // the cursor.
  Json events(const std::string& prefix, std::uint64_t after_sequence = 0) const;
  // The sequence of the newest browser-level event: a cursor that skips everything retained so far.
  std::uint64_t sequence() const;
  // A finished drag's Input.dragIntercepted events for its root session up to the cursor it read.
  void release_drags(const std::string& session, std::uint64_t through);
  // A consumed, finished download's events; with a frame, every download that frame began (its page
  // closed).
  void release_downloads(const std::vector<std::string>& guids, const std::string& frame = {});
  int execution_context(const std::string& session, const std::string& frame) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace suitecut::detail
