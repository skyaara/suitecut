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
} // namespace suitecut::control
