// SPDX-License-Identifier: MIT
// embed.h against the session service and the session fake engine: runtime
// inspection, starting and joining a session, tabs, views and frames (macOS),
// input, grants and agent calls, and the event cursor. Only the C API is
// used, as an embedding app would.
//
// usage: embed_test SERVICE FAKE_ENGINE
#include <atomic>
#include <cstring>
#include <filesystem>
#include <sys/un.h>
#include <fstream>
#include <set>
#include <thread>

#include "session_harness.hpp"
#include "suitecut/embed.h"

#ifdef __APPLE__
#include <IOSurface/IOSurfaceRef.h>
#endif

using namespace session_test;
namespace fs = std::filesystem;

namespace {
std::string service_binary, engine_binary;
fs::path root;

std::string refusal(const sc_error& error) {
  char code[64];
  sc_error_refusal(&error, code, sizeof code);
  return code;
}
std::string text(sc_json* json) {
  size_t size = 0;
  const char* data = sc_json_data(json, &size);
  std::string out(data ? data : "", size);
  sc_json_destroy(json);
  return out;
}
Json tabs(sc_session* session) {
  sc_json* listed = nullptr;
  sc_error error;
  require(sc_tab_list(session, &listed, &error) == SC_OK, std::string("list tabs: ") + error.message);
  return Json::parse(text(listed)).at("tabs");
}
std::vector<Json> commands(const fs::path& profile) {
  std::vector<Json> out;
  std::ifstream in(profile / "commands.log");
  std::string line;
  while (std::getline(in, line))
    if (!line.empty()) out.push_back(Json::parse(line));
  return out;
}
// A runtime directory as an app would install one, here from the build.
fs::path make_runtime(const std::string& name, const std::string& service) {
  const auto dir = root / name;
  fs::create_directories(dir);
  fs::create_symlink(service, dir / "suitecut-session");
  fs::create_symlink(engine_binary, dir / "suitecut-browser");
  return dir;
}

void runtime() {
  const auto dir = make_runtime("runtime", service_binary);
  sc_runtime_options options;
  sc_runtime_options_init(&options);
  sc_runtime_info info;
  sc_runtime_info_init(&info);
  sc_error error;
  const auto path = dir.string();
  options.path = path.c_str();
  check(sc_runtime_inspect(&options, &info, &error) == SC_OK, std::string("inspect a runtime: ") + error.message);
  check(info.protocol == 1 && info.protocol_min == 0 && info.protocol_max == 1, "the service speaks protocols 0 and 1");
  check(info.service_version[0] != 0 && !info.signature_verified, "a version, and no signature checked when none was asked");
  check(fs::path(info.engine_path).filename() == "suitecut-browser" && fs::path(info.service_path).filename() == "suitecut-session",
        "the runtime's programs are found");
  options.protocol_min = options.protocol_max = 2;
  check(sc_runtime_inspect(&options, &info, &error) == SC_UNSUPPORTED, "no shared protocol is SC_UNSUPPORTED");

  // A service from before protocol 1 does not know --version.
  const auto old_dir = root / "old-runtime";
  fs::create_directories(old_dir);
  {
    std::ofstream script(old_dir / "suitecut-session");
    script << "#!/bin/sh\necho usage >&2\nexit 2\n";
  }
  fs::permissions(old_dir / "suitecut-session", fs::perms::owner_all);
  fs::create_symlink(engine_binary, old_dir / "suitecut-browser");
  sc_runtime_options_init(&options);
  const auto old_path = old_dir.string();
  options.path = old_path.c_str();
  check(sc_runtime_inspect(&options, &info, &error) == SC_UNSUPPORTED && info.protocol_max == 0,
        "an old service is protocol 0 only, which a protocol 1 app cannot use");

  const auto missing = (root / "nowhere").string();
  options.path = missing.c_str();
  check(sc_runtime_inspect(&options, &info, &error) == SC_ERROR, "a directory without the programs is an error");
  options.path = "relative";
  check(sc_runtime_inspect(&options, &info, &error) == SC_INVALID_ARGUMENT, "a relative path is invalid");

  options.path = path.c_str();
  options.signing_requirement = "anchor apple generic and certificate leaf[subject.OU] = \"WTD7AF5L23\"";
#ifdef __APPLE__
  check(sc_runtime_inspect(&options, &info, &error) == SC_ERROR && !info.signature_verified,
        "unsigned programs fail a signing requirement");
#else
  check(sc_runtime_inspect(&options, &info, &error) == SC_UNSUPPORTED, "signing requirements are macOS only");
#endif
  sc_runtime_options bad = options;
  bad.abi_version = 99;
  check(sc_runtime_inspect(&bad, &info, &error) == SC_INVALID_ARGUMENT, "a wrong ABI version is refused");
}

struct Opened {
  sc_session* session = nullptr;
  sc_status status = SC_ERROR;
  sc_error error{};
};
Opened open(const sc_runtime_info* runtime, const std::string& owner, sc_session_role role = SC_ROLE_HOST,
            const char* grant = nullptr, bool start = false, uint32_t flags = 0) {
  static const auto dir = (root / "run").string(), profile = (root / "profile").string();
  sc_session_options options;
  sc_session_options_init(&options);
  options.runtime = runtime;
  options.name = "embed";
  options.directory = dir.c_str();
  options.profile = profile.c_str();
  options.owner = owner.c_str();
  options.role = role;
  options.grant = grant;
  options.client_name = "embed_test";
  options.scale = 2;
  options.timeout_ms = 10000;
  options.flags = flags | (start ? SC_SESSION_START : 0u);
  Opened opened;
  opened.status = sc_session_open(&options, &opened.session, &opened.error);
  return opened;
}

void session_flow() {
  const auto dir = make_runtime("session-runtime", service_binary);
  fs::create_directory(root / "run", root);
  fs::permissions(root / "run", fs::perms::owner_all);
  fs::create_directory(root / "profile");
  sc_runtime_options options;
  sc_runtime_options_init(&options);
  const auto path = dir.string();
  options.path = path.c_str();
  sc_runtime_info info;
  sc_runtime_info_init(&info);
  sc_error error;
  require(sc_runtime_inspect(&options, &info, &error) == SC_OK, "inspect");

  check(open(&info, "host").status == SC_CLOSED, "joining a session that is not running is SC_CLOSED");
  {
    // A socket path AF_UNIX cannot hold is refused before anything starts.
    const auto deep = (root / std::string(120, 'd')).string();
    sc_session_options options;
    sc_session_options_init(&options);
    options.runtime = &info;
    options.name = "embed";
    options.directory = deep.c_str();
    const auto profile = (root / "profile").string();
    options.profile = profile.c_str();
    options.owner = "host";
    options.flags = SC_SESSION_START;
    sc_session* session = nullptr;
    const auto limit = std::to_string(sizeof(sockaddr_un{}.sun_path) - 1);
    check(sc_session_open(&options, &session, &error) == SC_INVALID_ARGUMENT && !session &&
              std::string(error.message).find("at most " + limit) != std::string::npos,
          std::string("an overlong socket path names the limit: ") + error.message);
    check(!fs::exists(deep), "an overlong socket path starts nothing");
  }
  auto host_opened = open(&info, "host", SC_ROLE_HOST, nullptr, true);
  require(host_opened.status == SC_OK, std::string("start a session: ") + host_opened.error.message);
  sc_session* host = host_opened.session;
  sc_json* described = nullptr;
  require(sc_session_describe(host, &described, &error) == SC_OK, "describe");
  const auto hello = Json::parse(text(described));
  check(hello.at("protocol") == Json(1) && hello.at("role") == Json("host") && hello.at("engine").at("pid").as_int() > 0,
        "the hello names protocol 1, the role and the engine");
  check(open(&info, "bad owner!").status == SC_INVALID_ARGUMENT, "an unsafe owner is refused before connecting");

  // Tabs.
  sc_tab tab;
  sc_tab_init(&tab);
  require(sc_tab_open(host, "https://a.test/", 0, &tab, &error) == SC_OK, std::string("open a tab: ") + error.message);
  const int page = tab.page;
  check(page > 1 && tab.control == SC_CONTROL_UNCLAIMED, "a host's tab opens unclaimed");
  check(tabs(host).as_array().size() == 1, "the owner lists its tab");
  auto other = open(&info, "someone-else");
  require(other.status == SC_OK, "join as another owner");
  check(tabs(other.session).as_array().empty(), "another owner sees none of these tabs");
  check(sc_tab_close(other.session, page, &error) == SC_REFUSED && refusal(error) == "unknown_page",
        "another owner cannot close the tab: unknown_page");
  check(sc_tab_open(host, "file:///etc/passwd", 0, &tab, &error) == SC_REFUSED && refusal(error) == "invalid_url",
        "a scheme the session does not allow is refused");
  check(sc_tab_navigate(host, page, "https://b.test/", &tab, &error) == SC_OK && tab.control == SC_CONTROL_HUMAN,
        "navigating takes the tab for the person");
  const auto taken = tab.generation;
  check(sc_tab_history(host, page, SC_HISTORY_BACK, &tab, &error) == SC_OK, "history back");
  check(sc_tab_release(host, page, &tab, &error) == SC_OK && tab.control == SC_CONTROL_UNCLAIMED && tab.generation > taken,
        "releasing hands the tab back, with a new generation");
  check(sc_tab_take(host, page, &tab, &error) == SC_OK && tab.control == SC_CONTROL_HUMAN, "take");
  const auto shot = (root / "shot.png").string();
  check(sc_tab_screenshot(host, page, shot.c_str(), &error) == SC_OK && fs::file_size(shot) == 8,
        std::string("a screenshot is written to a file: ") + error.message);

  // Agents: a grant, the person's tab refused, the agent's own tab driven.
  char token[65];
  require(sc_session_grant(host, "core,debug", 0, token, sizeof token, &error) == SC_OK, std::string("grant: ") + error.message);
  check(std::strlen(token) == 64, "a grant token is 64 hex characters");
  auto agent = open(nullptr, "host", SC_ROLE_AGENT, token);
  require(agent.status == SC_OK, std::string("an agent joins with the grant: ") + agent.error.message);
  sc_json* reply = nullptr;
  const char* arguments = "{\"expression\":\"1\"}";
  check(sc_agent_call(agent.session, page, "evaluate", arguments, std::strlen(arguments), 0, 0, &reply, &error) == SC_REFUSED &&
            refusal(error) == "human_in_control",
        "an agent is refused on the person's tab: human_in_control");
  sc_tab mine;
  sc_tab_init(&mine);
  require(sc_tab_open(agent.session, nullptr, 0, &mine, &error) == SC_OK, "an agent opens its own tab");
  check(mine.control == SC_CONTROL_AGENT, "an agent's tab is the agent's");
  check(sc_agent_call(agent.session, mine.page, "evaluate", arguments, std::strlen(arguments), mine.generation, 0, &reply,
                      &error) == SC_OK,
        std::string("the agent drives its tab: ") + error.message);
  if (reply) check(Json::parse(text(reply)).contains("result"), "a call answers with its result");
  check(sc_agent_call(agent.session, mine.page, "evaluate", arguments, std::strlen(arguments), mine.generation + 5, 0,
                      &reply, &error) == SC_REFUSED && refusal(error) == "stale_generation",
        "a stale generation is refused");
  sc_json* catalog = nullptr;
  check(sc_agent_catalog(agent.session, &catalog, &error) == SC_OK &&
            !Json::parse(text(catalog)).at("tools").as_array().empty(),
        "the agent reads its catalog");
  check(sc_session_grant(agent.session, nullptr, 0, token, sizeof token, &error) == SC_REFUSED, "an agent cannot grant");
  require(sc_session_grant(host, nullptr, 0, token, sizeof token, &error) == SC_OK, "a second grant");
  auto revoked = open(nullptr, "host", SC_ROLE_AGENT, token);
  require(revoked.status == SC_OK, "a second agent joins");
  check(sc_session_revoke(host, token, &error) == SC_OK, "revoke");
  check(sc_tab_list(revoked.session, &reply, &error) == SC_REFUSED && refusal(error) == "not_granted",
        "a revoked grant stops the agent's next call");

  // Events: the first is a resync, then this owner's changes.
  sc_event* event = nullptr;
  require(sc_session_next_event(host, 2000, &event, &error) == SC_OK, std::string("first event: ") + error.message);
  check(sc_event_kind_of(event) == SC_EVENT_RESYNC, "the first event is a resync");
  sc_event_destroy(event);
  sc_tab_release(host, page, &tab, &error);
  bool changed = false;
  while (sc_session_next_event(host, 2000, &event, &error) == SC_OK) {
    changed = changed || (sc_event_kind_of(event) == SC_EVENT_TAB_CHANGED && sc_event_page(event) == page);
    sc_event_destroy(event);
    if (changed) break;
  }
  check(changed, "releasing the tab is a tab.changed event for it");
  check(sc_session_next_event(host, 200, &event, &error) == SC_TIMEOUT, "a quiet cursor times out");

  // An input-only view: no frames, but input reaches the engine.
  const auto profile = root / "profile";
  sc_view_options view_options;
  sc_view_options_init(&view_options);
  view_options.page = page;
  view_options.css_width = 640;
  view_options.css_height = 480;
  sc_view* input_view = nullptr;
  require(sc_view_attach(host, &view_options, &input_view, &error) == SC_OK, std::string("an input-only view: ") + error.message);
  sc_pointer_event pointer;
  sc_pointer_event_init(&pointer);
  pointer.x = 10;
  pointer.y = 20;
  check(sc_view_pointer(input_view, &pointer, &error) == SC_OK, "a move");
  pointer.action = SC_POINTER_DOWN;
  check(sc_view_pointer(input_view, &pointer, &error) == SC_OK, "a press");
  pointer.action = SC_POINTER_UP;
  check(sc_view_pointer(input_view, &pointer, &error) == SC_OK, "a release");
  sc_wheel_event wheel;
  sc_wheel_event_init(&wheel);
  wheel.delta_y = -40;
  check(sc_view_wheel(input_view, &wheel, &error) == SC_OK, "a wheel");
  sc_key_event key;
  sc_key_event_init(&key);
  key.key = "Enter";
  check(sc_view_key(input_view, &key, &error) == SC_OK, "a key down");
  key.down = 0;
  check(sc_view_key(input_view, &key, &error) == SC_OK, "a key up");
  check(sc_view_text(input_view, "h\xc3\xa9", 3, &error) == SC_OK, "text");
  // Moves from several threads at once all succeed; waiting ones coalesce.
  std::vector<std::thread> movers;
  std::atomic<int> moved{0};
  for (int t = 0; t < 4; ++t)
    movers.emplace_back([&, t] {
      for (int i = 0; i < 50; ++i) {
        sc_pointer_event move;
        sc_pointer_event_init(&move);
        move.x = t * 100 + i;
        move.y = i;
        sc_error ignored;
        if (sc_view_pointer(input_view, &move, &ignored) == SC_OK) ++moved;
      }
    });
  for (auto& mover : movers) mover.join();
  check(moved == 200, "concurrent moves all succeed");
  int inputs = 0, moves = 0, viewports = 0;
  bool typed = false;
  for (const auto& command : commands(profile)) {
    if (command.at("command") == Json("viewport") && command.at("width") == Json(640)) ++viewports;
    if (command.at("command") != Json("input")) continue;
    ++inputs;
    if (command.at("action") == Json("move")) ++moves;
    if (command.at("action") == Json("insertText") && command.at("text") == Json("h\xc3\xa9")) typed = true;
  }
  check(viewports == 1 && typed && inputs >= 7, "the view's size and input reached the engine");
  check(moves <= 204, "moves are sent at most once each");
  check(sc_view_pointer(nullptr, &pointer, &error) == SC_INVALID_ARGUMENT, "a null view is invalid");
  sc_view_destroy(input_view);

  // Frames.
  sc_surface_sink sink;
  sc_surface_sink_init(&sink);
  view_options.sink = &sink;
  view_options.css_width = 400;
  view_options.css_height = 300;
  sc_view* view = nullptr;
#ifdef __APPLE__
  require(sc_view_attach(host, &view_options, &view, &error) == SC_OK, std::string("attach a library sink: ") + error.message);
  sc_frame frame;
  sc_frame_init(&frame);
  check(sc_view_next_frame(view, 5000, &frame, &error) == SC_OK, std::string("a frame arrives: ") + error.message);
  check(frame.width == 800 && frame.height == 600 && frame.pixel_format == SC_PIXEL_NV12_FULL_601 && frame.surface,
        "the frame is the CSS size times the scale, in 420f, with its surface");
  if (frame.surface) {
    const auto surface = static_cast<IOSurfaceRef>(frame.surface);
    check(IOSurfaceGetWidth(surface) == 800 && IOSurfaceIsInUse(surface), "the surface is in use while the frame is held");
  }
  sc_frame_release(&frame);
  check(!frame.surface, "releasing clears the frame");
  std::this_thread::sleep_for(500ms);
  check(sc_view_next_frame(view, 200, &frame, &error) == SC_TIMEOUT, "a forged frame from another process is dropped");
  check(sc_view_resize(view, 300, 200, &error) == SC_OK, "resize");
  check(sc_view_next_frame(view, 5000, &frame, &error) == SC_OK && frame.width == 600 && frame.height == 400 &&
            frame.received >= 2,
        "after a resize the frames have the new size");
  sc_frame_release(&frame);
  sc_view* second = nullptr;
  check(sc_view_attach(host, &view_options, &second, &error) == SC_REFUSED && refusal(error) == "view_busy",
        "a second viewer is refused while one is attached");
  view_options.replace = 1;
  check(sc_view_attach(host, &view_options, &second, &error) == SC_OK, "replace takes the view over");
  sc_view_destroy(second);
  check(sc_view_detach(view, &error) == SC_OK, "detach");
  sc_view_destroy(view);
  sink.kind = SC_SINK_MACH_BOOTSTRAP;
  sink.name = "bad name";
  check(sc_view_attach(host, &view_options, &view, &error) == SC_REFUSED && refusal(error) == "invalid_request",
        "an unsafe bootstrap name is refused");
#else
  check(sc_view_attach(host, &view_options, &view, &error) == SC_UNSUPPORTED, "library sinks are macOS only");
  sink.kind = SC_SINK_MACH_BOOTSTRAP;
  sink.name = "com.example.receiver";
  check(sc_view_attach(host, &view_options, &view, &error) == SC_UNSUPPORTED, "Mach sinks are macOS only");
#endif

  // Host events and prompts (phase 3).
  {
    check(open(nullptr, "host", SC_ROLE_HOST, nullptr, false, 4u).status == SC_INVALID_ARGUMENT,
          "an unknown session flag is refused");
    auto prompts = open(nullptr, "host", SC_ROLE_HOST, nullptr, false, SC_SESSION_PROMPTS);
    require(prompts.status == SC_OK, std::string("a host that answers prompts joins: ") + prompts.error.message);
    sc_json* described = nullptr;
    require(sc_session_describe(host, &described, &error) == SC_OK, "describe");
    const auto hello = Json::parse(text(described));
    std::set<std::string> capabilities;
    for (const auto& name : hello.at("capabilities").as_array()) capabilities.insert(name.as_string());
    check(capabilities.contains("tabs.stop") && capabilities.contains("input.ime") && capabilities.contains("prompts"),
          "the session lists host events");
    // Drain to now, so what follows is this section's.
    while (sc_session_next_event(host, 100, &event, &error) == SC_OK) sc_event_destroy(event);
    sc_tab loaded;
    sc_tab_init(&loaded);
    require(sc_tab_open(host, "https://embed.test/", 0, &loaded, &error) == SC_OK, std::string("open: ") + error.message);
    bool finished = false, titled = false;
    while ((!finished || !titled) && sc_session_next_event(host, 3000, &event, &error) == SC_OK) {
      if (sc_event_page(event) == loaded.page) {
        size_t size = 0;
        const char* data = sc_event_json(event, &size);  // before reading size
        const auto body = Json::parse(std::string_view(data, size));
        if (sc_event_kind_of(event) == SC_EVENT_TAB_LOADING && body.at("loading") == Json(false)) finished = true;
        if (sc_event_kind_of(event) == SC_EVENT_TAB_TITLE && body.at("title") == Json("Title of https://embed.test/"))
          titled = true;
      }
      sc_event_destroy(event);
    }
    check(finished && titled, "loading and title arrive as SC_EVENT_TAB_LOADING and SC_EVENT_TAB_TITLE");
    check(sc_tab_stop(host, loaded.page, &loaded, &error) == SC_OK && loaded.control == SC_CONTROL_HUMAN,
          "sc_tab_stop answers and takes the tab");
    sc_tab_release(host, loaded.page, &loaded, &error);

    sc_view_options ime_options;
    sc_view_options_init(&ime_options);
    ime_options.page = loaded.page;
    sc_view* typing = nullptr;
    require(sc_view_attach(host, &ime_options, &typing, &error) == SC_OK, "an input view for IME");
    sc_ime_composition composition;
    sc_ime_composition_init(&composition);
    composition.text = "\xe3\x81\x8b";  // か
    composition.size = 3;
    composition.selection_start = composition.selection_end = 1;
    check(sc_view_ime_set(typing, &composition, &error) == SC_OK, std::string("sc_view_ime_set: ") + error.message);
    composition.replace_start = 0;
    composition.replace_end = 1;
    check(sc_view_ime_commit(typing, &composition, &error) == SC_OK, "sc_view_ime_commit");
    check(sc_view_ime_cancel(typing, &error) == SC_OK, "sc_view_ime_cancel");
    composition.selection_start = 2;
    composition.selection_end = 1;
    check(sc_view_ime_set(typing, &composition, &error) == SC_INVALID_ARGUMENT, "a backwards selection is invalid");
    check(sc_view_edit(typing, SC_EDIT_SELECT_ALL, &error) == SC_OK && sc_view_edit(typing, SC_EDIT_COPY, &error) == SC_OK,
          "sc_view_edit");
    check(sc_view_edit(typing, static_cast<sc_edit_command>(99), &error) == SC_INVALID_ARGUMENT, "an unknown edit is invalid");
    std::vector<std::string> ops;
    for (const auto& command : commands(profile))
      if (command.at("command") == Json("host") && command.at("pageId") == Json(loaded.page))
        ops.push_back(command.at("action").as_string() + ":" +
                      (command.contains("op") ? command.at("op").as_string()
                       : command.contains("edit") ? command.at("edit").as_string() : ""));
    check(ops == std::vector<std::string>{"stop:", "ime:set", "ime:commit", "ime:cancel", "edit:selectAll", "edit:copy"},
          "stop, IME and edits reach the engine in order");
    sc_view_destroy(typing);
    sc_tab_release(host, loaded.page, &loaded, &error);

    // A dialog, held because a prompt host is connected, answered by the host.
    const char raise[] = R"({"expression":"dialog:confirm:Proceed?"})";
    sc_json* called = nullptr;
    require(sc_agent_call(host, loaded.page, "evaluate", raise, sizeof raise - 1, 0, 0, &called, &error) == SC_OK,
            std::string("raise a dialog: ") + error.message);
    sc_json_destroy(called);
    std::string id;
    while (id.empty() && sc_session_next_event(host, 3000, &event, &error) == SC_OK) {
      if (sc_event_kind_of(event) == SC_EVENT_PROMPT_OPENED && sc_event_page(event) == loaded.page) {
        size_t size = 0;
        const char* data = sc_event_json(event, &size);  // before reading size
        const auto body = Json::parse(std::string_view(data, size));
        if (body.at("prompt").at("message") == Json("Proceed?")) id = body.at("prompt").at("id").as_string();
      }
      sc_event_destroy(event);
    }
    require(!id.empty(), "the dialog is SC_EVENT_PROMPT_OPENED");
    check(sc_prompt_answer_dialog(host, id.c_str(), 0, nullptr, &error) == SC_OK, "sc_prompt_answer_dialog");
    check(sc_prompt_answer_dialog(host, id.c_str(), 1, nullptr, &error) == SC_REFUSED && refusal(error) == "prompt_gone",
          "a second answer is prompt_gone");
    bool closed = false;
    while (!closed && sc_session_next_event(host, 3000, &event, &error) == SC_OK) {
      if (sc_event_kind_of(event) == SC_EVENT_PROMPT_CLOSED && sc_event_page(event) == loaded.page) {
        size_t size = 0;
        const char* data = sc_event_json(event, &size);  // before reading size
        const auto body = Json::parse(std::string_view(data, size));
        closed = body.at("prompt") == Json(id) && body.at("outcome") == Json("dismissed");
      }
      sc_event_destroy(event);
    }
    check(closed, "the answered dialog is SC_EVENT_PROMPT_CLOSED, dismissed");
    check(sc_prompt_answer_dialog(host, nullptr, 1, nullptr, &error) == SC_INVALID_ARGUMENT, "a prompt is named");
    sc_session_close(prompts.session, 0, &error);
    sc_session_destroy(prompts.session);
    check(sc_tab_close(host, loaded.page, &error) == SC_OK, "close the host events tab");
  }

  // Closing: the agent may not shut the session down; the host may.
  check(sc_session_close(agent.session, 1, &error) == SC_REFUSED && refusal(error) == "forbidden",
        "an agent cannot shut the session down");
  check(sc_tab_list(agent.session, &reply, &error) == SC_CLOSED, "but its own handle is closed");
  sc_session_destroy(agent.session);
  sc_session_destroy(revoked.session);
  sc_session_destroy(other.session);
  check(sc_session_close(host, 1, &error) == SC_OK, std::string("the host shuts the session down: ") + error.message);
  check(sc_session_close(host, 1, &error) == SC_OK, "close is idempotent");
  check(sc_tab_list(host, &reply, &error) == SC_CLOSED, "a closed session's calls are SC_CLOSED");
  check(eventually([] { return !fs::exists(root / "run" / "embed.sock"); }, 10s), "the service withdraws its socket");
  sc_session_destroy(host);
}

void engine_exit() {
  const auto dir = make_runtime("exit-runtime", service_binary);
  const auto run = root / "exit-run", profile = root / "exit-profile";
  fs::create_directory(run);
  fs::permissions(run, fs::perms::owner_all);
  fs::create_directory(profile);
  sc_runtime_options options;
  sc_runtime_options_init(&options);
  const auto path = dir.string();
  options.path = path.c_str();
  sc_runtime_info info;
  sc_runtime_info_init(&info);
  sc_error error;
  require(sc_runtime_inspect(&options, &info, &error) == SC_OK, "inspect");
  sc_session_options session_options;
  sc_session_options_init(&session_options);
  const auto run_text = run.string(), profile_text = profile.string();
  session_options.runtime = &info;
  session_options.name = "exits";
  session_options.directory = run_text.c_str();
  session_options.profile = profile_text.c_str();
  session_options.owner = "host";
  session_options.flags = SC_SESSION_START;
  sc_session* session = nullptr;
  require(sc_session_open(&session_options, &session, &error) == SC_OK, std::string("start: ") + error.message);
  sc_tab tab;
  sc_tab_init(&tab);
  require(sc_tab_open(session, nullptr, 1, &tab, &error) == SC_OK, "open an agent tab");
  sc_event* event = nullptr;
  require(sc_session_next_event(session, 2000, &event, &error) == SC_OK, "first event");
  sc_event_destroy(event);
  const char* arguments = "{\"expression\":\"exit\"}";
  sc_json* reply = nullptr;
  sc_agent_call(session, tab.page, "evaluate", arguments, std::strlen(arguments), 0, 3000, &reply, &error);
  if (reply) sc_json_destroy(reply);
  bool exited = false;
  const auto deadline = Clock::now() + 15s;
  while (!exited && Clock::now() < deadline) {
    const auto status = sc_session_next_event(session, 1000, &event, &error);
    if (status == SC_OK) {
      exited = sc_event_kind_of(event) == SC_EVENT_ENGINE_EXITED;
      sc_event_destroy(event);
    } else if (status != SC_TIMEOUT) {
      break;
    }
  }
  check(exited, "an engine that exits is an engine.exited event");
  sc_session_close(session, 0, &error);
  sc_session_destroy(session);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: embed_test SERVICE FAKE_ENGINE\n";
    return 2;
  }
  service_binary = argv[1];
  engine_binary = argv[2];
  char pattern[] = "/tmp/sc-embed-XXXXXX";
  require(::mkdtemp(pattern) != nullptr, "temporary directory");
  root = pattern;
  try {
    check(sc_embed_abi_version() == SC_EMBED_ABI_VERSION && sc_abi_version() == SC_ABI_VERSION,
          "embed.h has its own ABI version and leaves client.h's alone");
    std::cerr << "-- runtime\n";
    runtime();
    std::cerr << "-- session_flow\n";
    session_flow();
    std::cerr << "-- engine_exit\n";
    engine_exit();
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << " (after: " << last_check << ")\n";
    ++failures;
  }
  // Stop anything a failed check left running before removing its directory.
  for (const auto& [name, dir] : {std::pair{"embed", "run"}, std::pair{"exits", "exit-run"}})
    if (fs::exists(root / dir / (std::string(name) + ".sock")))
      run({service_binary, "--session", name, "--session-dir", (root / dir).string(), "close"});
  fs::remove_all(root);
  if (failures) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << "embed_test: all checks passed\n";
  return 0;
}
