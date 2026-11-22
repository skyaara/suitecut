// SPDX-License-Identifier: MIT
// The session service's two protocols over one socket.
//
// Protocol 0 is the unversioned one embedding apps already use. Every
// request names its `owner`, and exactly one of these keys says what it is:
//
//   request  an agent's tool call on `page`, from the native agent catalogue
//   tabs     list, open, close, navigate, take or release an owner's tabs
//   view     attach a page's frames to a Mach port, resize it, send input
//   watch    wait until the session's tabs change after `cursor`
//   catalog  the tools agents may call
//
// Protocol 1 messages carry "v":1, a request `id` and an `op`; a connection
// begins with `hello`, which binds it to one owner and role. See
// docs/embedding/api-design.md. A connection keeps the protocol of its first
// message.
#pragma once
#include "control_session.hpp"
#include "host.hpp"

namespace suitecut::session {

struct ServiceOptions {
  std::string session;  // the session's name
  int idle_ms = 0;
  bool protocol0 = true;
  bool protocol1 = true;
  bool grants_required = false;
};

inline constexpr const char* service_name = "suitecut-session";

class Protocols {
 public:
  Protocols(Host& host, ServiceOptions options) : host_(host), options_(std::move(options)) {}
  Json Handle(control::Peer& peer, const Json& message);
  void Closed(control::Peer& peer);

 private:
  Json Protocol0(const Json& envelope);
  Json Tabs0(const std::string& owner, const Json& tabs);
  Json View0(const std::string& owner, const Json& view);
  Json Protocol1(control::Peer& peer, const Json& message);

  Host& host_;
  ServiceOptions options_;
};

}  // namespace suitecut::session
