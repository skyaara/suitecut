// SPDX-License-Identifier: MIT
// Protocol 0 of the session service: the unversioned envelopes that
// Napdock's installed helper sends (helpers/browserhost in that repository).
// These tests pin today's behaviour, so a change to the service that breaks
// the helper fails here first. They run the service against the session fake
// engine; nothing here is a browser.
//
// usage: shared_session_test SERVICE FAKE_ENGINE [SERVICE_FLAGS...]
//
// Run with --protocol-1 off, the service behaves as the protocol 0 service
// Napdock shipped with. The same checks passed against that program itself
// (examples/shared_session.cpp at commit cb24c74).
#include <set>

#include "session_harness.hpp"

using namespace session_test;

namespace {
std::string binary, engine;
std::vector<std::string> flags;
bool protocol1 = true;

Json tabs(const std::string& owner, Json op) {
  return Json{{"owner", owner}, {"tabs", std::move(op)}};
}
Json view(const std::string& owner, int page, const std::string& op, Json fields = Json::object{}) {
  fields["op"] = op;
  fields["page"] = page;
  return Json{{"owner", owner}, {"view", std::move(fields)}};
}
Json tool(const std::string& owner, int page, const std::string& name,
          Json arguments = Json::object{}) {
  return Json{{"owner", owner},
              {"page", page},
              {"request", Json{{"tool", name}, {"arguments", std::move(arguments)}}}};
}
Json mouse(Json fields) {
  fields["kind"] = "mouse";
  return Json{{"event", std::move(fields)}};
}
Json keyboard(Json fields) {
  fields["kind"] = "keyboard";
  return Json{{"event", std::move(fields)}};
}
Json listed(Connection& connection, const std::string& owner) {
  return result(connection.call(tabs(owner, Json{{"op", "list"}})));
}
Json tab_of(Connection& connection, const std::string& owner, int page) {
  const auto all = listed(connection, owner);
  for (const auto& tab : all.at("tabs").as_array())
    if (tab.at("page").as_int() == page) return tab;
  return Json{};
}
std::string control(const Json& tab) {
  return tab.is_object() ? tab.at("control").as_string() : "gone";
}
std::int64_t generation(const Json& tab) {
  return tab.is_object() ? tab.at("generation").as_int() : -1;
}
int open(Connection& connection, const std::string& owner, const std::string& by,
         const std::string& url = "") {
  Json op{{"op", "open"}, {"by", by}};
  if (!url.empty()) op["url"] = url;
  return static_cast<int>(result(connection.call(tabs(owner, op))).at("page").as_int());
}

void lifecycle_and_framing() {
  Service service(binary, engine, "framing", flags);
  const auto started = service.start();
  check(ok(started) && started.at("result").at("running") == Json(true),
        "start reports a running session");
  check(service.control("status").at("result").at("running") == Json(true),
        "status sees the running session");
  {
    Connection connection(service.socket());
    const auto status = connection.call(Json{{"request", Json{{"tool", "_session_status"}}}});
    check(ok(status) && status.at("result").at("session") == Json("framing") &&
              status.at("result").at("pid").is_integer(),
          "_session_status names the session and its pid");
    // One connection carries any number of requests in turn.
    bool all = true;
    for (int i = 0; i < 50; ++i)
      all = all && ok(connection.call(tabs("browserhost", Json{{"op", "list"}})));
    check(all, "fifty requests on one connection all answer");
  }
  {
    // An empty message ends the connection; it is not answered.
    Connection connection(service.socket());
    connection.send_bytes(std::string(4, '\0'));
    check(connection.closed_by_service(5s), "an empty message closes the connection");
  }
  {
    // A message that stops arriving is dropped after the 2 s receive budget.
    Connection connection(service.socket());
    connection.send_bytes(Connection::frame(R"({"owner":"a","tabs":{"op":"list"}})").substr(0, 12));
    const auto start = Clock::now();
    check(connection.closed_by_service(6s), "a stalled message closes the connection");
    check(Clock::now() - start >= 1500ms, "a stalled message gets about two seconds");
  }
  {
    Connection connection(service.socket());
    connection.send_bytes(Connection::frame("{not json"));
    check(connection.closed_by_service(5s), "a message that is not JSON closes the connection");
  }
  {
    Connection connection(service.socket());
    check(ok(connection.call(tabs("browserhost", Json{{"op", "list"}}))),
          "the service still answers after bad connections");
    const auto closed = connection.call(Json{{"request", Json{{"tool", "_session_close"}}}});
    check(ok(closed) && closed.at("result").at("closed") == Json(true),
          "_session_close acknowledges");
  }
  check(eventually([&] { return !std::filesystem::exists(service.socket()); }, 10s),
        "a closed session withdraws its socket");
  service.mark_stopped();
  check(service.control("status").at("result").at("running") == Json(false),
        "status after close says not running");
}

void envelopes_and_isolation(Service& service) {
  Connection connection(service.socket());
  // Owners.
  check(code(connection.call(Json{{"tabs", Json{{"op", "list"}}}})) == "invalid_owner",
        "a missing owner is refused");
  check(code(connection.call(tabs("bad owner", Json{{"op", "list"}}))) == "invalid_owner",
        "an owner outside [A-Za-z0-9._:-] is refused");
  check(code(connection.call(Json{{"owner", "a"}})) == "invalid_request",
        "an envelope with no kind is refused");
  check(code(connection.call(tabs("a", Json{{"op", "fly"}}))) == "invalid_request",
        "an unknown tabs op is refused");
  check(code(connection.call(tabs("a", Json{{"op", "open"}, {"url", "file:///etc/passwd"}}))) ==
            "invalid_url",
        "only http and https open");
  check(code(connection.call(tool("a", 2, "pages"))) == "unknown_tool",
        "the service keeps tabs to itself");

  // A protocol 1 client's first message. To a protocol 0 service it reads as
  // an unversioned envelope with no owner, and the reply carries no "v": that
  // is how a protocol 1 client knows the service is protocol 0 only. It needs
  // its own connection: a connection keeps the protocol of its first message.
  {
    Connection fresh(service.socket());
    const auto hello =
        fresh.call(Json{{"v", 1},
                        {"id", 1},
                        {"op", "hello"},
                        {"params", Json{{"protocols", Json::array{Json(1)}}, {"owner", "a"}}}});
    if (protocol1)
      check(ok(hello) && hello.at("v") == Json(1) && hello.at("id") == Json(1),
            "hello gets a protocol 1 reply");
    else
      check(!ok(hello) && !hello.contains("v") && code(hello) == "invalid_owner",
            "hello to a protocol 0 service is refused as a missing owner, with no v");
  }
  // On a protocol 0 connection, a versioned message is still protocol 0.
  const auto late =
      connection.call(Json{{"v", 1}, {"id", 2}, {"op", "hello"}, {"params", Json::object{}}});
  check(!late.contains("v") && code(late) == "invalid_owner",
        "a protocol 0 connection stays protocol 0");

  // Ownership: an owner never sees or touches another's tabs.
  const int page = open(connection, "owner-a", "human", "https://a.test/");
  check(listed(connection, "owner-b").at("tabs").as_array().empty(),
        "another owner lists none of a's tabs");
  for (const auto& envelope :
       {tabs("owner-b", Json{{"op", "close"}, {"page", page}}),
        tabs("owner-b", Json{{"op", "take"}, {"page", page}}),
        tabs("owner-b", Json{{"op", "release"}, {"page", page}}),
        tabs("owner-b", Json{{"op", "navigate"}, {"page", page}, {"url", "https://b.test/"}}),
        view("owner-b", page, "resize", Json{{"width", 800}, {"height", 600}}),
        view("owner-b", page, "screenshot"),
        view("owner-b", page, "input", mouse(Json{{"action", "move"}, {"x", 1.0}, {"y", 1.0}})),
        view("owner-b", page, "attach", Json{{"port", "com.example.port"}}),
        tool("owner-b", page, "evaluate", Json{{"expression", "1"}})})
    check(code(connection.call(envelope)) == "unknown_page",
          "another owner's request is unknown_page: " + envelope.dump());
  check(
      code(connection.call(tabs("owner-a", Json{{"op", "close"}, {"page", 1}}))) == "unknown_page",
      "the engine's main page belongs to nobody");
  check(code(connection.call(tabs("owner-a", Json{{"op", "close"}, {"page", 9999}}))) ==
            "unknown_page",
        "a page that never existed is unknown");
  check(eventually([&] {
          return tab_of(connection, "owner-a", page).at("url") == Json("https://a.test/");
        }),
        "the tab's URL follows the engine's navigation event");
  check(tab_of(connection, "owner-a", page).at("title") == Json("Title of https://a.test/"),
        "the title is read from the page");
  check(code(connection.call(tabs(
            "owner-a", Json{{"op", "navigate"}, {"page", page}, {"url", "https://fail.test/"}}))) ==
            "navigation_failed",
        "a failed navigation says so");
  check(ok(connection.call(tabs("owner-a", Json{{"op", "close"}, {"page", page}}))),
        "the owner closes its tab");
  check(listed(connection, "owner-a").at("tabs").as_array().empty(), "a closed tab is gone");

  // The catalogue.
  const auto catalog = result(connection.call(Json{{"owner", "a"}, {"catalog", Json::object{}}}));
  std::set<std::string> names;
  for (const auto& entry : catalog.as_array()) names.insert(entry.at("name").as_string());
  check(names.contains("evaluate") && names.contains("navigate") && names.contains("console"),
        "the catalogue has the tools Napdock calls");
  check(!names.contains("pages") && !names.contains("events") && !names.contains("profiles"),
        "the catalogue leaves out tabs, events and profiles");
}

void control_rules(Service& service) {
  Connection connection(service.socket());
  const std::string owner = "control";
  const int page = open(connection, owner, "human", "https://c.test/");
  auto now = [&] { return tab_of(connection, owner, page); };
  check(control(now()) == "unclaimed" && generation(now()) == 1,
        "a person's new tab is unclaimed at generation 1");

  check(ok(connection.call(tool(owner, page, "evaluate", Json{{"expression", "1"}}))),
        "an agent calls an unclaimed tab");
  check(control(now()) == "agent" && generation(now()) == 2,
        "the agent's call claims it, generation 2");
  check(ok(connection.call(tool(owner, page, "evaluate", Json{{"expression", "1"}}))),
        "the agent keeps calling");
  check(generation(now()) == 2, "calls by the holder do not bump the generation");

  check(ok(connection.call(
            view(owner, page, "input", mouse(Json{{"action", "move"}, {"x", 3.5}, {"y", 4.5}})))),
        "pointer movement is delivered");
  check(ok(connection.call(
            view(owner, page, "input",
                 mouse(Json{{"action", "wheel"}, {"deltaX", 0.0}, {"deltaY", -40.0}})))),
        "a wheel is delivered");
  check(control(now()) == "agent" && generation(now()) == 2,
        "moving and scrolling do not take the tab");

  check(ok(connection.call(
            view(owner, page, "input",
                 mouse(Json{{"action", "down"}, {"button", "left"}, {"clickCount", 1}})))),
        "a press is delivered");
  check(control(now()) == "human" && generation(now()) == 3, "a press takes the tab, generation 3");
  const auto refused = connection.call(tool(owner, page, "evaluate", Json{{"expression", "1"}}));
  check(code(refused) == "human_in_control", "the agent is refused while the person holds it");
  check(ok(connection.call(
            view(owner, page, "input",
                 mouse(Json{{"action", "up"}, {"button", "left"}, {"clickCount", 1}})))),
        "a release of the button is delivered");
  check(generation(now()) == 3, "input from the holder does not bump the generation");

  check(ok(connection.call(tabs(owner, Json{{"op", "release"}, {"page", page}}))),
        "the person releases");
  check(control(now()) == "unclaimed" && generation(now()) == 4, "released, generation 4");
  check(ok(connection.call(tabs(owner, Json{{"op", "release"}, {"page", page}}))),
        "a second release is harmless");
  check(generation(now()) == 4, "releasing an unclaimed tab changes nothing");

  check(ok(connection.call(
            view(owner, page, "input", keyboard(Json{{"action", "down"}, {"key", "a"}})))),
        "a key is delivered");
  check(control(now()) == "human" && generation(now()) == 5, "a key takes the tab");
  connection.call(tabs(owner, Json{{"op", "release"}, {"page", page}}));
  check(ok(connection.call(
            view(owner, page, "input", keyboard(Json{{"action", "insertText"}, {"text", "hi"}})))),
        "typed text is delivered");
  check(control(now()) == "human" && generation(now()) == 7, "typed text takes the tab");
  connection.call(tabs(owner, Json{{"op", "release"}, {"page", page}}));
  check(ok(connection.call(
            tabs(owner, Json{{"op", "navigate"}, {"page", page}, {"history", "reload"}}))),
        "the person reloads");
  check(control(now()) == "human" && generation(now()) == 9, "a person's navigation takes the tab");
  check(ok(connection.call(tabs(owner, Json{{"op", "take"}, {"page", page}}))),
        "take while holding");
  check(generation(now()) == 9, "taking a tab one already holds changes nothing");
  connection.call(tabs(owner, Json{{"op", "release"}, {"page", page}}));

  check(code(connection.call(
            view(owner, page, "input",
                 mouse(Json{{"action", "down"}, {"button", "thumb"}, {"clickCount", 1}})))) ==
            "invalid_input",
        "an unknown button is invalid_input");
  check(code(connection.call(view(owner, page, "input", Json{{"event", Json{{"kind", "pen"}}}}))) ==
            "invalid_input",
        "an unknown input kind is invalid_input");
  check(code(connection.call(view(owner, page, "resize", Json{{"width", 0}, {"height", 10}}))) ==
            "invalid_input",
        "a zero width is refused");
  check(result(
            connection.call(view(owner, page, "resize", Json{{"width", 20000}, {"height", 12000}})))
                .at("width") == Json(20000),
        "the service adds no size cap of its own");

  const int agents = open(connection, owner, "agent");
  check(control(tab_of(connection, owner, agents)) == "agent" &&
            generation(tab_of(connection, owner, agents)) == 1,
        "an agent's new tab starts as the agent's");
  connection.call(tabs(owner, Json{{"op", "close"}, {"page", agents}}));
  connection.call(tabs(owner, Json{{"op", "close"}, {"page", page}}));
}

void watch_cursor(Service& service) {
  Connection connection(service.socket());
  const std::string owner = "watcher";
  const int page = open(connection, owner, "human");
  const auto cursor = listed(connection, owner).at("cursor").as_int();
  {
    const auto start = Clock::now();
    const auto woke = result(connection.call(
        Json{{"owner", owner}, {"watch", Json{{"cursor", cursor}, {"timeoutMs", 300}}}}));
    check(Clock::now() - start >= 250ms && woke.at("cursor").as_int() == cursor,
          "a watch with no change times out");
  }
  std::thread later([&] {
    std::this_thread::sleep_for(200ms);
    Connection other(service.socket());
    other.call(tabs(owner, Json{{"op", "take"}, {"page", page}}));
  });
  const auto start = Clock::now();
  const auto woke = result(connection.call(
      Json{{"owner", owner}, {"watch", Json{{"cursor", cursor}, {"timeoutMs", 20000}}}}));
  later.join();
  check(Clock::now() - start < 5s && woke.at("cursor").as_int() > cursor,
        "a change wakes the watch with a newer cursor");
  check(woke.at("tabs").as_array().size() == 1 &&
            woke.at("tabs").at(0).at("control") == Json("human"),
        "the watch reply lists the owner's tabs as changed");
  const auto stale = result(
      connection.call(Json{{"owner", owner}, {"watch", Json{{"cursor", 0}, {"timeoutMs", 5000}}}}));
  check(stale.at("cursor").as_int() >= woke.at("cursor").as_int(), "an old cursor answers at once");
  const auto other = result(
      connection.call(Json{{"owner", "nobody"}, {"watch", Json{{"cursor", 0}, {"timeoutMs", 0}}}}));
  check(other.at("tabs").as_array().empty(), "another owner's watch lists none of these tabs");
  connection.call(tabs(owner, Json{{"op", "close"}, {"page", page}}));
}

void popups(Service& service) {
  Connection connection(service.socket());
  const std::string owner = "opener";
  const int page = open(connection, owner, "agent");
  check(ok(connection.call(tool(owner, page, "evaluate", Json{{"expression", "popup"}}))),
        "the page opens a popup");
  int popup = 0;
  check(eventually([&] {
          const auto all = listed(connection, owner);
          for (const auto& tab : all.at("tabs").as_array())
            if (tab.at("page").as_int() != page) popup = static_cast<int>(tab.at("page").as_int());
          return popup != 0;
        }),
        "the popup joins its opener's owner");
  check(control(tab_of(connection, owner, popup)) == "agent",
        "the popup is held by whoever held the opener");
  check(listed(connection, "someone-else").at("tabs").as_array().empty(),
        "the popup is not another owner's");
  check(ok(connection.call(tool(owner, popup, "evaluate", Json{{"expression", "1"}}))),
        "the agent drives the popup");
  check(ok(connection.call(tool(owner, popup, "evaluate", Json{{"expression", "close-self"}}))),
        "the popup closes itself");
  check(eventually([&] { return !tab_of(connection, owner, popup).is_object(); }),
        "a page the engine closed leaves the list");
  // The owner's agent lets go of the page on its next call: its handle's
  // close reaches the engine, which no longer knows the page.
  const auto closes_of = [&](int id) {
    int count = 0;
    for (const auto& command : service.commands())
      if (command.at("command") == Json("context") && command.at("action") == Json("closePage") &&
          command.at("params").at("pageId") == Json(id))
        ++count;
    return count;
  };
  check(closes_of(popup) == 0, "nothing closed the popup before the agent's next call");
  check(ok(connection.call(tool(owner, page, "evaluate", Json{{"expression", "1"}}))),
        "the agent goes on with the opener");
  check(closes_of(popup) == 1, "the agent's driver forgets the page the engine closed");

  // No page limit of the service's own: more popups than the driver's old
  // cap of 128 all stay the owner's and drivable.
  const int many = 140;
  for (int i = 0; i < many; ++i)
    require(ok(connection.call(tool(owner, page, "evaluate", Json{{"expression", "popup"}}))),
            "open a popup");
  std::vector<int> popups;
  check(eventually([&] {
          popups.clear();
          const auto all = listed(connection, owner);
          for (const auto& tab : all.at("tabs").as_array())
            if (tab.at("page").as_int() != page)
              popups.push_back(static_cast<int>(tab.at("page").as_int()));
          return static_cast<int>(popups.size()) == many;
        }),
        "every popup is listed");
  require(static_cast<int>(popups.size()) == many,
          std::to_string(popups.size()) + " of " + std::to_string(many) + " popups listed");
  check(ok(connection.call(tool(owner, popups.back(), "evaluate", Json{{"expression", "1"}}))),
        "the agent drives the last of " + std::to_string(many) + " popups");
  check(static_cast<int>(listed(connection, owner).at("tabs").as_array().size()) == many + 1,
        "adopting them all closed none");
  for (const int id : popups)
    require(ok(connection.call(tool(owner, id, "evaluate", Json{{"expression", "close-self"}}))),
            "a popup closes itself");
  check(eventually([&] { return listed(connection, owner).at("tabs").as_array().size() == 1; }),
        "the closed popups leave the list");
  check(ok(connection.call(tool(owner, page, "evaluate", Json{{"expression", "1"}}))),
        "the agent goes on after they close");
  int forgotten = 0;
  for (const int id : popups) forgotten += closes_of(id) == 1;
  check(forgotten == many, "the driver forgets every page the engine closed");
  connection.call(tabs(owner, Json{{"op", "close"}, {"page", page}}));
}

void view_ops(Service& service) {
  Connection connection(service.socket());
  const std::string owner = "viewer";
  const int page = open(connection, owner, "human", "https://v.test/");
  const auto attached = connection.call(
      view(owner, page, "attach",
           Json{{"port", "com.napdock.surfaces.00000000-0000-0000-0000-000000000000"}}));
  check(ok(attached) && attached.at("result").at("enginePid").as_int() > 0,
        "attach answers with the engine's pid");
  check(code(connection.call(view(owner, page, "attach", Json{{"port", "bad port"}}))) ==
            "invalid_request",
        "attach refuses a port name with unsafe characters");
  check(ok(connection.call(view(owner, page, "detach"))), "detach answers");
  const auto shot = connection.call(view(owner, page, "screenshot"));
  check(ok(shot) && shot.at("result").at("base64") == Json("iVBORw0KGgo=") &&
            shot.at("result").at("mimeType") == Json("image/png"),
        "a screenshot returns base64 PNG");
  check(control(tab_of(connection, owner, page)) == "unclaimed",
        "a screenshot does not take the tab");
  bool closed = false;
  for (const auto& command : service.commands())
    if (command.at("command") == Json("context") && command.at("action") == Json("closePage") &&
        command.at("params").at("pageId").as_int() == page)
      closed = true;
  check(!closed && tab_of(connection, owner, page).is_object(), "a screenshot leaves the tab open");
  connection.call(tabs(owner, Json{{"op", "close"}, {"page", page}}));
}

// Every envelope shape the installed Napdock helper sends, as it builds them
// (Helper.swift, AgentTools.swift, MCPServer.swift, Runtime.swift,
// Session.swift), in the order a person and an agent would use them.
void napdock_replay(Service& service) {
  Connection connection(service.socket());
  const std::string owner = "session-0f2c9a3e-napdock";
  // Runtime.swift probe.
  check(ok(connection.call(Json::parse(R"({"owner":"browserhost","tabs":{"op":"list"}})"))),
        "probe");
  // Helper.swift: a person opens a tab with a URL.
  const auto opened = connection.call(
      Json::parse(R"({"owner":")" + owner +
                  R"(","tabs":{"op":"open","by":"human","url":"https://example.test/"}})"));
  check(ok(opened), "helper open");
  const int page = static_cast<int>(opened.at("result").at("page").as_int());
  const auto p = std::to_string(page);
  const std::vector<std::string> person{
      R"({"owner":")" + owner + R"(","view":{"op":"resize","page":)" + p +
          R"(,"width":1280,"height":800}})",
      R"({"owner":")" + owner + R"(","view":{"op":"attach","page":)" + p +
          R"(,"port":"com.napdock.surfaces.6f1c2b4e-8d3a-4c1f-9e2b-7a5d0c3e1f42"}})",
      R"({"owner":")" + owner + R"(","view":{"op":"input","page":)" + p +
          R"(,"event":{"action":"move","x":120.5,"y":64.25,"kind":"mouse"}}})",
      R"({"owner":")" + owner + R"(","view":{"op":"input","page":)" + p +
          R"(,"event":{"action":"down","button":"left","clickCount":1,"kind":"mouse"}}})",
      R"({"owner":")" + owner + R"(","view":{"op":"input","page":)" + p +
          R"(,"event":{"action":"up","button":"left","clickCount":1,"kind":"mouse"}}})",
      R"({"owner":")" + owner + R"(","view":{"op":"input","page":)" + p +
          R"(,"event":{"action":"wheel","deltaX":-0.0,"deltaY":-12.5,"kind":"mouse"}}})",
      R"({"owner":")" + owner + R"(","view":{"op":"input","page":)" + p +
          R"(,"event":{"action":"down","key":"Shift","kind":"keyboard"}}})",
      R"({"owner":")" + owner + R"(","view":{"op":"input","page":)" + p +
          R"(,"event":{"action":"down","key":"a","kind":"keyboard"}}})",
      R"({"owner":")" + owner + R"(","view":{"op":"input","page":)" + p +
          R"(,"event":{"action":"up","key":"a","kind":"keyboard"}}})",
      R"({"owner":")" + owner + R"(","view":{"op":"input","page":)" + p +
          R"(,"event":{"action":"up","key":"Shift","kind":"keyboard"}}})",
      R"({"owner":")" + owner + R"(","view":{"op":"input","page":)" + p +
          R"(,"event":{"action":"insertText","text":"héllo 東京","kind":"keyboard"}}})",
      R"({"owner":")" + owner + R"(","view":{"op":"screenshot","page":)" + p + R"(}})",
      R"({"owner":")" + owner + R"(","tabs":{"op":"navigate","page":)" + p +
          R"(,"url":"https://example.test/next"}})",
      R"({"owner":")" + owner + R"(","tabs":{"op":"navigate","page":)" + p +
          R"(,"history":"back"}})",
      R"({"owner":")" + owner + R"(","tabs":{"op":"navigate","page":)" + p +
          R"(,"history":"forward"}})",
      R"({"owner":")" + owner + R"(","tabs":{"op":"take","page":)" + p + R"(}})",
      R"({"owner":")" + owner + R"(","tabs":{"op":"release","page":)" + p + R"(}})",
      R"({"owner":")" + owner + R"(","view":{"op":"detach","page":)" + p + R"(}})",
      R"({"owner":")" + owner + R"(","watch":{"cursor":0,"timeoutMs":20000}})",
      R"({"owner":")" + owner + R"(","tabs":{"op":"list"}})",
  };
  for (const auto& text : person) {
    const auto reply = connection.call(Json::parse(text));
    check(ok(reply), "helper envelope answers ok: " + text + " -> " + reply.dump());
  }
  const auto shot = connection.call(Json::parse(person[11]));
  check(ok(shot) && shot.at("result").at("base64").is_string(),
        "the helper's screenshot reads base64");
  const auto watched = result(connection.call(Json::parse(person[18])));
  check(watched.at("cursor").is_integer() && watched.at("tabs").is_array(),
        "the helper's watch reads cursor and tabs");
  const auto& first = watched.at("tabs").at(0);
  for (const auto* key : {"page", "url", "title", "control", "generation"})
    check(first.contains(key), std::string("a listed tab has ") + key);

  // MCPServer.swift and AgentTools.swift: an agent's tools.
  const auto agent = connection.call(
      Json::parse(R"({"owner":")" + owner + R"(","tabs":{"op":"open","by":"agent"}})"));
  check(ok(agent), "MCP open");
  const auto a = std::to_string(agent.at("result").at("page").as_int());
  const std::vector<std::string> calls{
      R"({"owner":")" + owner + R"(","page":)" + a +
          R"(,"request":{"tool":"evaluate","arguments":{"expression":"1+1"}}})",
      R"({"owner":")" + owner + R"(","page":)" + a +
          R"(,"request":{"tool":"navigate","arguments":{"url":"https://example.test/agent"}}})",
  };
  for (const auto& text : calls) {
    const auto reply = connection.call(Json::parse(text));
    check(ok(reply), "MCP tool answers ok: " + text + " -> " + reply.dump());
  }
  const auto human = connection.call(
      Json::parse(R"({"owner":")" + owner + R"(","page":)" + p +
                  R"(,"request":{"tool":"evaluate","arguments":{"expression":"1"}}})"));
  check(ok(human), "an agent may use the person's tab once released");
  connection.call(
      Json::parse(R"({"owner":")" + owner + R"(","tabs":{"op":"take","page":)" + p + R"(}})"));
  const auto blocked = connection.call(
      Json::parse(R"({"owner":")" + owner + R"(","page":)" + p +
                  R"(,"request":{"tool":"evaluate","arguments":{"expression":"1"}}})"));
  // SessionWire.result reads error.code and error.message.
  check(!ok(blocked) && code(blocked) == "human_in_control" &&
            blocked.at("error").at("message").is_string(),
        "the helper reads human_in_control from error.code");
  for (const auto& page_text : {p, a})
    check(
        ok(connection.call(Json::parse(R"({"owner":")" + owner +
                                       R"(","tabs":{"op":"close","page":)" + page_text + R"(}})"))),
        "helper close");
}

// Rule 9: agent calls of different owners do not wait for each other. Each
// owner has its own agent; one owner's calls still run one at a time.
void owners_in_parallel(Service& service) {
  Connection setup(service.socket());
  const int slow_page = open(setup, "slow-owner", "agent");
  const int quick_page = open(setup, "quick-owner", "agent");
  std::atomic<bool> slow_done{false};
  std::thread slow([&] {
    Connection connection(service.socket());
    connection.call(tool("slow-owner", slow_page, "evaluate", Json{{"expression", "slow:3000"}}));
    slow_done = true;
  });
  std::this_thread::sleep_for(300ms);
  const auto start = Clock::now();
  const auto quick =
      setup.call(tool("quick-owner", quick_page, "evaluate", Json{{"expression", "1"}}));
  const auto took = Clock::now() - start;
  check(ok(quick) && took < 1500ms && !slow_done,
        "another owner's agent call does not wait for a slow one");
  const int other_tab = open(setup, "quick-owner", "agent");
  check(!slow_done, "another owner opens a tab while the slow call runs");
  // The same owner's next call waits for its own running call.
  const auto same_start = Clock::now();
  Connection same(service.socket());
  const auto queued =
      same.call(tool("slow-owner", slow_page, "evaluate", Json{{"expression", "1"}}));
  check(ok(queued) && Clock::now() - same_start > 1000ms,
        "the same owner's calls run one at a time");
  slow.join();
  for (const auto& [owner, page] : {std::pair{std::string("slow-owner"), slow_page},
                                    {"quick-owner", quick_page},
                                    {"quick-owner", other_tab}})
    setup.call(tabs(owner, Json{{"op", "close"}, {"page", page}}));
}

void engine_exit() {
  Service service(binary, engine, "exit", flags);
  require(ok(service.start()), "start");
  Connection connection(service.socket());
  const int page = open(connection, "a", "agent");
  const auto died = connection.call(tool("a", page, "evaluate", Json{{"expression", "exit"}}));
  check(!ok(died), "a call whose engine dies fails");
  // The worker then withdraws: later requests are engine_exited, or the
  // connection is closed, and the socket goes away.
  check(eventually([&] { return !std::filesystem::exists(service.socket()); }, 10s),
        "the session withdraws once its engine has gone");
  service.mark_stopped();
  check(service.control("status").at("result").at("running") == Json(false),
        "status then says not running");
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: shared_session_test SERVICE FAKE_ENGINE [SERVICE_FLAGS...]\n";
    return 2;
  }
  binary = argv[1];
  engine = argv[2];
  for (int i = 3; i < argc; ++i) flags.push_back(argv[i]);
  for (std::size_t i = 0; i + 1 < flags.size(); ++i)
    if (flags[i] == "--protocol-1" && flags[i + 1] == "off") protocol1 = false;
  try {
    std::cerr << "-- lifecycle_and_framing\n";
    lifecycle_and_framing();
    {
      Service service(binary, engine, "shared", flags);
      require(ok(service.start()), "start");
      std::cerr << "-- envelopes_and_isolation\n";
      envelopes_and_isolation(service);
      std::cerr << "-- control_rules\n";
      control_rules(service);
      std::cerr << "-- watch_cursor\n";
      watch_cursor(service);
      std::cerr << "-- popups\n";
      popups(service);
      std::cerr << "-- view_ops\n";
      view_ops(service);
      std::cerr << "-- napdock_replay\n";
      napdock_replay(service);
      std::cerr << "-- owners_in_parallel\n";
      owners_in_parallel(service);
    }
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
  std::cout << "shared_session_test: all checks passed\n";
  return 0;
}
