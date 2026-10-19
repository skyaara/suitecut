// SPDX-License-Identifier: MIT
#pragma once
#include <map>
#include <memory>
#include <string>
#include "suitecut/agent.hpp"
#include "suitecut/credentials.hpp"

namespace suitecut {
// Persistent, synchronous tool surface for an agent supplied by the host.
// Calls are serialized by the host. This object borrows Browser; destroy it first.
// No model, Node runtime, daemon, or callback thread is created here.
class AgentDriver {
 public:
  explicit AgentDriver(Browser& browser);
  ~AgentDriver();
  AgentDriver(const AgentDriver&) = delete;
  AgentDriver& operator=(const AgentDriver&) = delete;
  static Json tools();
  // Host-controlled vault configuration; agent tools never receive its master key.
  void set_credential_vault(std::shared_ptr<CredentialVault> vault);
  Json call(const std::string& tool, const Json& arguments = Json::object{});
  // Serialized, page-scoped calls only. Caps the native operation and nested
  // wait/batch budget; never changes the caller's persistent page timeout.
  Json call_with_timeout(const std::string& tool, const Json& arguments, Timeout timeout);
 private:
  std::shared_ptr<CredentialVault> vault_;
  Browser& browser_;
  std::shared_ptr<Transport> transport_;
  std::map<int, Context> contexts_; // Only SDK-owned contexts for attached browsers.
  std::map<int, Page> pages_;
  std::map<int, Request> pending_;
  std::map<int, Json> snapshots_;
  int active_ = 1;
  bool attached_ = false;
};
} // namespace suitecut
