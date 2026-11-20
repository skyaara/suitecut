// SPDX-License-Identifier: MIT
#include "protocol.hpp"

#include "suitecut/agent_tools.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <filesystem>

#ifndef SUITECUT_SESSION_VERSION
#define SUITECUT_SESSION_VERSION "0.0.0"
#endif

namespace suitecut::session {
namespace {

int page_of(const Json& object, const char* key = "page") {
  if (!object.contains(key) || !object.at(key).is_integer())
    throw Refused("invalid_request", std::string("A ") + (std::string(key) == "view" ? "view" : "page") + " ID is required");
  const auto page = object.at(key).as_int();
  if (page < 2 || page > INT_MAX) throw Refused(std::string(key) == "view" ? "unknown_view" : "unknown_page",
                                                std::string(key) == "view" ? "No such view" : "No such tab");
  return static_cast<int>(page);
}

double number_in(const Json& object, const char* key, double low, double high) {
  if (!object.contains(key) || !object.at(key).is_number()) throw Refused("invalid_input", std::string("Missing ") + key);
  const auto value = object.at(key).as_double();
  if (!(value >= low && value <= high)) throw Refused("invalid_input", std::string("Out of range: ") + key);
  return value;
}

std::string text_in(const Json& object, const char* key, const std::string& fallback = "") {
  return object.contains(key) && object.at(key).is_string() ? object.at(key).as_string() : fallback;
}

// The engine command for a person's history step: the engine names each by
// the Playwright call it is.
Json history_fields(const std::string& history) {
  if (history == "back") return Json{{"command", "historyBack"}, {"api", "page.goBack"}};
  if (history == "forward") return Json{{"command", "historyForward"}, {"api", "page.goForward"}};
  if (history == "reload") return Json{{"command", "reload"}, {"api", "page.reload"}};
  throw Refused("invalid_request", "Navigate to a URL, or back, forward or reload");
}

Json mouse_button(const Json& event, Json fields) {
  const auto button = text_in(event, "button", "left");
  if (button != "left" && button != "right" && button != "middle") throw Refused("invalid_input", "Unknown button");
  fields["button"] = button;
  fields["clickCount"] = static_cast<int>(number_in(event, "clickCount", 1, INT_MAX));
  return fields;
}

// Protocol 0's input: the engine's input command from a viewer's event,
// field by field, and whether it is the kind of input that takes the tab: a
// press, a key or typed text. Moving the pointer or scrolling doesn't.
std::pair<Json, bool> input_fields0(const Json& event) {
  const auto kind = text_in(event, "kind");
  const auto action = text_in(event, "action");
  if (kind == "mouse") {
    Json fields{{"command", "input"}, {"kind", "mouse"}, {"action", action}};
    if (action == "move") {
      fields["x"] = number_in(event, "x", 0, INT_MAX);
      fields["y"] = number_in(event, "y", 0, INT_MAX);
      fields["steps"] = 1;
      return {fields, false};
    }
    if (action == "down" || action == "up") {
      return {mouse_button(event, fields), action == "down"};
    }
    if (action == "wheel") {
      fields["deltaX"] = number_in(event, "deltaX", -INT_MAX, INT_MAX);
      fields["deltaY"] = number_in(event, "deltaY", -INT_MAX, INT_MAX);
      return {fields, false};
    }
    throw Refused("invalid_input", "Unknown mouse action");
  }
  if (kind == "keyboard") {
    if (action == "down" || action == "up") {
      if (!event.contains("key") || !event.at("key").is_string() || event.at("key").as_string().empty())
        throw Refused("invalid_input", "A key names its key");
      return {Json{{"command", "input"}, {"kind", "keyboard"}, {"action", action}, {"key", event.at("key")}},
              action == "down"};
    }
    if (action == "insertText") {
      if (!event.contains("text") || !event.at("text").is_string())
        throw Refused("invalid_input", "Typed text is a string");
      return {Json{{"command", "input"}, {"kind", "keyboard"}, {"action", "insertText"}, {"text", event.at("text")}},
              true};
    }
    throw Refused("invalid_input", "Unknown keyboard action");
  }
  throw Refused("invalid_input", "Input is mouse or keyboard");
}

std::string decode_base64(const std::string& text) {
  auto value = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  std::string out;
  int bits = 0, buffer = 0;
  for (const char c : text) {
    if (c == '=') break;
    const int v = value(c);
    if (v < 0) throw Refused("failed", "The engine returned a screenshot that is not base64");
    buffer = (buffer << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buffer >> bits) & 0xff));
    }
  }
  return out;
}

// What a protocol 1 connection is bound to by its hello.
enum class Role { host, agent };
struct Connection {
  int protocol = -1;  // -1 until its first message
  bool greeted = false;
  std::string owner;
  Role role = Role::host;
  std::string groups;
  std::set<std::string> tools;
  std::optional<std::string> grant;
  std::optional<std::size_t> max_bytes;
};

Connection& connection_of(control::Peer& peer) {
  if (!peer.state) peer.state = std::make_shared<Connection>();
  return *std::static_pointer_cast<Connection>(peer.state);
}

Json error0(const std::string& code, const std::string& message) {
  return Json{{"ok", false}, {"error", Json{{"code", code}, {"message", message}}}};
}

Json reply1(const Json& id, Json result) { return Json{{"v", 1}, {"id", id}, {"ok", true}, {"result", std::move(result)}}; }
Json error1(const Json& id, const std::string& code, const std::string& message) {
  return Json{{"v", 1},
              {"id", id},
              {"ok", false},
              {"error", Json{{"code", code}, {"message", message}, {"retryable", code == "busy"}}}};
}

void host_only(const Connection& connection, const std::string& op) {
  if (connection.role != Role::host) throw Refused("forbidden", "An agent may not use " + op);
}

}  // namespace

Json Protocols::Handle(control::Peer& peer, const Json& message) {
  auto& connection = connection_of(peer);
  if (connection.protocol < 0) {
    connection.protocol = options_.protocol1 && message.is_object() && message.contains("v") ? 1 : 0;
    // On protocol 1, session.close is for hosts: a protocol 0 close
    // envelope on the same connection must not get round that.
    if (connection.protocol == 1) peer.session_requests = false;
  }
  if (connection.protocol == 1) return Protocol1(peer, message);
  if (!options_.protocol0)
    return error0("protocol_unsupported", "This session speaks protocol 1 only; begin with hello");
  return Protocol0(message);
}

void Protocols::Closed(control::Peer& peer) { host_.ReaderGone(peer.id); }

// ---- Protocol 0 ---------------------------------------------------------

Json Protocols::Protocol0(const Json& envelope) {
  try {
    host_.Live();
    if (!envelope.is_object() || !envelope.contains("owner") || !envelope.at("owner").is_string() ||
        !safe_token(envelope.at("owner").as_string(), 0))
      throw Refused("invalid_owner", "Every request names its owner");
    const auto& owner = envelope.at("owner").as_string();
    Json result;
    if (envelope.contains("request")) {
      const auto& request = envelope.at("request");
      if (!request.is_object() || !request.contains("tool") || !request.at("tool").is_string())
        throw Refused("invalid_request", "A tool call names its tool");
      const auto tool = request.at("tool").as_string();
      if (!host_.Tools().contains(tool)) throw Refused("unknown_tool", "Not a shared browser tool: " + tool);
      const int page = page_of(envelope);
      const Json arguments = request.contains("arguments") ? request.at("arguments") : Json(Json::object{});
      if (!arguments.is_object()) throw Refused("invalid_request", "Tool arguments are an object");
      result = host_.Agent(owner, page, tool, arguments, Claim::after, std::nullopt).result;
    } else if (envelope.contains("tabs"))
      result = Tabs0(owner, envelope.at("tabs"));
    else if (envelope.contains("view"))
      result = View0(owner, envelope.at("view"));
    else if (envelope.contains("watch")) {
      const auto& watch = envelope.at("watch");
      const auto cursor = watch.is_object() && watch.contains("cursor") && watch.at("cursor").is_integer()
                              ? static_cast<std::uint64_t>(std::max<std::int64_t>(0, watch.at("cursor").as_int()))
                              : 0;
      const auto timeout = watch.is_object() && watch.contains("timeoutMs") && watch.at("timeoutMs").is_integer()
                               ? std::clamp<std::int64_t>(watch.at("timeoutMs").as_int(), 0, 25000)
                               : 20000;
      result = host_.Watch(owner, cursor, std::chrono::milliseconds(timeout));
    } else if (envelope.contains("catalog"))
      result = host_.Catalog(host_.Tools());
    else
      throw Refused("invalid_request", "Unknown shared session request");
    return Json{{"ok", true}, {"result", std::move(result)}};
  } catch (const Refused& refused) {
    return error0(refused.code, refused.what());
  } catch (const std::exception& error) {
    return error0("failed", error.what());
  }
}

Json Protocols::Tabs0(const std::string& owner, const Json& tabs) {
  if (!tabs.is_object() || !tabs.contains("op") || !tabs.at("op").is_string())
    throw Refused("invalid_request", "A tabs request names its op");
  const auto op = tabs.at("op").as_string();
  if (op == "list") return host_.List(owner);
  if (op == "open") {
    // A `by` that is not a string reads as the default, as it always has.
    const auto by = text_in(tabs, "by", "agent");
    if (by != "agent" && by != "human") throw Refused("invalid_request", "A tab opens for an agent or a person");
    std::optional<std::string> url;
    if (tabs.contains("url")) url = host_.CheckedUrl(tabs.at("url"));
    return host_.Open(owner, by == "agent" ? Control::agent : Control::unclaimed, url);
  }
  const int page = page_of(tabs);
  if (op == "close") {
    host_.CloseTab(owner, page);
    return Json{{"closed", true}};
  }
  if (op == "navigate") {
    // A person's navigation from the app's toolbar.
    Json fields = tabs.contains("url") ? Json{{"command", "navigate"}, {"url", host_.CheckedUrl(tabs.at("url"))}}
                                       : history_fields(text_in(tabs, "history"));
    return host_.Navigate(owner, page, std::move(fields));
  }
  if (op == "take") return host_.Take(owner, page);
  if (op == "release") return host_.Release(owner, page, Control::human);
  throw Refused("invalid_request", "Unknown tabs op: " + op);
}

Json Protocols::View0(const std::string& owner, const Json& view) {
  if (!view.is_object() || !view.contains("op") || !view.at("op").is_string())
    throw Refused("invalid_request", "A view request names its op");
  const auto op = view.at("op").as_string();
  const int page = page_of(view);
  host_.TabOf(owner, page);
  if (op == "attach") {
    // macOS bootstrap service names are at most 128 bytes (BOOTSTRAP_MAX_NAME_LEN).
    if (!view.contains("port") || !view.at("port").is_string() || !safe_token(view.at("port").as_string(), 128))
      throw Refused("invalid_request", "Attach names the receiver's Mach port");
    // Protocol 0 moves the page's one output to the newest attach.
    return Json{{"enginePid", host_.Attach(owner, page, view.at("port").as_string(), true, false)}};
  }
  if (op == "detach") {
    host_.Detach(owner, page);
    return Json{{"detached", true}};
  }
  if (op == "screenshot") return host_.Screenshot(owner, page);
  if (op == "resize") {
    const int width = static_cast<int>(number_in(view, "width", 1, INT_MAX));
    const int height = static_cast<int>(number_in(view, "height", 1, INT_MAX));
    host_.Resize(owner, page, width, height);
    return Json{{"width", width}, {"height", height}};
  }
  if (op == "input") {
    if (!view.contains("event") || !view.at("event").is_object()) throw Refused("invalid_input", "Input carries an event");
    auto [fields, takes] = input_fields0(view.at("event"));
    host_.Input(owner, page, std::move(fields), takes);
    return Json{{"delivered", true}};
  }
  throw Refused("invalid_request", "Unknown view op: " + op);
}

// ---- Protocol 1 ---------------------------------------------------------

Json Protocols::Protocol1(control::Peer& peer, const Json& message) {
  auto& connection = connection_of(peer);
  Json id;
  try {
    if (!message.is_object() || !message.contains("v"))
      throw Refused("invalid_request", "This connection speaks protocol 1: every message carries \"v\":1");
    if (message.contains("id") && message.at("id").is_integer()) id = message.at("id");
    if (!message.at("v").is_integer() || message.at("v").as_int() != 1)
      throw Refused("protocol_unsupported", "This session speaks protocol 1");
    if (!message.contains("id") || !message.at("id").is_integer())
      throw Refused("invalid_request", "A request carries an integer id");
    if (!message.contains("op") || !message.at("op").is_string()) throw Refused("invalid_request", "A request names its op");
    const auto op = message.at("op").as_string();
    const Json params = message.contains("params") ? message.at("params") : Json(Json::object{});
    if (!params.is_object()) throw Refused("invalid_request", "params is an object");

    Json result;
    if (op == "hello") {
      if (connection.greeted) throw Refused("invalid_request", "This connection has already said hello");
      bool speaks = false;
      if (params.contains("protocols") && params.at("protocols").is_array())
        for (const auto& version : params.at("protocols").as_array())
          speaks = speaks || (version.is_integer() && version.as_int() == 1);
      if (!speaks) throw Refused("protocol_unsupported", "This session speaks protocols 0 and 1; offer 1");
      if (!params.contains("owner") || !params.at("owner").is_string() || !safe_token(params.at("owner").as_string(), 0))
        throw Refused("invalid_owner", "hello names its owner: [A-Za-z0-9._:-]");
      const auto owner = params.at("owner").as_string();
      const auto role = text_in(params, "role", "host");
      if (role != "host" && role != "agent") throw Refused("invalid_request", "The role is host or agent");
      Connection bound;
      bound.owner = owner;
      bound.role = role == "agent" ? Role::agent : Role::host;
      bound.groups = host_.ToolGroups();
      if (params.contains("grant") && params.at("grant").is_string()) {
        const auto token = params.at("grant").as_string();
        const auto grant = host_.GrantFor(token);
        if (!grant || grant->owner != owner) throw Refused("not_granted", "The grant is missing, expired or for another owner");
        bound.role = Role::agent;
        bound.groups = grant->groups;
        bound.grant = token;
      } else if (options_.grants_required && bound.role == Role::agent) {
        // Hosts are the same user's apps; it is agents that need a host's grant.
        throw Refused("not_granted", "An agent in this session needs a grant from its host");
      }
      bound.tools = host_.ToolsIn(bound.groups);
      if (params.contains("accept") && params.at("accept").is_object() &&
          params.at("accept").contains("maxMessageBytes")) {
        const auto& limit = params.at("accept").at("maxMessageBytes");
        if (!limit.is_integer() || limit.as_int() < 1) throw Refused("invalid_request", "maxMessageBytes is a positive integer");
        bound.max_bytes = static_cast<std::size_t>(limit.as_int());
      }
      bound.protocol = 1;
      bound.greeted = true;
      Json::array capabilities{"tabs", "views", "input.pointer", "input.key", "input.text", "events", "agent.call", "grants"};
#ifdef __APPLE__
      capabilities.insert(capabilities.begin() + 2, Json("view.sink.mach-bootstrap"));
#endif
      Json::array groups;
      for (std::size_t begin = 0; begin <= bound.groups.size();) {
        const auto end = bound.groups.find(',', begin);
        groups.push_back(bound.groups.substr(begin, end == std::string::npos ? std::string::npos : end - begin));
        if (end == std::string::npos) break;
        begin = end + 1;
      }
      Json engine = Json::object{};
      if (const auto pid = host_.EnginePid()) engine["pid"] = *pid;
      result = Json{{"protocol", 1},
                    {"service", Json{{"name", service_name}, {"version", SUITECUT_SESSION_VERSION}}},
                    {"runtime", Json{{"cef", host_.EngineVersion()}}},
                    {"session", options_.session},
                    {"owner", owner},
                    {"role", role == "agent" || bound.grant ? "agent" : "host"},
                    {"tools", std::move(groups)},
                    {"engine", std::move(engine)},
                    {"capabilities", std::move(capabilities)}};
      connection = std::move(bound);
      return reply1(id, std::move(result));
    }
    if (!connection.greeted) throw Refused("hello_required", "A protocol 1 connection begins with hello");
    if (connection.grant && !host_.GrantFor(*connection.grant))
      throw Refused("not_granted", "This connection's grant was revoked or has expired");
    const auto& owner = connection.owner;
    // Events still read once the engine has gone, so readers get engine.exited.
    if (op != "events.next" && op != "session.status") host_.Live();

    if (op == "session.status") {
      Json::array protocols;
      if (options_.protocol0) protocols.push_back(0);
      protocols.push_back(1);
      result = Json{{"pid", static_cast<std::int64_t>(::getpid())},
                    {"session", options_.session},
                    {"idleTimeoutMs", options_.idle_ms},
                    {"protocols", std::move(protocols)}};
    } else if (op == "session.close") {
      host_only(connection, op);
      peer.close_session = true;
      result = Json{{"closed", true}};
    } else if (op == "session.grant") {
      host_only(connection, op);
      if (params.contains("owner") && params.at("owner") != Json(owner))
        throw Refused("forbidden", "A host grants only its own owner");
      if (text_in(params, "role", "agent") != "agent") throw Refused("invalid_request", "A grant is for an agent");
      Host::Grant grant;
      grant.owner = owner;
      if (params.contains("tools")) {
        if (!params.at("tools").is_array() || params.at("tools").as_array().empty())
          throw Refused("invalid_request", "tools lists tool groups");
        for (const auto& group : params.at("tools").as_array()) {
          if (!group.is_string()) throw Refused("invalid_request", "tools lists tool groups");
          grant.groups += (grant.groups.empty() ? "" : ",") + group.as_string();
        }
        try {
          (void)agent_tool_catalog(grant.groups);
        } catch (const std::exception& error) {
          throw Refused("invalid_request", error.what());
        }
      } else {
        grant.groups = "core";
      }
      if (params.contains("ttlMs")) {
        if (!params.at("ttlMs").is_integer() || params.at("ttlMs").as_int() < 1)
          throw Refused("invalid_request", "ttlMs is a positive integer");
        grant.expires = std::chrono::steady_clock::now() + std::chrono::milliseconds(params.at("ttlMs").as_int());
      }
      const auto groups = grant.groups;
      result = Json{{"grant", host_.AddGrant(std::move(grant))}, {"owner", owner}, {"role", "agent"}, {"tools", groups}};
    } else if (op == "session.revoke") {
      host_only(connection, op);
      if (!params.contains("grant") || !params.at("grant").is_string()) throw Refused("invalid_request", "Name the grant");
      if (!host_.Revoke(owner, params.at("grant").as_string())) throw Refused("not_granted", "No such grant for this owner");
      result = Json{{"revoked", true}};
    } else if (op == "tabs.list") {
      result = host_.List(owner);
    } else if (op == "tabs.open") {
      auto control = connection.role == Role::host ? Control::unclaimed : Control::agent;
      if (params.contains("by")) {
        const auto by = text_in(params, "by");
        if (by != "agent" && by != "human") throw Refused("invalid_request", "A tab opens for an agent or a person");
        if (by == "human" && connection.role != Role::host) throw Refused("forbidden", "An agent opens tabs as an agent");
        control = by == "agent" ? Control::agent : Control::unclaimed;
      }
      std::optional<std::string> url;
      if (params.contains("url")) url = host_.CheckedUrl(params.at("url"));
      result = host_.Open(owner, control, url);
    } else if (op == "tabs.close") {
      const int page = page_of(params);
      if (connection.role != Role::host && host_.TabOf(owner, page).control != Control::agent)
        throw Refused("forbidden", "An agent closes only tabs it controls");
      host_.CloseTab(owner, page);
      result = Json{{"closed", true}};
    } else if (op == "tabs.navigate") {
      host_only(connection, op);
      const int page = page_of(params);
      Json fields = params.contains("url") ? Json{{"command", "navigate"}, {"url", host_.CheckedUrl(params.at("url"))}}
                                           : history_fields(text_in(params, "history"));
      result = host_.Navigate(owner, page, std::move(fields));
    } else if (op == "tabs.take") {
      host_only(connection, op);
      result = host_.Take(owner, page_of(params));
    } else if (op == "tabs.release") {
      result = host_.Release(owner, page_of(params), connection.role == Role::host ? Control::human : Control::agent);
    } else if (op == "view.attach") {
      host_only(connection, op);
      const int page = page_of(params);
      host_.TabOf(owner, page);
      const auto& sink = params.contains("sink") ? params.at("sink") : Json{};
      if (!sink.is_object() || text_in(sink, "kind") != "mach-bootstrap")
        throw Refused("unsupported", "This session's views use a mach-bootstrap sink");
#ifndef __APPLE__
      throw Refused("unsupported", "Mach sinks exist only on macOS");
#endif
      const auto name = text_in(sink, "name");
      // macOS bootstrap service names are at most 128 bytes (BOOTSTRAP_MAX_NAME_LEN).
      if (!safe_token(name, 128)) throw Refused("invalid_request", "The sink names the receiver's bootstrap service");
      const bool replace = params.contains("replace") && params.at("replace") == Json(true);
      const int pid = host_.Attach(owner, page, name, replace, true);
      result = Json{{"view", page}, {"engine", Json{{"pid", pid}}}};
    } else if (op == "view.detach") {
      host_only(connection, op);
      host_.Detach(owner, page_of(params, "view"));
      result = Json{{"detached", true}};
    } else if (op == "view.resize") {
      host_only(connection, op);
      const int view = page_of(params, "view");
      const int width = static_cast<int>(number_in(params, "width", 1, INT_MAX));
      const int height = static_cast<int>(number_in(params, "height", 1, INT_MAX));
      host_.Resize(owner, view, width, height);
      result = Json{{"width", width}, {"height", height}};
    } else if (op == "view.screenshot") {
      const int page = page_of(params);
      auto shot = host_.Screenshot(owner, page);
      const auto& to = params.contains("to") ? params.at("to") : Json("inline");
      if (to.is_object()) {
        const auto path = text_in(to, "file");
        if (path.empty() || !std::filesystem::path(path).is_absolute())
          throw Refused("invalid_request", "to.file is an absolute path");
        const auto bytes = decode_base64(shot.at("base64").as_string());
        const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd < 0) throw Refused("failed", "Cannot write " + path + ": " + std::strerror(errno));
        std::size_t offset = 0;
        while (offset < bytes.size()) {
          const auto n = ::write(fd, bytes.data() + offset, bytes.size() - offset);
          if (n < 0 && errno == EINTR) continue;
          if (n <= 0) {
            ::close(fd);
            throw Refused("failed", "Cannot write " + path);
          }
          offset += static_cast<std::size_t>(n);
        }
        ::close(fd);
        result = Json{{"path", path}, {"mimeType", "image/png"}, {"bytes", static_cast<std::int64_t>(bytes.size())}};
      } else if (to == Json("inline")) {
        result = std::move(shot);
      } else {
        throw Refused("invalid_request", "to is \"inline\" or {\"file\": ABSOLUTE}");
      }
    } else if (op == "input.pointer" || op == "input.wheel" || op == "input.key" || op == "input.text") {
      host_only(connection, op);
      const int view = page_of(params, "view");
      host_.TabOf(owner, view);
      // A pointer action at a position moves there first; moving never takes
      // the tab. Modifiers come from the keys the host holds down.
      auto move_to = [&] {
        if (!params.contains("x") && !params.contains("y")) return;
        host_.Input(owner, view,
                    Json{{"command", "input"}, {"kind", "mouse"}, {"action", "move"},
                         {"x", number_in(params, "x", 0, INT_MAX)}, {"y", number_in(params, "y", 0, INT_MAX)}, {"steps", 1}},
                    false);
      };
      const auto action = text_in(params, "action");
      if (op == "input.pointer") {
        if (action == "move") {
          if (!params.contains("x") || !params.contains("y")) throw Refused("invalid_input", "A move has x and y");
          move_to();
        } else if (action == "down" || action == "up") {
          Json button = params;
          if (!button.contains("clickCount")) button["clickCount"] = 1;
          auto fields = mouse_button(button, Json{{"command", "input"}, {"kind", "mouse"}, {"action", action}});
          move_to();
          host_.Input(owner, view, std::move(fields), action == "down");
        } else {
          throw Refused("invalid_input", "A pointer action is move, down or up");
        }
      } else if (op == "input.wheel") {
        Json fields{{"command", "input"}, {"kind", "mouse"}, {"action", "wheel"},
                    {"deltaX", number_in(params, "deltaX", -INT_MAX, INT_MAX)},
                    {"deltaY", number_in(params, "deltaY", -INT_MAX, INT_MAX)}};
        move_to();
        host_.Input(owner, view, std::move(fields), false);
      } else if (op == "input.key") {
        if (action != "down" && action != "up") throw Refused("invalid_input", "A key action is down or up");
        if (text_in(params, "key").empty()) throw Refused("invalid_input", "A key names its key");
        host_.Input(owner, view, Json{{"command", "input"}, {"kind", "keyboard"}, {"action", action}, {"key", params.at("key")}},
                    action == "down");
      } else {
        if (!params.contains("text") || !params.at("text").is_string()) throw Refused("invalid_input", "Typed text is a string");
        host_.Input(owner, view,
                    Json{{"command", "input"}, {"kind", "keyboard"}, {"action", "insertText"}, {"text", params.at("text")}},
                    true);
      }
      result = Json::object{};
    } else if (op == "agent.call") {
      const auto tool = text_in(params, "tool");
      if (tool.empty()) throw Refused("invalid_request", "A tool call names its tool");
      if (!connection.tools.contains(tool)) {
        if (host_.Tools().contains(tool)) throw Refused("forbidden", "This connection's grant does not include " + tool);
        throw Refused("unknown_tool", "Not a session tool: " + tool);
      }
      const int page = page_of(params);
      const Json arguments = params.contains("arguments") ? params.at("arguments") : Json(Json::object{});
      if (!arguments.is_object()) throw Refused("invalid_request", "Tool arguments are an object");
      std::optional<std::uint64_t> expected;
      if (params.contains("expectGeneration")) {
        if (!params.at("expectGeneration").is_integer() || params.at("expectGeneration").as_int() < 1)
          throw Refused("invalid_request", "expectGeneration is a positive integer");
        expected = static_cast<std::uint64_t>(params.at("expectGeneration").as_int());
      }
      auto called = host_.Agent(owner, page, tool, arguments, Claim::before, expected);
      result = Json{{"result", std::move(called.result)}, {"generation", static_cast<std::int64_t>(called.generation)}};
      if (called.control_lost) result["controlLost"] = true;
    } else if (op == "agent.catalog") {
      result = Json{{"tools", host_.Catalog(connection.tools)}};
    } else if (op == "events.next") {
      if (!params.contains("cursor") || !params.at("cursor").is_integer() || params.at("cursor").as_int() < 0)
        throw Refused("invalid_request", "events.next takes the cursor it last got, or 0");
      const auto timeout = params.contains("timeoutMs") && params.at("timeoutMs").is_integer()
                               ? std::clamp<std::int64_t>(params.at("timeoutMs").as_int(), 0, 25000)
                               : 20000;
      result = host_.NextEvents(peer.id, owner, static_cast<std::uint64_t>(params.at("cursor").as_int()),
                                std::chrono::milliseconds(timeout));
    } else {
      throw Refused("invalid_request", "Unknown op: " + op);
    }
    auto reply = reply1(id, std::move(result));
    if (connection.max_bytes) {
      const auto size = reply.dump().size();
      if (size > *connection.max_bytes)
        return error1(id, "reply_too_large",
                      "The reply is " + std::to_string(size) + " bytes, over this client's maxMessageBytes; for a "
                      "screenshot use to:{file}");
    }
    return reply;
  } catch (const Refused& refused) {
    return error1(id, refused.code, refused.what());
  } catch (const std::exception& error) {
    return error1(id, "failed", error.what());
  }
}

}  // namespace suitecut::session
