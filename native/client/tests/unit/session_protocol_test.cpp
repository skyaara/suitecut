// SPDX-License-Identifier: MIT
// Protocol 1 of the session service (docs/embedding/api-design.md): the
// handshake and detecting a protocol 0 service, request ids and typed
// errors, roles and grants, the control rules with expectGeneration and
// claiming before the call, the event cursor, views and input. It runs the
// service against the session fake engine; nothing here is a browser.
//
// usage: session_protocol_test SERVICE FAKE_ENGINE
#include <set>

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
