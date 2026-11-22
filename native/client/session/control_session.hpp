// SPDX-License-Identifier: MIT
// The private Unix socket shared by suitecut-native-control's named sessions
// and the session service: startup lock, readiness, framing (a little-endian
// u32 length and that many bytes of JSON) and peer checks.
#pragma once
#include "suitecut/json.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace suitecut::control {
struct SessionOptions {
  std::string name;
  std::string directory;
  int timeout_ms=35000;
  int idle_ms=600000;
};
using Worker=std::function<void(int listener,int ready)>;
// Starts only on an explicit open. Mutations are sent once, with no reconnect
// or replay after dispatch. The worker is a separate native owning process.
Json session_request(const SessionOptions&,const Json& request,bool start,const Worker& worker);
void serve_session(const SessionOptions&,int listener,int ready,
                   const std::function<Json(const Json&)>& call,
                   const std::function<void()>& close);

// One client connection of a shared session, as its handler sees it.
struct Peer {
  std::uint64_t id=0;            // unique for the worker's lifetime
  std::shared_ptr<void> state;   // whatever the handler keeps per connection
  bool close_session=false;      // set by the handler: close once this reply is sent
  // While true, the socket layer answers `_session_status` and
  // `_session_close` itself. A handler clears it for a connection whose
  // protocol decides those (protocol 1 closes for hosts only).
  bool session_requests=true;
};
using PeerCall=std::function<Json(Peer&,const Json&)>;
using PeerClosed=std::function<void(Peer&)>;

// Like serve_session, but each connection runs on its own thread (at most
// |connections| at once, or any number for 0) so a slow request, such as an agent's wait, never
// holds up another client's. |call| must be safe to run concurrently. A
// connection may carry several requests in turn until its client closes it.
// |alive|, when given, is polled; once it returns false the worker withdraws
// and exits as if closed, so the next start launches a fresh one.
// `_session_status` and `_session_close` requests ({"request":{"tool":...}})
// are answered here, before |call|, unless the handler cleared the
// connection's Peer::session_requests.
void serve_shared_session(const SessionOptions&,int listener,int ready,int connections,
                          const std::function<Json(const Json&)>& call,
                          const std::function<void()>& close,
                          const std::function<bool()>& alive={});
// The same, with per-connection state: |closed| runs once a connection ends.
void serve_shared_session(const SessionOptions&,int listener,int ready,int connections,
                          const PeerCall& call,const PeerClosed& closed,
                          const std::function<void()>& close,
                          const std::function<bool()>& alive={});
} // namespace suitecut::control
