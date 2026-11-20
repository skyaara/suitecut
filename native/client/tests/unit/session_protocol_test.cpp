// SPDX-License-Identifier: MIT
// Protocol 1 of the session service (docs/embedding/api-design.md): the
// handshake and detecting a protocol 0 service, request ids and typed
// errors, roles and grants, the control rules with expectGeneration and
// claiming before the call, the event cursor, views and input. It runs the
// service against the session fake engine; nothing here is a browser.
//
// usage: session_protocol_test SERVICE FAKE_ENGINE
#include <fstream>
#include <set>
#include <sstream>

#include "session_harness.hpp"

using namespace session_test;

namespace {
std::string binary, engine;

// A protocol 1 client over one connection: it says hello, then numbers its
// requests.
class Client {
 public:
  Client(const Service& service, Json hello_params) : connection_(service.socket()) {
    hello_ = connection_.call(Json{{"v", 1}, {"id", next_++}, {"op", "hello"}, {"params", std::move(hello_params)}});
  }
  static Json host(const std::string& owner) {
    return Json{{"protocols", Json::array{Json(1)}}, {"owner", owner}, {"role", "host"},
                {"client", Json{{"name", "session_protocol_test"}, {"version", "1"}}}};
  }
  static Json agent(const std::string& owner, const std::string& grant = "") {
    Json params{{"protocols", Json::array{Json(1)}}, {"owner", owner}, {"role", "agent"}};
    if (!grant.empty()) params["grant"] = grant;
    return params;
  }
  const Json& hello() const { return hello_; }
  // The protocol a hello reply shows: a reply without "v" is protocol 0's.
  int protocol() const { return hello_.is_object() && hello_.contains("v") ? 1 : 0; }

  Json call(const std::string& op, Json params = Json::object{}, Clock::duration timeout = 30s) {
    const auto id = next_++;
    auto reply = connection_.call(Json{{"v", 1}, {"id", id}, {"op", op}, {"params", std::move(params)}}, timeout);
    require(reply.is_object(), op + " got a reply");
    check(reply.at("v") == Json(1) && reply.at("id") == Json(id), op + " reply carries v and its id");
    if (!ok(reply))
      check(reply.at("error").contains("retryable") && reply.at("error").at("message").is_string(),
            op + " error carries message and retryable");
    return reply;
  }
  Json result(const std::string& op, Json params = Json::object{}) {
    const auto reply = call(op, std::move(params));
    require(ok(reply), op + " is ok: " + reply.dump());
    return reply.at("result");
  }
  Connection& raw() { return connection_; }

 private:
  Connection connection_;
  std::int64_t next_ = 1;
  Json hello_;
};

Json tab_of(Client& client, int page) {
  const auto listed = client.result("tabs.list");
  for (const auto& tab : listed.at("tabs").as_array())
    if (tab.at("page").as_int() == page) return tab;
  return Json{};
}
std::string control(const Json& tab) { return tab.is_object() ? tab.at("control").as_string() : "gone"; }
std::int64_t generation(const Json& tab) { return tab.is_object() ? tab.at("generation").as_int() : -1; }
int page_of(const Json& tab) { return static_cast<int>(tab.at("page").as_int()); }

void detects_protocol0() {
  Service old(binary, engine, "old", {"--protocol-1", "off"});
  require(ok(old.start()), "start a protocol 0 only service");
  Client client(old, Client::host("detect"));
  check(client.protocol() == 0 && code(client.hello()) == "invalid_owner",
        "a protocol 0 service answers hello without v, as invalid_owner");
}

void handshake(Service& service) {
  {
    Client client(service, Client::host("hello-owner"));
    const auto& hello = client.hello();
    check(client.protocol() == 1 && ok(hello) && hello.at("id") == Json(1), "hello is answered in protocol 1");
    const auto& result = hello.at("result");
    check(result.at("protocol") == Json(1) && result.at("service").at("name") == Json("suitecut-session") &&
              result.at("service").at("version").is_string(),
          "hello names the service and its version");
    check(result.at("owner") == Json("hello-owner") && result.at("role") == Json("host") &&
              result.at("session") == Json("protocol"),
          "hello binds owner and role");
    check(result.at("engine").at("pid").as_int() > 0 && result.at("runtime").at("cef") == Json("session-fake"),
          "hello reports the engine");
    std::set<std::string> capabilities;
    for (const auto& name : result.at("capabilities").as_array()) capabilities.insert(name.as_string());
    check(capabilities.contains("tabs") && capabilities.contains("events") && capabilities.contains("agent.call") &&
              capabilities.contains("grants"),
          "hello lists the capabilities");
    check(code(client.call("hello", Client::host("hello-owner"))) == "invalid_request", "a second hello is refused");
    const auto status = client.result("session.status");
    check(status.at("protocols") == Json(Json::array{Json(0), Json(1)}) && status.at("session") == Json("protocol"),
          "session.status lists both protocols");
  }
  {
    Connection connection(service.socket());
    const auto first = connection.call(Json{{"v", 1}, {"id", 9}, {"op", "tabs.list"}});
    check(code(first) == "hello_required" && first.at("id") == Json(9) && first.at("v") == Json(1),
          "a connection that does not begin with hello gets hello_required");
    const auto unversioned = connection.call(Json{{"owner", "a"}, {"tabs", Json{{"op", "list"}}}});
    check(unversioned.at("v") == Json(1) && code(unversioned) == "invalid_request",
          "a protocol 1 connection stays protocol 1");
  }
  {
    Connection connection(service.socket());
    check(code(connection.call(Json{{"v", 2}, {"id", 1}, {"op", "hello"}})) == "protocol_unsupported",
          "an unknown v is protocol_unsupported");
  }
  {
    Connection connection(service.socket());
    const auto reply = connection.call(Json{{"v", 1}, {"id", 1}, {"op", "hello"},
                                            {"params", Json{{"protocols", Json::array{Json(2)}}, {"owner", "a"}}}});
    check(code(reply) == "protocol_unsupported", "no protocol in common is protocol_unsupported");
  }
  {
    Connection connection(service.socket());
    const auto reply = connection.call(Json{{"v", 1}, {"id", "seven"}, {"op", "hello"}});
    check(code(reply) == "invalid_request" && reply.at("id").is_null(), "a non-integer id is invalid_request, id null");
  }
  {
    Client client(service, Json{{"protocols", Json::array{Json(1)}}, {"owner", "bad owner"}});
    check(code(client.hello()) == "invalid_owner", "hello with an unsafe owner is invalid_owner");
  }
  {
    Client small(service, Json{{"protocols", Json::array{Json(1)}}, {"owner", "small"}, {"accept", Json{{"maxMessageBytes", 300}}}});
    check(code(small.call("agent.catalog")) == "reply_too_large", "a reply over the client's maxMessageBytes is refused");
    check(ok(small.call("tabs.list")), "a small reply still arrives");
  }
}

void control_rules(Service& service) {
  const std::string owner = "rules";
  Client host(service, Client::host(owner));
  Client agent(service, Client::agent(owner));
  check(ok(agent.hello()) && agent.hello().at("result").at("role") == Json("agent"), "an agent says hello");
  const int page = page_of(host.result("tabs.open", Json{{"url", "https://rules.test/"}}));
  check(control(tab_of(host, page)) == "unclaimed" && generation(tab_of(host, page)) == 1,
        "a host's tab opens unclaimed");

  // Rule 5: an agent call on an unclaimed tab claims it before it runs.
  std::thread slow([&] {
    Client other(service, Client::agent(owner));
    other.call("agent.call", Json{{"page", page}, {"tool", "evaluate"}, {"arguments", Json{{"expression", "slow:1500"}}}});
  });
  check(eventually([&] { return control(tab_of(host, page)) == "agent"; }, 1000ms),
        "the tab is the agent's while its first call still runs");
  slow.join();
  check(generation(tab_of(host, page)) == 2, "claiming is one control change");

  const auto called = agent.result("agent.call", Json{{"page", page}, {"tool", "evaluate"},
                                                      {"arguments", Json{{"expression", "1"}}}, {"expectGeneration", 2}});
  check(called.at("result") == Json(42) && called.at("generation") == Json(2) && !called.contains("controlLost"),
        "an agent call returns its result and generation");
  check(code(agent.call("agent.call", Json{{"page", page}, {"tool", "evaluate"}, {"arguments", Json{{"expression", "1"}}},
                                           {"expectGeneration", 1}})) == "stale_generation",
        "rule 6: a stale expectGeneration is refused");

  // Rule 7: a person taking the tab during a call doesn't cancel it.
  std::thread taker([&] {
    std::this_thread::sleep_for(300ms);
    Client person(service, Client::host(owner));
    person.call("tabs.take", Json{{"page", page}});
  });
  const auto lost = agent.result("agent.call", Json{{"page", page}, {"tool", "evaluate"},
                                                    {"arguments", Json{{"expression", "slow:1200"}}}});
  taker.join();
  check(lost.at("controlLost") == Json(true) && lost.at("generation") == Json(3), "rule 7: the result says control was lost");
  // Rule 4.
  check(code(agent.call("agent.call", Json{{"page", page}, {"tool", "evaluate"}, {"arguments", Json{{"expression", "1"}}}})) ==
            "human_in_control",
        "rule 4: a person's tab refuses agent calls");
  check(generation(tab_of(host, page)) == 3, "a refused call changes nothing");

  // Roles.
  for (const auto& [op, params] : std::vector<std::pair<std::string, Json>>{
           {"tabs.take", Json{{"page", page}}},
           {"tabs.navigate", Json{{"page", page}, {"url", "https://rules.test/b"}}},
           {"view.resize", Json{{"view", page}, {"width", 100}, {"height", 100}}},
           {"input.pointer", Json{{"view", page}, {"action", "move"}, {"x", 1}, {"y", 1}}},
           {"input.text", Json{{"view", page}, {"text", "x"}}},
           {"session.close", Json::object{}},
           {"session.grant", Json::object{}},
           {"tabs.open", Json{{"by", "human"}}},
           {"tabs.close", Json{{"page", page}}}})
    check(code(agent.call(op, params)) == "forbidden", "an agent may not use " + op);
  check(ok(agent.call("tabs.release", Json{{"page", page}})) && control(tab_of(host, page)) == "human",
        "an agent's release leaves a person's tab alone");
  check(ok(host.call("tabs.release", Json{{"page", page}})) && control(tab_of(host, page)) == "unclaimed" &&
            generation(tab_of(host, page)) == 4,
        "the host releases its person's tab");
  const int own = page_of(agent.result("tabs.open"));
  check(control(tab_of(host, own)) == "agent", "an agent's tab opens as the agent's");
  check(ok(agent.call("tabs.release", Json{{"page", own}})) && control(tab_of(host, own)) == "unclaimed",
        "an agent releases its own tab");
  agent.call("agent.call", Json{{"page", own}, {"tool", "evaluate"}, {"arguments", Json{{"expression", "1"}}}});
  check(ok(agent.call("tabs.close", Json{{"page", own}})), "an agent closes a tab it controls");

  // Rule 8.
  Client stranger(service, Client::host("stranger"));
  check(stranger.result("tabs.list").at("tabs").as_array().empty(), "rule 8: another owner lists none of these tabs");
  for (const auto& [op, params] : std::vector<std::pair<std::string, Json>>{
           {"tabs.take", Json{{"page", page}}}, {"tabs.close", Json{{"page", page}}},
           {"view.screenshot", Json{{"page", page}}},
           {"agent.call", Json{{"page", page}, {"tool", "evaluate"}, {"arguments", Json{{"expression", "1"}}}}}})
    check(code(stranger.call(op, params)) == "unknown_page", "rule 8: another owner's " + op + " is unknown_page");
  check(code(stranger.call("input.key", Json{{"view", page}, {"action", "down"}, {"key", "a"}})) == "unknown_page",
        "rule 8: another owner's view is unknown");
  check(code(host.call("tabs.open", Json{{"url", "javascript:alert(1)"}})) == "invalid_url", "only http and https open");
  check(code(host.call("tabs.fly")) == "invalid_request", "an unknown op is invalid_request");
  host.call("tabs.close", Json{{"page", page}});
}

void grants(Service& service) {
  const std::string owner = "granting";
  Client host(service, Client::host(owner));
  const auto granted = host.result("session.grant", Json{{"tools", Json::array{Json("core")}}});
  const auto token = granted.at("grant").as_string();
  check(token.size() == 64 && token.find_first_not_of("0123456789abcdef") == std::string::npos,
        "a grant is 32 random bytes in hex");
  check(code(host.call("session.grant", Json{{"owner", "someone-else"}})) == "forbidden", "a host grants only its owner");
  check(code(host.call("session.grant", Json{{"tools", Json::array{Json("wizardry")}}})) == "invalid_request",
        "an unknown tool group is refused");

  Client agent(service, Client::agent(owner, token));
  const auto& bound = agent.hello().at("result");
  check(bound.at("role") == Json("agent") && bound.at("tools") == Json(Json::array{Json("core")}),
        "a granted agent gets the grant's tools");
  std::set<std::string> names;
  const auto catalog = agent.result("agent.catalog");
  for (const auto& tool : catalog.at("tools").as_array()) names.insert(tool.at("name").as_string());
  check(names.contains("navigate") && !names.contains("evaluate"), "the catalogue is the grant's");
  const int page = page_of(agent.result("tabs.open"));
  check(code(agent.call("agent.call", Json{{"page", page}, {"tool", "evaluate"}, {"arguments", Json{{"expression", "1"}}}})) ==
            "forbidden",
        "a tool outside the grant is forbidden");
  check(code(agent.call("agent.call", Json{{"page", page}, {"tool", "wizardry"}})) == "unknown_tool",
        "a tool nobody offers is unknown_tool");
  check(ok(agent.call("agent.call", Json{{"page", page}, {"tool", "navigate"}, {"arguments", Json{{"url", "https://granted.test/"}}}})),
        "a tool in the grant runs");

  Client wrong(service, Client::agent("not-granting", token));
  check(code(wrong.hello()) == "not_granted", "a grant does not work for another owner");
  Client forged(service, Client::agent(owner, std::string(64, 'a')));
  check(code(forged.hello()) == "not_granted", "a made-up grant is refused");

  check(ok(host.call("session.revoke", Json{{"grant", token}})), "the host revokes the grant");
  check(code(agent.call("tabs.list")) == "not_granted", "a revoked grant's connection is refused");
  check(code(host.call("session.revoke", Json{{"grant", token}})) == "not_granted", "a grant revokes once");

  const auto brief = host.result("session.grant", Json{{"ttlMs", 200}}).at("grant").as_string();
  std::this_thread::sleep_for(400ms);
  Client late(service, Client::agent(owner, brief));
  check(code(late.hello()) == "not_granted", "an expired grant is refused");
  host.call("tabs.close", Json{{"page", page}});
}

void events(Service& service) {
  const std::string owner = "eventful";
  Client host(service, Client::host(owner));
  Client reader(service, Client::host(owner));
  const auto first = reader.result("events.next", Json{{"cursor", 0}, {"timeoutMs", 0}});
  check(first.at("events").at(0).at("type") == Json("resync"), "a first read is resync");
  auto cursor = first.at("cursor").as_int();
  const auto idle_start = Clock::now();
  const auto idle = reader.result("events.next", Json{{"cursor", cursor}, {"timeoutMs", 300}});
  check(idle.at("events").as_array().empty() && idle.at("cursor").as_int() == cursor && Clock::now() - idle_start >= 250ms,
        "with nothing new, events.next waits and returns none");

  const int page = page_of(host.result("tabs.open", Json{{"url", "https://events.test/"}}));
  const auto opened = reader.result("events.next", Json{{"cursor", cursor}, {"timeoutMs", 5000}});
  check(!opened.at("events").as_array().empty() && opened.at("events").at(0).at("type") == Json("tab.changed") &&
            opened.at("cursor").as_int() > cursor,
        "opening a tab is a tab.changed event");
  cursor = opened.at("cursor").as_int();
  if (!eventually([&] {
        const auto next = reader.result("events.next", Json{{"cursor", cursor}, {"timeoutMs", 0}});
        cursor = next.at("cursor").as_int();
        return tab_of(host, page).at("url") == Json("https://events.test/");
      }))
    check(false, "the tab's URL arrives");

  std::thread later([&] {
    std::this_thread::sleep_for(200ms);
    Client person(service, Client::host(owner));
    person.call("tabs.take", Json{{"page", page}});
  });
  const auto taken = reader.result("events.next", Json{{"cursor", cursor}, {"timeoutMs", 5000}});
  later.join();
  const auto& event = taken.at("events").at(0);
  check(event.at("type") == Json("tab.changed") && event.at("tab").at("control") == Json("human") &&
            event.at("tab").at("title") == Json("Title of https://events.test/"),
        "a take wakes the reader with the tab as it is, title included");
  cursor = taken.at("cursor").as_int();

  Client stranger(service, Client::host("other-events"));
  const auto other_first = stranger.result("events.next", Json{{"cursor", 0}, {"timeoutMs", 0}});
  const auto mine = host.result("tabs.release", Json{{"page", page}});
  (void)mine;
  const auto other = stranger.result("events.next", Json{{"cursor", other_first.at("cursor").as_int()}, {"timeoutMs", 300}});
  check(other.at("events").as_array().empty(), "another owner sees none of these events");

  host.call("tabs.close", Json{{"page", page}});
  bool closed = false;
  for (int round = 0; round < 5 && !closed; ++round) {
    const auto next = reader.result("events.next", Json{{"cursor", cursor}, {"timeoutMs", 2000}});
    for (const auto& item : next.at("events").as_array())
      closed = closed || (item.at("type") == Json("tab.closed") && item.at("page").as_int() == page);
    cursor = next.at("cursor").as_int();
  }
  check(closed, "closing a tab is a tab.closed event");
}

// With no reader left, events are not kept: a new reader with an old cursor
// starts again from resync.
void reader_resync(Service& service) {
  const std::string owner = "resyncing";
  Client host(service, Client::host(owner));
  std::int64_t cursor = 0;
  {
    Client reader(service, Client::host(owner));
    cursor = reader.result("events.next", Json{{"cursor", 0}, {"timeoutMs", 0}}).at("cursor").as_int();
  }
  // The reader's connection has gone; give the service a moment to notice.
  std::this_thread::sleep_for(1500ms);
  const int page = page_of(host.result("tabs.open"));
  Client reader(service, Client::host(owner));
  const auto next = reader.result("events.next", Json{{"cursor", cursor}, {"timeoutMs", 0}});
  check(next.at("events").at(0).at("type") == Json("resync"), "events missed with no reader read as resync");
  host.call("tabs.close", Json{{"page", page}});
}

void views_and_input(Service& service) {
  const std::string owner = "viewing";
  Client host(service, Client::host(owner));
  const int page = page_of(host.result("tabs.open"));
#ifdef __APPLE__
  const Json sink{{"kind", "mach-bootstrap"}, {"name", "com.example.view.1"}};
  const auto attached = host.result("view.attach", Json{{"page", page}, {"sink", sink}});
  check(attached.at("view").as_int() == page && attached.at("engine").at("pid").as_int() > 0, "view.attach answers");
  check(code(host.call("view.attach", Json{{"page", page}, {"sink", sink}})) == "view_busy", "a second view is view_busy");
  check(ok(host.call("view.attach", Json{{"page", page}, {"sink", sink}, {"replace", true}})), "replace moves the view");
  check(ok(host.call("view.detach", Json{{"view", page}})), "view.detach answers");
  check(ok(host.call("view.attach", Json{{"page", page}, {"sink", sink}})), "a detached tab attaches again");
  check(code(host.call("view.attach", Json{{"page", page}, {"sink", Json{{"kind", "dmabuf"}}}})) == "unsupported",
        "an unknown sink is unsupported");
#endif
  check(host.result("view.resize", Json{{"view", page}, {"width", 900}, {"height", 700}}).at("width") == Json(900),
        "view.resize answers");

  check(ok(host.call("input.pointer", Json{{"view", page}, {"action", "move"}, {"x", 10.5}, {"y", 20}})) &&
            control(tab_of(host, page)) == "unclaimed",
        "a pointer move does not take the tab");
  check(ok(host.call("input.wheel", Json{{"view", page}, {"x", 5}, {"y", 5}, {"deltaX", 0}, {"deltaY", 30}})) &&
            control(tab_of(host, page)) == "unclaimed",
        "a wheel does not take the tab");
  check(ok(host.call("input.pointer", Json{{"view", page}, {"action", "down"}, {"x", 30}, {"y", 40}, {"button", "left"}})) &&
            control(tab_of(host, page)) == "human",
        "a press takes the tab");
  bool moved_then_pressed = false;
  const auto commands = service.commands();
  for (std::size_t i = 0; i + 1 < commands.size(); ++i)
    if (commands[i].at("command") == Json("input") && commands[i].at("action") == Json("move") &&
        commands[i].at("x") == Json(30.0) && commands[i + 1].at("action") == Json("down"))
      moved_then_pressed = true;
  check(moved_then_pressed, "a press at a position moves there first");
  host.call("input.pointer", Json{{"view", page}, {"action", "up"}, {"button", "left"}});
  host.call("tabs.release", Json{{"page", page}});
  check(ok(host.call("input.key", Json{{"view", page}, {"action", "down"}, {"key", "Enter"}})) &&
            control(tab_of(host, page)) == "human",
        "a key takes the tab");
  host.call("tabs.release", Json{{"page", page}});
  check(ok(host.call("input.text", Json{{"view", page}, {"text", "héllo"}})) && control(tab_of(host, page)) == "human",
        "text takes the tab");
  check(code(host.call("input.pointer", Json{{"view", page}, {"action", "fling"}})) == "invalid_input",
        "an unknown pointer action is invalid_input");
  check(code(host.call("input.key", Json{{"view", 9999}, {"action", "down"}, {"key", "a"}})) == "unknown_page",
        "input on a view that does not exist is unknown");

  const auto inline_shot = host.result("view.screenshot", Json{{"page", page}});
  check(inline_shot.at("base64") == Json("iVBORw0KGgo=") && inline_shot.at("mimeType") == Json("image/png"),
        "an inline screenshot is base64 PNG");
  char scratch[] = "/tmp/sc-shot-XXXXXX";
  require(::mkdtemp(scratch) != nullptr, "scratch directory");
  const std::string path = std::string(scratch) + "/shot.png";
  const auto filed = host.result("view.screenshot", Json{{"page", page}, {"to", Json{{"file", path}}}});
  std::ifstream in(path, std::ios::binary);
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  check(filed.at("path") == Json(path) && bytes == std::string("\x89PNG\r\n\x1a\n", 8), "a screenshot to a file writes the PNG");
  std::filesystem::remove_all(scratch);
  check(code(host.call("view.screenshot", Json{{"page", page}, {"to", Json{{"file", "relative.png"}}}})) == "invalid_request",
        "a screenshot file is an absolute path");
  check(tab_of(host, page).is_object(), "screenshots leave the tab open");
  host.call("tabs.close", Json{{"page", page}});
}

// Reads one owner's events from a cursor, keeping what it has seen.
class Reader {
 public:
  Reader(const Service& service, const std::string& owner) : client_(service, Client::host(owner)) {
    cursor_ = client_.result("events.next", Json{{"cursor", 0}, {"timeoutMs", 0}}).at("cursor").as_int();
  }
  // The first event, from now on, that |match| accepts; null after |timeout|.
  Json wait_for(const std::function<bool(const Json&)>& match, Clock::duration timeout = 5s) {
    const auto deadline = Clock::now() + timeout;
    for (;;) {
      for (; next_ < seen_.size(); ++next_)
        if (match(seen_[next_])) return seen_[next_++];
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
      if (left <= 0) return Json{};
      const auto reply = client_.result("events.next", Json{{"cursor", cursor_}, {"timeoutMs", std::min<std::int64_t>(left, 1000)}});
      cursor_ = reply.at("cursor").as_int();
      for (const auto& event : reply.at("events").as_array()) seen_.push_back(event);
    }
  }
  const std::vector<Json>& seen() const { return seen_; }

 private:
  Client client_;
  std::int64_t cursor_ = 0;
  std::vector<Json> seen_;
  std::size_t next_ = 0;
};

bool is(const Json& event, const char* type) { return event.at("type") == Json(type); }
std::vector<Json> commands_named(const Service& service, const std::string& name) {
  std::vector<Json> out;
  for (const auto& command : service.commands())
    if (command.at("command") == Json(name)) out.push_back(command);
  return out;
}

// Phase 3: loading, titles, stop, cursor, IME and edit commands, dialogs as
// prompts, popups with their opener, and protocol 0 unchanged.
void host_events(Service& service) {
  const std::string owner = "hosting";
  Client host(service, Client::host(owner));
  std::set<std::string> capabilities;
  for (const auto& name : host.hello().at("result").at("capabilities").as_array()) capabilities.insert(name.as_string());
  check(capabilities.contains("tabs.stop") && capabilities.contains("tab.loading") && capabilities.contains("tab.title") &&
            capabilities.contains("view.cursor") && capabilities.contains("input.ime") &&
            capabilities.contains("input.edit") && capabilities.contains("prompts"),
        "with host events, hello lists them");
  Reader reader(service, owner);

  // Loading state and title.
  const int page = page_of(host.result("tabs.open", Json{{"url", "https://host.test/"}}));
  const auto on_page = [page](const char* type) {
    return [page, type](const Json& event) {
      return is(event, type) && ((event.contains("page") && event.at("page") == Json(page)) ||
                                 (event.contains("view") && event.at("view") == Json(page)) ||
                                 (event.contains("tab") && event.at("tab").at("page") == Json(page)));
    };
  };
  const auto started = reader.wait_for([&](const Json& event) { return on_page("tab.loading")(event) && event.at("loading") == Json(true); });
  check(started.is_object() && started.at("progress").as_double() == 0, "a load start is tab.loading");
  bool tab_first = false;
  for (const auto& event : reader.seen()) {
    if (on_page("tab.changed")(event)) { tab_first = true; break; }
    if (on_page("tab.loading")(event)) break;
  }
  check(tab_first, "a new tab's tab.changed comes before its loading events");
  const auto progress = reader.wait_for([&](const Json& event) { return on_page("tab.loading")(event) && event.at("progress").as_double() == 0.5; });
  check(progress.is_object() && progress.at("loading") == Json(true), "progress is tab.loading with the progress");
  const auto title = reader.wait_for(on_page("tab.title"));
  check(title.is_object() && title.at("title") == Json("Title of https://host.test/"), "a title change is tab.title");
  const auto stopped = reader.wait_for([&](const Json& event) { return on_page("tab.loading")(event) && event.at("loading") == Json(false); });
  check(stopped.is_object() && stopped.at("canGoBack") == Json(true) && stopped.at("progress").as_double() == 1,
        "a finished load is tab.loading with history");
  const auto listed = tab_of(host, page);
  check(listed.at("loading") == Json(false) && listed.at("canGoBack") == Json(true) && listed.at("canGoForward") == Json(false),
        "tabs.list carries loading and history");

  // Stop.
  check(ok(host.call("tabs.stop", Json{{"page", page}})) && control(tab_of(host, page)) == "human",
        "tabs.stop answers and, as navigating, takes the tab");
  check(!commands_named(service, "host").empty() && commands_named(service, "host").back().at("action") == Json("stop"),
        "tabs.stop reaches the engine");
  host.call("tabs.release", Json{{"page", page}});

  // IME and edit commands.
  check(ok(host.call("input.ime.cancel", Json{{"view", page}})) && control(tab_of(host, page)) == "unclaimed",
        "cancelling a composition does not take the tab");
  check(ok(host.call("input.ime.set", Json{{"view", page}, {"text", "かな"}, {"selection", Json::array{1, 2}}})) &&
            control(tab_of(host, page)) == "human",
        "setting a composition takes the tab");
  const auto set = commands_named(service, "host").back();
  check(set.at("action") == Json("ime") && set.at("op") == Json("set") && set.at("text") == Json("かな") &&
            set.at("selection") == Json(Json::array{1, 2}) && !set.contains("replacement"),
        "input.ime.set reaches the engine");
  check(ok(host.call("input.ime.commit", Json{{"view", page}, {"text", "仮名"}, {"replacement", Json::array{0, 2}}})),
        "input.ime.commit answers");
  const auto commit = commands_named(service, "host").back();
  check(commit.at("op") == Json("commit") && commit.at("replacement") == Json(Json::array{0, 2}) && !commit.contains("selection"),
        "input.ime.commit reaches the engine");
  check(code(host.call("input.ime.set", Json{{"view", page}, {"text", "x"}, {"selection", Json::array{2, 1}}})) == "invalid_input",
        "a backwards range is invalid_input");
  check(code(host.call("input.ime.commit", Json{{"view", page}})) == "invalid_input", "a commit has text");
  host.call("tabs.release", Json{{"page", page}});
  check(ok(host.call("input.edit", Json{{"view", page}, {"command", "copy"}})) && control(tab_of(host, page)) == "unclaimed",
        "copy does not take the tab");
  check(ok(host.call("input.edit", Json{{"view", page}, {"command", "paste"}})) && control(tab_of(host, page)) == "human",
        "paste takes the tab");
  check(commands_named(service, "host").back().at("edit") == Json("paste"), "input.edit reaches the engine");
  check(code(host.call("input.edit", Json{{"view", page}, {"command", "format"}})) == "invalid_input",
        "an unknown edit is invalid_input");
  host.call("tabs.release", Json{{"page", page}});

  // Cursor and IME placement, raised here through the fake engine. The
  // agent call claims the tab for the agent.
  const auto evaluate = [&](Client& client, int on, const std::string& expression) {
    return client.call("agent.call", Json{{"page", on}, {"tool", "evaluate"}, {"arguments", Json{{"expression", expression}}}});
  };
  check(ok(evaluate(host, page, "cursor:pointer")), "raise a cursor change");
  const auto cursor = reader.wait_for(on_page("view.cursor"));
  check(cursor.is_object() && cursor.at("cursor") == Json("pointer"), "a cursor change is view.cursor");
  evaluate(host, page, "ime");
  const auto ime = reader.wait_for(on_page("view.ime"));
  check(ime.is_object() && ime.at("range") == Json(Json::array{0, 3}) &&
            ime.at("bounds") == Json(Json::array{Json::array{10, 20, 30, 40}}),
        "a composition range is view.ime");

  // Dialogs: without a connection that answers prompts, the engine answers.
  check(commands_named(service, "dialog").empty(), "no dialogs are held while nobody answers prompts");
  const auto prompt_on = [](int on) {
    return [on](const Json& event) { return is(event, "prompt.opened") && event.at("prompt").at("page") == Json(on); };
  };
  evaluate(host, page, "dialog:alert:unseen");
  check(!reader.wait_for(prompt_on(page), 600ms).is_object(), "an unheld dialog is no prompt");

  auto prompting = std::make_unique<Client>(service, [&] {
    auto params = Client::host(owner);
    params["capabilities"] = Json::array{Json("prompts")};
    return params;
  }());
  require(ok(prompting->hello()), "a host that answers prompts says hello");
  check(eventually([&] {
          const auto held = commands_named(service, "dialog");
          return !held.empty() && held.back().at("action") == Json("subscribe") && held.back().at("enabled") == Json(true) &&
                 held.back().at("pageId") == Json(page);
        }),
        "a prompt host holds its owner's dialogs");
  Client agent(service, Client::agent(owner));
  require(ok(agent.hello()), "an agent of the owner says hello");

  evaluate(host, page, "dialog:confirm:Sure?");
  const auto opened = reader.wait_for(prompt_on(page));
  require(opened.is_object(), "a held dialog is prompt.opened");
  const auto& prompt = opened.at("prompt");
  const auto id = prompt.at("id").as_string();
  check(prompt.at("kind") == Json("dialog") && prompt.at("dialog") == Json("confirm") && prompt.at("message") == Json("Sure?") &&
            prompt.at("deadlineMs") == Json(30000) && id.starts_with("dialog:"),
        "prompt.opened describes the dialog");
  check(control(tab_of(host, page)) == "agent", "the agent holds the tab that opened the dialog");
  check(code(agent.call("prompt.answer", Json{{"prompt", id}})) == "invalid_request", "an answer says accept");
  check(ok(agent.call("prompt.answer", Json{{"prompt", id}, {"accept", true}})), "the agent answers on a tab it holds");
  const auto answer = commands_named(service, "dialog").back();
  check(answer.at("action") == Json("answer") && answer.at("accept") == Json(true) && answer.at("promptText") == Json(""),
        "the answer reaches the engine");
  const auto closed = reader.wait_for([&](const Json& event) { return is(event, "prompt.closed") && event.at("prompt") == Json(id); });
  check(closed.is_object() && closed.at("outcome") == Json("accepted") && closed.at("page") == Json(page),
        "an answered dialog is prompt.closed, accepted");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"accept", false}})) == "prompt_gone",
        "a second answer is prompt_gone");
  check(code(host.call("prompt.answer", Json{{"prompt", "nope"}, {"accept", true}})) == "unknown_prompt",
        "an unknown prompt is unknown_prompt");

  const auto next_prompt = [&] {
    const auto event = reader.wait_for([&](const Json& e) { return is(e, "prompt.opened"); });
    require(event.is_object(), "a dialog is prompt.opened");
    return event.at("prompt").at("id").as_string();
  };
  // The person holds the tab: the agent may not answer, the host may.
  evaluate(host, page, "dialog:prompt:Name?");
  const auto named = next_prompt();
  host.call("tabs.take", Json{{"page", page}});
  check(code(agent.call("prompt.answer", Json{{"prompt", named}, {"accept", true}})) == "forbidden",
        "an agent may not answer on a tab the person holds");
  {
    Client stranger(service, Client::host("someone-else"));
    check(code(stranger.call("prompt.answer", Json{{"prompt", named}, {"accept", true}})) == "unknown_prompt",
          "another owner's prompt is unknown_prompt");
  }
  check(ok(host.call("prompt.answer", Json{{"prompt", named}, {"accept", true}})) && control(tab_of(host, page)) == "human",
        "the host answers; answering does not change control");
  check(commands_named(service, "dialog").back().at("promptText") == Json("default"),
        "a prompt answered without text keeps its default");
  check(reader.wait_for([&](const Json& e) { return is(e, "prompt.closed") && e.at("prompt") == Json(named); }).is_object(),
        "the host's answer closes the prompt");
  host.call("tabs.release", Json{{"page", page}});

  // A host's navigation dismisses the page's held alert first, as closed:
  // the page is blocked in it and would not navigate.
  evaluate(host, page, "dialog:alert:soon gone");
  const auto reset = next_prompt();
  host.call("tabs.navigate", Json{{"page", page}, {"url", "https://host.test/next"}});
  const auto reset_closed = reader.wait_for([&](const Json& e) { return is(e, "prompt.closed") && e.at("prompt") == Json(reset); });
  check(reset_closed.is_object() && reset_closed.at("outcome") == Json("closed"), "a navigation closes a held dialog");
  const auto dismissal = commands_named(service, "dialog").back();
  check(dismissal.at("action") == Json("answer") && dismissal.at("accept") == Json(false),
        "the session dismissed it before navigating");
  check(code(host.call("prompt.answer", Json{{"prompt", reset}, {"accept", true}})) == "prompt_gone",
        "a closed prompt is prompt_gone");
  host.call("tabs.release", Json{{"page", page}});
  // A beforeunload is left to the person; the engine's reset closes it here.
  evaluate(host, page, "dialog:beforeunload:Leave?");
  const auto leaving = next_prompt();
  const auto answers_before = commands_named(service, "dialog").size();
  host.call("tabs.navigate", Json{{"page", page}, {"url", "https://host.test/after"}});
  const auto left_closed = reader.wait_for([&](const Json& e) { return is(e, "prompt.closed") && e.at("prompt") == Json(leaving); });
  check(left_closed.is_object() && left_closed.at("outcome") == Json("closed") &&
            commands_named(service, "dialog").size() == answers_before,
        "a beforeunload is not dismissed for the person; a reset closes it");
  host.call("tabs.release", Json{{"page", page}});

  // A popup has its opener, and its dialogs are held too.
  evaluate(host, page, "popup");
  const auto popup_event = reader.wait_for([&](const Json& e) {
    return is(e, "tab.changed") && e.at("tab").contains("opener") && e.at("tab").at("opener") == Json(page);
  });
  require(popup_event.is_object(), "a popup is tab.changed with its opener");
  const int popup = page_of(popup_event.at("tab"));
  check(tab_of(host, popup).at("opener") == Json(page), "tabs.list carries a popup's opener");
  check(eventually([&] {
          for (const auto& held : commands_named(service, "dialog"))
            if (held.at("pageId") == Json(popup) && held.at("enabled") == Json(true)) return true;
          return false;
        }),
        "a popup's dialogs are held while a prompt host is connected");

  // Protocol 0 keeps its tab fields.
  {
    Connection old(service.socket());
    const auto reply = old.call(Json{{"owner", owner}, {"tabs", Json{{"op", "list"}}}});
    bool plain = ok(reply);
    for (const auto& tab : plain ? reply.at("result").at("tabs").as_array() : Json::array{})
      plain = plain && !tab.contains("opener") && !tab.contains("loading") && !tab.contains("canGoBack") &&
              !tab.contains("canGoForward") && tab.contains("title");
    check(plain, "protocol 0 tabs carry only the fields they always had");
  }

  // The last prompt host leaves: what it held is dismissed, and dialogs go
  // back to default answers.
  evaluate(host, page, "dialog:confirm:left open");
  const auto left = next_prompt();
  prompting.reset();
  const auto dismissed = reader.wait_for([&](const Json& e) { return is(e, "prompt.closed") && e.at("prompt") == Json(left); });
  check(dismissed.is_object() && dismissed.at("outcome") == Json("dismissed"), "a prompt host leaving dismisses its prompts");
  check(eventually([&] {
          const auto held = commands_named(service, "dialog");
          return !held.empty() && held.back().at("action") == Json("subscribe") && held.back().at("enabled") == Json(false);
        }),
        "with no prompt host, dialogs are answered by default again");
  host.call("tabs.close", Json{{"page", popup}});
  host.call("tabs.close", Json{{"page", page}});
}

// Item 5: a person's prompts. They are held only for a host that shows
// prompts, go only to a host, and are answered by today's policy when it
// leaves.
void person_prompts(Service& service) {
  const std::string owner = "people";
  Client host(service, Client::host(owner));
  Reader reader(service, owner);
  std::set<std::string> capabilities;
  for (const auto& name : host.hello().at("result").at("capabilities").as_array()) capabilities.insert(name.as_string());
  check(capabilities.contains("prompts.person"), "with host events, hello lists prompts.person");
  const int page = page_of(host.result("tabs.open", Json{{"url", "https://people.test/"}}));
  const auto evaluate = [&](int on, const std::string& expression) {
    return host.call("agent.call", Json{{"page", on}, {"tool", "evaluate"}, {"arguments", Json{{"expression", expression}}}});
  };
  const auto person_holds = [&](int on, bool enabled) {
    for (const auto& command : commands_named(service, "host"))
      if (command.at("pageId") == Json(on) && command.at("action") == Json("prompts") &&
          command.at("enabled") == Json(enabled))
        return true;
    return false;
  };
  const auto opened_kind = [&](const char* kind, Clock::duration timeout = 5s) {
    return reader.wait_for([&](const Json& event) {
      return is(event, "prompt.opened") && event.at("prompt").at("kind") == Json(kind) && event.at("prompt").at("page") == Json(page);
    }, timeout);
  };
  const auto closed_of = [&](const std::string& id, Clock::duration timeout = 5s) {
    return reader.wait_for([&](const Json& event) { return is(event, "prompt.closed") && event.at("prompt") == Json(id); }, timeout);
  };
  const auto last_answer = [&] {
    const auto host_commands = commands_named(service, "host");
    for (auto it = host_commands.rbegin(); it != host_commands.rend(); ++it)
      if (it->at("action") == Json("answer")) return *it;
    return Json{};
  };

  // With nobody to ask, nothing is held.
  evaluate(page, "file:open");
  check(!opened_kind("file", 600ms).is_object() && !person_holds(page, true), "no host with prompts: no file prompt");
  // An agent that lists prompts answers dialogs, not a person's prompts.
  {
    auto params = Client::agent(owner);
    params["capabilities"] = Json::array{Json("prompts")};
    Client agent_prompts(service, params);
    require(ok(agent_prompts.hello()), "an agent that answers prompts says hello");
    check(eventually([&] {
            for (const auto& held : commands_named(service, "dialog"))
              if (held.at("pageId") == Json(page) && held.at("enabled") == Json(true)) return true;
            return false;
          }),
          "an agent with prompts holds dialogs");
    check(!person_holds(page, true), "an agent with prompts holds none of a person's prompts");
  }
  auto params = Client::host(owner);
  params["capabilities"] = Json::array{Json("prompts")};
  auto prompting = std::make_unique<Client>(service, params);
  require(ok(prompting->hello()), "a host that shows prompts says hello");
  check(eventually([&] { return person_holds(page, true); }), "a host with prompts holds a person's prompts");
  Client agent(service, Client::agent(owner));

  // File choosers.
  char scratch[] = "/tmp/sc-person-XXXXXX";
  require(::mkdtemp(scratch) != nullptr, "a scratch directory");
  const std::string dir = scratch, file = dir + "/notes.txt";
  std::ofstream(file) << "notes";
  evaluate(page, "file:open");
  auto opened = opened_kind("file");
  require(opened.is_object(), "a page's file chooser is prompt.opened");
  auto id = opened.at("prompt").at("id").as_string();
  check(id.starts_with("file:") && opened.at("prompt").at("mode") == Json("open") &&
            opened.at("prompt").at("accept") == Json(Json::array{Json(".txt")}) &&
            !opened.at("prompt").contains("deadlineMs") && !opened.at("prompt").contains("state"),
        "prompt.opened describes the file chooser, with no deadline");
  check(code(agent.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json(file)}}})) == "forbidden",
        "an agent may not answer a person's prompt");
  const auto refused = host.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json("notes.txt")}}});
  check(code(refused) == "invalid_path" && refused.at("error").at("message").as_string().find("notes.txt") != std::string::npos,
        "a relative path is invalid_path, naming it");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json(dir + "/missing.txt")}}})) == "invalid_path",
        "a missing file is invalid_path");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json(dir)}}})) == "invalid_path",
        "a directory is invalid_path for a file");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json(file), Json(file)}}})) == "invalid_request",
        "a single chooser takes one path");
  check(ok(host.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json(file)}}})), "a readable file answers it");
  check(last_answer().at("answer").at("paths") == Json(Json::array{Json(file)}), "the paths reach the engine");
  auto closed = closed_of(id);
  check(closed.is_object() && closed.at("outcome") == Json("accepted"), "an answered chooser is prompt.closed, accepted");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"cancel", true}})) == "prompt_gone", "a second answer is prompt_gone");
  evaluate(page, "file:openFolder");
  id = opened_kind("file").at("prompt").at("id").as_string();
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json(file)}}})) == "invalid_path",
        "a file does not answer a folder chooser");
  check(ok(host.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json(dir)}}})), "a directory answers it");
  evaluate(page, "file:save");
  id = opened_kind("file").at("prompt").at("id").as_string();
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json("/no/such/dir/out.txt")}}})) ==
            "invalid_path",
        "saving outside a writable directory is invalid_path");
  check(ok(host.call("prompt.answer", Json{{"prompt", id}, {"paths", Json::array{Json(dir + "/new.txt")}}})),
        "a new file in a writable directory answers a save chooser");
  evaluate(page, "file:openMultiple");
  id = opened_kind("file").at("prompt").at("id").as_string();
  check(ok(host.call("prompt.answer", Json{{"prompt", id}, {"cancel", true}})) &&
            last_answer().at("answer") == Json(Json{{"cancel", true}}),
        "cancel answers a chooser");
  check(closed_of(id).at("outcome") == Json("dismissed"), "a cancelled chooser is dismissed");
  // A navigation drops a held chooser.
  evaluate(page, "file:open");
  id = opened_kind("file").at("prompt").at("id").as_string();
  host.call("tabs.navigate", Json{{"page", page}, {"url", "https://people.test/next"}});
  check(closed_of(id).at("outcome") == Json("closed"), "a navigation closes a held chooser");
  host.call("tabs.release", Json{{"page", page}});

  // Permissions.
  evaluate(page, "permission:geolocation");
  opened = opened_kind("permission");
  require(opened.is_object(), "a permission request is prompt.opened");
  id = opened.at("prompt").at("id").as_string();
  check(opened.at("prompt").at("permissions") == Json(Json::array{Json("geolocation")}) &&
            opened.at("prompt").at("origin") == Json("https://fake.test"),
        "prompt.opened names the permission and origin");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"accept", "yes"}})) == "invalid_request", "accept is a boolean");
  check(ok(host.call("prompt.answer", Json{{"prompt", id}, {"accept", false}})) &&
            last_answer().at("answer") == Json(Json{{"accept", false}}),
        "a denial reaches the engine");
  check(closed_of(id).at("outcome") == Json("dismissed"), "a denied permission is dismissed");

  // Client certificates.
  evaluate(page, "certificate");
  opened = opened_kind("certificate");
  require(opened.is_object(), "a certificate request is prompt.opened");
  id = opened.at("prompt").at("id").as_string();
  const auto& certificate = opened.at("prompt").at("certificates").at(0);
  check(certificate.at("index") == Json(0) && certificate.at("subject") == Json("Person") &&
            certificate.at("issuer") == Json("Fake CA") && certificate.contains("expires"),
        "prompt.opened lists the certificates");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"index", 3}})) == "invalid_request", "an index must be listed");
  check(ok(host.call("prompt.answer", Json{{"prompt", id}, {"index", 0}})) && last_answer().at("answer").at("index") == Json(0),
        "a chosen certificate reaches the engine");

  // HTTP authentication. On the agent's tab its own credentials answer, with
  // no prompt; on the person's tab, the person is asked.
  check(control(tab_of(host, page)) == "agent", "the agent holds the tab");
  evaluate(page, "auth:agent");
  check(eventually([&] { return last_answer().is_object() && last_answer().at("answer") == Json(Json{{"policy", true}}); }),
        "on the agent's tab its credentials answer");
  check(!opened_kind("auth", 600ms).is_object(), "and nobody is asked");
  evaluate(page, "auth");
  opened = opened_kind("auth");
  check(opened.is_object() && !opened.at("prompt").contains("agentCredentials"),
        "on the agent's tab without its credentials, the person is asked");
  id = opened.at("prompt").at("id").as_string();
  check(ok(host.call("prompt.answer", Json{{"prompt", id}, {"cancel", true}})), "the person can cancel a login");
  const std::string secret = "pw-k3y-9817-not-logged";
  host.call("tabs.navigate", Json{{"page", page}, {"url", "https://people.test/auth-agent"}});
  check(control(tab_of(host, page)) == "human", "the person holds the tab");
  opened = opened_kind("auth");
  require(opened.is_object(), "on the person's tab the agent's credentials do not answer: the person is asked");
  id = opened.at("prompt").at("id").as_string();
  check(opened.at("prompt").at("realm") == Json("Fake") && opened.at("prompt").at("scheme") == Json("basic") &&
            opened.at("prompt").at("url") == Json("https://people.test/auth-agent"),
        "prompt.opened describes the challenge");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"username", "ann"}})) == "invalid_request",
        "a login has a password");
  const auto answered = host.call("prompt.answer", Json{{"prompt", id}, {"username", "ann"}, {"password", secret}});
  check(ok(answered), "a login answers it");
  check(last_answer().at("answer").at("username") == Json("ann") &&
            last_answer().at("answer").at("password") == Json("<redacted>"),
        "the login reached the engine (the fake engine logs no password)");
  closed = closed_of(id);
  check(closed.is_object() && closed.at("outcome") == Json("accepted"), "an answered login is accepted");

  // Downloads: on the agent's tab, the agent's policy; on the person's tab,
  // the person says where.
  host.call("tabs.release", Json{{"page", page}});
  evaluate(page, "download");
  check(!opened_kind("download", 600ms).is_object(), "a download on the agent's tab asks nobody");
  host.call("tabs.navigate", Json{{"page", page}, {"url", "https://people.test/download"}});
  opened = opened_kind("download");
  require(opened.is_object(), "a download on the person's tab asks where");
  id = opened.at("prompt").at("id").as_string();
  check(id.starts_with("download:") && opened.at("prompt").at("suggestedName") == Json("report.pdf") &&
            opened.at("prompt").at("mimeType") == Json("application/pdf") && opened.at("prompt").at("totalBytes") == Json(3),
        "prompt.opened describes the download");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"path", "report.pdf"}})) == "invalid_path",
        "a relative download path is invalid_path");
  check(code(host.call("prompt.answer", Json{{"prompt", id}, {"path", dir}})) == "invalid_path",
        "a directory is no download path");
  const auto saved = dir + "/report.pdf";
  check(ok(host.call("prompt.answer", Json{{"prompt", id}, {"path", saved}})), "a path answers it");
  closed = closed_of(id);
  check(closed.is_object() && closed.at("outcome") == Json("accepted") && closed.at("path") == Json(saved),
        "the download's prompt closes once it is saved there");
  std::string contents;
  std::getline(std::ifstream(saved), contents);
  check(contents == "pdf", "the file is where the person chose");
  host.call("tabs.navigate", Json{{"page", page}, {"url", "https://people.test/download"}});
  id = opened_kind("download").at("prompt").at("id").as_string();
  check(ok(host.call("prompt.answer", Json{{"prompt", id}, {"cancel", true}})), "cancel answers a download");
  check(closed_of(id).at("outcome") == Json("dismissed") &&
            commands_named(service, "download").back().at("action") == Json("cancel"),
        "a cancelled download is dismissed, and cancelled in the engine");

  // The password went to the engine and nowhere else: no event, reply,
  // status or file of the service carries it.
  bool leaked = answered.dump().find(secret) != std::string::npos ||
                host.result("session.status").dump().find(secret) != std::string::npos ||
                service.control("status").dump().find(secret) != std::string::npos;
  for (const auto& event : reader.seen()) leaked = leaked || event.dump().find(secret) != std::string::npos;
  const auto root = std::filesystem::path(service.profile()).parent_path();
  for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
    if (!entry.is_regular_file()) continue;
    std::ifstream in(entry.path(), std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    leaked = leaked || text.str().find(secret) != std::string::npos;
  }
  check(!leaked, "an answered password is in no event, reply, status or file of the service");

  // The last host with prompts leaves: what is held is dismissed, and the
  // engine answers by today's policy again.
  host.call("tabs.release", Json{{"page", page}});
  evaluate(page, "permission:notifications");
  const auto left = opened_kind("permission").at("prompt").at("id").as_string();
  prompting.reset();
  check(closed_of(left).at("outcome") == Json("dismissed"), "a host leaving dismisses a person's prompts");
  check(eventually([&] { return person_holds(page, false); }), "and the engine answers them by policy again");
  host.call("tabs.close", Json{{"page", page}});
  std::error_code ignored;
  std::filesystem::remove_all(dir, ignored);
}

void close_and_protocol0_off() {
  Service service(binary, engine, "only-one", {"--protocol-0", "off"});
  require(ok(service.start()), "start a protocol 1 only service");
  {
    Connection connection(service.socket());
    const auto refused = connection.call(Json{{"owner", "a"}, {"tabs", Json{{"op", "list"}}}});
    check(!refused.contains("v") && code(refused) == "protocol_unsupported",
          "with protocol 0 off, an unversioned message is protocol_unsupported");
  }
  check(service.control("status").at("result").at("running") == Json(true), "the CLI's status still works");
  Client host(service, Client::host("closer"));
  check(host.result("session.status").at("protocols") == Json(Json::array{Json(1)}), "status lists protocol 1 only");
  Client agent(service, Client::agent("closer"));
  check(code(agent.call("session.close")) == "forbidden", "an agent may not close the session");
  // Nor with protocol 0's close envelope on its protocol 1 connection, which
  // the socket layer used to answer before the protocol saw it.
  for (const auto* tool : {"_session_close", "_session_status"}) {
    const auto refused = agent.raw().call(Json{{"request", Json{{"tool", tool}}}});
    check(refused.contains("v") && code(refused) == "invalid_request",
          std::string("a protocol 1 connection's ") + tool + " envelope is refused");
  }
  check(service.control("status").at("result").at("running") == Json(true), "the agent did not close the session");
  check(host.result("session.close").at("closed") == Json(true), "the host closes the session");
  check(eventually([&] { return !std::filesystem::exists(service.socket()); }, 10s), "the session withdraws its socket");
  service.mark_stopped();
}

void grants_required() {
  Service service(binary, engine, "strict", {"--grants", "required", "--tools", "core"});
  require(ok(service.start()), "start a service that requires grants");
  Client host(service, Client::host("strict-owner"));
  check(ok(host.hello()) && host.hello().at("result").at("tools") == Json(Json::array{Json("core")}),
        "a host needs no grant, and gets the service's tools");
  Client agent(service, Client::agent("strict-owner"));
  check(code(agent.hello()) == "not_granted", "an agent without a grant is refused");
  const auto token = host.result("session.grant").at("grant").as_string();
  Client granted(service, Client::agent("strict-owner", token));
  check(ok(granted.hello()), "an agent with a grant is let in");
}

void engine_exit() {
  Service service(binary, engine, "exiting");
  require(ok(service.start()), "start");
  Client host(service, Client::host("dying"));
  Client reader(service, Client::host("dying"));
  auto cursor = reader.result("events.next", Json{{"cursor", 0}, {"timeoutMs", 0}}).at("cursor").as_int();
  const int page = page_of(host.result("tabs.open"));
  cursor = reader.result("events.next", Json{{"cursor", cursor}, {"timeoutMs", 2000}}).at("cursor").as_int();
  host.call("agent.call", Json{{"page", page}, {"tool", "evaluate"}, {"arguments", Json{{"expression", "exit"}}}});
  bool exited = false;
  try {
    for (int round = 0; round < 5 && !exited; ++round) {
      const auto next = reader.call("events.next", Json{{"cursor", cursor}, {"timeoutMs", 2000}});
      if (!ok(next)) break;
      for (const auto& event : next.at("result").at("events").as_array())
        exited = exited || event.at("type") == Json("engine.exited");
      cursor = next.at("result").at("cursor").as_int();
    }
  } catch (...) {
    // The worker may withdraw before the reader asks again.
  }
  check(exited, "a reader is told engine.exited");
  check(eventually([&] { return !std::filesystem::exists(service.socket()); }, 10s),
        "the session withdraws once its engine has gone");
  service.mark_stopped();
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: session_protocol_test SERVICE FAKE_ENGINE\n";
    return 2;
  }
  binary = argv[1];
  engine = argv[2];
  try {
    const auto version = run({binary, "--version", "--json"});
    const auto described = Json::parse(version.second);
    check(described.at("name") == Json("suitecut-session") &&
              described.at("protocols").at("session") == Json(Json::array{Json(0), Json(1)}),
          "--version --json names the service and its protocols");
    std::cerr << "-- detects_protocol0\n";
    detects_protocol0();
    {
      Service service(binary, engine, "protocol");
      require(ok(service.start()), "start");
      std::cerr << "-- handshake\n";
      handshake(service);
      std::cerr << "-- control_rules\n";
      control_rules(service);
      std::cerr << "-- grants\n";
      grants(service);
      std::cerr << "-- events\n";
      events(service);
      std::cerr << "-- reader_resync\n";
      reader_resync(service);
      std::cerr << "-- views_and_input\n";
      views_and_input(service);
      std::cerr << "-- host_events\n";
      host_events(service);
      std::cerr << "-- person_prompts\n";
      person_prompts(service);
    }
    std::cerr << "-- close_and_protocol0_off\n";
    close_and_protocol0_off();
    std::cerr << "-- grants_required\n";
    grants_required();
    std::cerr << "-- engine_exit\n";
    engine_exit();
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << " (after: " << last_check << ")\n";
    return 1;
  }
  if (failures) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "session_protocol_test: all checks passed\n";
  return 0;
}
