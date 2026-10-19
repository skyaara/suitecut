// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/transport.hpp"
#include "suitecut/json.hpp"
namespace suitecut::detail {
class CdpBackend {
 public:
  static std::unique_ptr<CdpBackend> launch(const LaunchOptions&);
  ~CdpBackend();
  Request submit(Json, std::chrono::milliseconds);
  void cancel(int);
  std::optional<Packet> poll(std::chrono::milliseconds);
  Packet ready() const;
  Packet wait_ready(int, std::chrono::milliseconds);
  ClientStats stats() const;
  void close();
 private:
  struct Impl;
  explicit CdpBackend(std::unique_ptr<Impl>);
  std::unique_ptr<Impl> impl_;
};
}
