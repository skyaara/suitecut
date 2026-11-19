// SPDX-License-Identifier: MIT
#pragma once
#include "suitecut/json.hpp"
#include <functional>
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
// Like serve_session, but each connection runs on its own thread (at most
// |connections| at once, or any number for 0) so a slow request, such as an agent's wait, never
// holds up another client's. |call| must be safe to run concurrently. A
// connection may carry several requests in turn until its client closes it.
// |alive|, when given, is polled; once it returns false the worker withdraws
// and exits as if closed, so the next start launches a fresh one.
void serve_shared_session(const SessionOptions&,int listener,int ready,int connections,
                          const std::function<Json(const Json&)>& call,
                          const std::function<void()>& close,
                          const std::function<bool()>& alive={});
} // namespace suitecut::control
