// SPDX-License-Identifier: MIT
// suitecut-session: one SuiteCut engine that people and agents use together,
// for an app that embeds the browser in its own window. The same program is
// also built as suitecut-shared-session, the name embedding apps already run.
//
// The worker owns the engine and serves its private Unix socket (see
// control_session.hpp for framing and startup), speaking protocol 0 and
// protocol 1 (protocol.hpp).
#include "control_session.hpp"
#include "protocol.hpp"
#include "suitecut/agent_tools.hpp"

#include <iostream>

#ifndef SUITECUT_SESSION_VERSION
#define SUITECUT_SESSION_VERSION "0.0.0"
#endif

namespace {
using suitecut::Json;

int usage() {
  std::cerr << "usage: suitecut-session --session NAME --browser /absolute/suitecut-browser "
               "--profile /absolute/profile [--scale 1|2] [--session-dir DIR] [--idle-ms N]\n"
               "       [--tools GROUPS] [--allow-schemes LIST] [--grants optional|required]\n"
               "       [--protocol-0 on|off] [--protocol-1 on|off] start|status|close\n"
               "       suitecut-session --version --json\n";
  return 2;
}

bool on_off(const std::string& value, const std::string& flag) {
  if (value == "on") return true;
  if (value == "off") return false;
  throw std::invalid_argument(flag + " is on or off");
}

std::set<std::string> schemes_in(const std::string& list) {
  std::set<std::string> out;
  for (std::size_t begin = 0; begin <= list.size();) {
    const auto end = list.find(',', begin);
    const auto scheme =
        list.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    if (scheme.empty() ||
        scheme.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789+.-") != std::string::npos)
      throw std::invalid_argument("--allow-schemes lists lower-case URL schemes, comma-separated");
    out.insert(scheme);
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return out;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    suitecut::session::HostOptions host;
    host.launch.backend = suitecut::BrowserBackend::cef;
    host.launch.scale = 2;
    host.launch.host_events = true;  // loading, titles, cursors and IME for hosts' windows
    suitecut::session::ServiceOptions service;
    suitecut::control::SessionOptions session;
    session.idle_ms = 30 * 60 * 1000;
    std::string command;
    bool version = false, json = false;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      auto value = [&]() -> std::string {
        if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + argument);
        return argv[++i];
      };
      if (argument == "--session")
        session.name = value();
      else if (argument == "--session-dir")
        session.directory = value();
      else if (argument == "--browser")
        host.launch.executable = value();
      else if (argument == "--profile")
        host.launch.profile = value();
      else if (argument == "--scale")
        host.launch.scale = static_cast<int>(Json::parse(value()).as_int());
      else if (argument == "--idle-ms")
        session.idle_ms = static_cast<int>(Json::parse(value()).as_int());
      else if (argument == "--tools")
        host.tools = value();
      else if (argument == "--allow-schemes")
        host.schemes = schemes_in(value());
      else if (argument == "--grants") {
        const auto mode = value();
        if (mode != "optional" && mode != "required") return usage();
        service.grants_required = mode == "required";
      } else if (argument == "--protocol-0")
        service.protocol0 = on_off(value(), argument);
      else if (argument == "--protocol-1")
        service.protocol1 = on_off(value(), argument);
      else if (argument == "--version")
        version = true;
      else if (argument == "--json")
        json = true;
      else if (argument == "start" || argument == "status" || argument == "close") {
        if (!command.empty()) return usage();
        command = argument;
      } else
        return usage();
    }
    if (version) {
      if (!json) {
        std::cout << suitecut::session::service_name << ' ' << SUITECUT_SESSION_VERSION << '\n';
        return 0;
      }
      std::cout << Json{{"name", suitecut::session::service_name},
                        {"version", SUITECUT_SESSION_VERSION},
                        {"protocols", Json{{"session", Json::array{Json(0), Json(1)}}}}}
                       .dump()
                << '\n';
      return 0;
    }
    if (session.name.empty() || command.empty()) return usage();
    if (!service.protocol0 && !service.protocol1)
      throw std::invalid_argument("Turn on at least one protocol");
    if (command == "start" && (host.launch.executable.empty() || host.launch.profile.empty()))
      return usage();
    // Fails here, before a worker starts, if a group is unknown.
    (void)suitecut::agent_tool_catalog(host.tools);
    service.session = session.name;
    service.idle_ms = session.idle_ms;
    auto worker = [&](int listener, int ready) {
      suitecut::session::Host state(host);
      suitecut::session::Protocols protocols(state, service);
      suitecut::control::serve_shared_session(
          session, listener, ready, 0,
          [&](suitecut::control::Peer& peer, const Json& message) {
            return protocols.Handle(peer, message);
          },
          [&](suitecut::control::Peer& peer) { protocols.Closed(peer); }, [&] { state.Close(); },
          [&] { return state.Alive(); });
    };
    const auto tool = command == "close" ? "_session_close" : "_session_status";
    const auto reply = suitecut::control::session_request(
        session, Json{{"request", Json{{"tool", tool}}}}, command == "start", worker);
    std::cout << reply.dump() << '\n';
    return reply.at("ok").as_bool() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
