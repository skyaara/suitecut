// SPDX-License-Identifier: MIT
// Real-engine host events through suitecut/embed.h (Linux and macOS, headless).
// Starts its own session on a SuiteCut runtime as a host that answers prompts,
// and checks against a local page: loading state and titles, stopping a slow
// load, JavaScript dialogs answered as prompts (and closed by a navigation),
// the cursor over different elements, IME composition and edit commands, a
// popup's opener, and a person's prompts: a file chooser, permission requests,
// an HTTP login and where to save a download. No frames are needed: the view
// is input-only.
//
// usage: suitecut-native-cef_host_events RUNTIME ARTIFACTS
//   RUNTIME   SuiteCut.app, a directory holding suitecut-browser and
//             suitecut-session, or the suitecut-browser inside one of those
//   ARTIFACTS directory for results.json
//
// The session's name, folder and profile are its own and removed afterwards.
#include "suitecut/embed.h"
#include "suitecut/json.hpp"
#include "posix_fixture.hpp"

#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using suitecut::Json;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {
// Fixed layout in CSS pixels, so the pointer can find each element.
const char kPage[] =
    "<!doctype html><meta charset=utf-8><title>Host events</title><style>"
    "html,body{margin:0}#box{position:absolute;left:20px;top:20px;width:300px;height:40px}"
    "#pop{position:absolute;left:20px;top:100px;width:200px;height:40px;display:block}"
    "#busy{position:absolute;left:20px;top:180px;width:200px;height:40px;cursor:wait}"
    "</style><input id=box autocomplete=off><a id=pop href=/page?popup target=_blank>popup</a><div id=busy></div>"
    "<script>setTimeout(()=>{document.title='Changed'},300)</script>";

const char kFiles[] =
    "<!doctype html><meta charset=utf-8><title>Files</title><style>html,body{margin:0}"
    "#f{position:absolute;left:20px;top:20px;width:300px;height:40px}</style><input id=f type=file>"
    "<script>f.onchange=()=>{document.title='files:'+[...f.files].map(x=>x.name).join(',')}</script>";
const char kLogin[] = "YW5uOnB3LWJveC00NDE3LXVubG9nZ2Vk";  // ann:pw-box-4417-unlogged
const char kPassword[] = "pw-box-4417-unlogged";
// /auth-agent (realm Agent) takes bob:pw-agent-5521, /auth-retry (realm
// Retry) carl:pw-retry-7730: realms of their own, so Chromium's auth cache for
// Box does not answer them.
const char kAgentLogin[] = "Ym9iOnB3LWFnZW50LTU1MjE=";
const char kRetryLogin[] = "Y2FybDpwdy1yZXRyeS03NzMw";

// Serves the page; /stall, a page whose image is /slow; and /slow, headers
// and then nothing for a long while. So /stall commits at once but stays
// loading until it is stopped. /files has a file input; /auth asks for a
// Basic login until it gets kLogin; /download is an attachment.
class PageServer {
 public:
  PageServer() {
    socket_ = suitecut_test::socket();
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (socket_ < 0 || ::bind(socket_, reinterpret_cast<sockaddr*>(&address), sizeof address) || ::listen(socket_, 16))
      throw std::runtime_error("Cannot start the page server");
    socklen_t size = sizeof address;
    if (::getsockname(socket_, reinterpret_cast<sockaddr*>(&address), &size)) throw std::runtime_error("Cannot read the port");
    port_ = ntohs(address.sin_port);
    thread_ = std::thread([this] { Serve(); });
  }
  ~PageServer() {
    stopped_ = true;
    if (thread_.joinable()) thread_.join();
    std::lock_guard lock(mutex_);
    for (auto& client : clients_)
      if (client.joinable()) client.join();
    if (socket_ >= 0) ::close(socket_);
  }
  std::string Url(const std::string& path) const { return "http://127.0.0.1:" + std::to_string(port_) + path; }

 private:
  static void Send(int client, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
      const auto wrote = ::send(client, data.data() + offset, data.size() - offset, MSG_NOSIGNAL_OR_ZERO);
      if (wrote <= 0) return;
      offset += static_cast<std::size_t>(wrote);
    }
  }
#ifdef MSG_NOSIGNAL
  static constexpr int MSG_NOSIGNAL_OR_ZERO = MSG_NOSIGNAL;
#else
  static constexpr int MSG_NOSIGNAL_OR_ZERO = 0;
#endif
  void Answer(int client) {
    timeval timeout{2, 0};
    ::setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
    char request[4096];
    const auto n = ::recv(client, request, sizeof request - 1, 0);
    const std::string head = n > 0 ? std::string(request, static_cast<std::size_t>(n)) : "";
    if (head.starts_with("GET /auth")) {
      const bool agent = head.starts_with("GET /auth-agent"), retry = head.starts_with("GET /auth-retry");
      const std::string login = agent ? kAgentLogin : retry ? kRetryLogin : kLogin;
      const std::string realm = agent ? "Agent" : retry ? "Retry" : "Box";
      if (head.find("Authorization: Basic " + login + "\r\n") == std::string::npos) {
        const std::string body = "<!doctype html><title>Login needed</title>";
        Send(client, "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"" + realm + "\"\r\nContent-Type: text/html\r\n"
                     "Connection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
      } else {
        const std::string body = "<!doctype html><title>Authed</title>";
        Send(client, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: " +
                         std::to_string(body.size()) + "\r\n\r\n" + body);
      }
    } else if (head.starts_with("GET /download")) {
      const std::string body = "downloaded-bytes";
      Send(client, "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\n"
                   "Content-Disposition: attachment; filename=\"report.txt\"\r\nConnection: close\r\n"
                   "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    } else if (head.starts_with("GET /slow")) {
      Send(client, "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n");
      // Hold the load open, but let the server stop promptly.
      for (int i = 0; i < 300 && !stopped_; ++i) std::this_thread::sleep_for(100ms);
    } else {
      const std::string body = head.starts_with("GET /page")    ? kPage
                               : head.starts_with("GET /stall") ? "<!doctype html><title>Stalled</title><img src=/slow>"
                               : head.starts_with("GET /files") ? kFiles
                                                                : "<!doctype html><title>other</title>";
      Send(client, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\n"
                   "Connection: close\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
    }
    ::close(client);
  }
  void Serve() {
    while (!stopped_) {
      const int client = suitecut_test::accept(socket_);
      if (client < 0) continue;
      std::lock_guard lock(mutex_);
      clients_.emplace_back([this, client] { Answer(client); });
    }
  }
  int socket_ = -1;
  unsigned port_ = 0;
  std::atomic_bool stopped_{false};
  std::thread thread_;
  std::mutex mutex_;
  std::vector<std::thread> clients_;
};

class Run {
 public:
  explicit Run(fs::path artifacts) : artifacts_(std::move(artifacts)) {}
  Json report{{"ok", false}, {"stage", "start"}, {"closed", false}, {"checks", Json::array{}}, {"notes", Json::array{}}};

  void check(bool ok, const std::string& name, Json detail = nullptr) {
    Json row{{"name", name}, {"ok", ok}};
    if (!detail.is_null()) row["detail"] = std::move(detail);
    report["checks"].as_array().push_back(std::move(row));
    if (!ok) throw std::runtime_error("Check failed: " + name);
  }
  // Recorded, not required: behaviour that depends on the platform.
  void note(const std::string& name, Json detail) {
    report["notes"].as_array().push_back(Json{{"name", name}, {"detail", std::move(detail)}});
  }
  void stage(const std::string& name) { report["stage"] = name; }
  void write() {
    fs::create_directories(artifacts_);
    std::ofstream(artifacts_ / "results.json") << report.dump() << "\n";
  }

 private:
  fs::path artifacts_;
};

std::string text_of(sc_json* json) {
  std::size_t size = 0;
  const char* data = sc_json_data(json, &size);
  std::string out(data ? data : "", size);
  sc_json_destroy(json);
  return out;
}

// The session's events, kept, so a wait can look back as well as ahead.
class Events {
 public:
  explicit Events(sc_session* session) : session_(session) {}
  // Events from here on are what later waits look at.
  void mark() { from_ = seen_.size(); }
  Json wait(const std::function<bool(sc_event_kind, const Json&)>& match, std::chrono::milliseconds limit = 15s) {
    const auto deadline = Clock::now() + limit;
    std::size_t at = from_;
    for (;;) {
      for (; at < seen_.size(); ++at)
        if (match(kinds_[at], seen_[at])) return seen_[at];
      const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
      if (left <= 0) return Json{};
      sc_event* event = nullptr;
      sc_error error;
      const auto status = sc_session_next_event(session_, static_cast<uint32_t>(std::min<long long>(left, 1000)), &event, &error);
      if (status == SC_TIMEOUT) continue;
      if (status != SC_OK) throw std::runtime_error(std::string("next event: ") + error.message);
      std::size_t size = 0;
      const char* data = sc_event_json(event, &size);
      kinds_.push_back(sc_event_kind_of(event));
      seen_.push_back(Json::parse(std::string_view(data, size)));
      sc_event_destroy(event);
    }
  }
  Json all() const {
    Json::array out;
    for (const auto& event : seen_) out.push_back(event);
    return out;
  }

 private:
  sc_session* session_;
  std::vector<Json> seen_;
  std::vector<sc_event_kind> kinds_;
  std::size_t from_ = 0;
};

fs::path runtime_from(const fs::path& given) {
  if (fs::is_regular_file(given)) return given.parent_path();  // the engine inside a runtime
  return given;
}

int host_events_main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: suitecut-native-cef_host_events RUNTIME ARTIFACTS\n";
    return 2;
  }
  Run run(fs::absolute(argv[2]));
  fs::remove(fs::absolute(argv[2]) / "results.json");
  char session_dir[] = "/tmp/sc-he-XXXXXX";  // short: the socket path must fit sun_path
  char profile_dir[] = "/tmp/sc-he-profile-XXXXXX";
  if (!::mkdtemp(session_dir) || !::mkdtemp(profile_dir)) {
    std::perror("mkdtemp");
    return 1;
  }
  sc_session* session = nullptr;
  sc_view* view = nullptr;
  int engine_pid = 0;
  std::unique_ptr<Events> events;
  try {
    const auto runtime_path = fs::absolute(runtime_from(argv[1])).lexically_normal().string();
    run.stage("inspect");
    sc_runtime_options runtime_options;
    sc_runtime_options_init(&runtime_options);
    runtime_options.path = runtime_path.c_str();
    sc_runtime_info runtime;
    sc_runtime_info_init(&runtime);
    sc_error error;
    if (sc_runtime_inspect(&runtime_options, &runtime, &error) != SC_OK)
      throw std::runtime_error(std::string("inspect: ") + error.message);
    run.report["runtime"] = Json{{"path", runtime_path}, {"service", runtime.service_version}};

    run.stage("session");
    PageServer pages;
    sc_session_options options;
    sc_session_options_init(&options);
    options.runtime = &runtime;
    options.name = "host-events";
    options.directory = session_dir;
    options.profile = profile_dir;
    options.owner = "events";
    options.client_name = "cef_host_events";
    options.timeout_ms = 20000;
    options.flags = SC_SESSION_START | SC_SESSION_PROMPTS;
    if (sc_session_open(&options, &session, &error) != SC_OK) throw std::runtime_error(std::string("open: ") + error.message);
    sc_json* described = nullptr;
    if (sc_session_describe(session, &described, &error) != SC_OK) throw std::runtime_error("describe");
    const auto hello = Json::parse(text_of(described));
    engine_pid = static_cast<int>(hello.at("engine").at("pid").as_int());
    std::vector<std::string> capabilities;
    for (const auto& name : hello.at("capabilities").as_array()) capabilities.push_back(name.as_string());
    const auto has = [&](const char* name) { return std::find(capabilities.begin(), capabilities.end(), name) != capabilities.end(); };
    run.check(has("tabs.stop") && has("tab.loading") && has("tab.title") && has("view.cursor") && has("input.ime") &&
                  has("input.edit") && has("prompts"),
              "the real engine reports host events", hello.at("capabilities"));
    events = std::make_unique<Events>(session);
    events->wait([](sc_event_kind kind, const Json&) { return kind == SC_EVENT_RESYNC; }, 5s);

    // 1. Loading state and titles.
    run.stage("loading");
    events->mark();
    sc_tab tab;
    sc_tab_init(&tab);
    if (sc_tab_open(session, pages.Url("/page").c_str(), 0, &tab, &error) != SC_OK)
      throw std::runtime_error(std::string("open tab: ") + error.message);
    const int page = tab.page;
    const auto on = [page](sc_event_kind want) {
      return [page, want](sc_event_kind kind, const Json& event) {
        return kind == want && ((event.contains("page") && event.at("page") == Json(page)) ||
                                (event.contains("view") && event.at("view") == Json(page)));
      };
    };
    const auto started = events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_LOADING)(kind, event) && event.at("loading") == Json(true);
    });
    run.check(started.is_object(), "a load start is SC_EVENT_TAB_LOADING", started);
    const auto finished = events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_LOADING)(kind, event) && event.at("loading") == Json(false);
    });
    // The first navigation replaces the new tab's about:blank, so whether
    // anything is behind it is the engine's; nothing is ahead.
    run.check(finished.is_object() && finished.at("canGoForward") == Json(false), "the load finishes", finished);
    const auto titled = events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_TITLE)(kind, event) && event.at("title") == Json("Host events");
    });
    run.check(titled.is_object(), "the page's title is SC_EVENT_TAB_TITLE");
    const auto changed = events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_TITLE)(kind, event) && event.at("title") == Json("Changed");
    });
    run.check(changed.is_object(), "a script's title change is SC_EVENT_TAB_TITLE");
    double best = 0;
    const auto so_far = events->all();  // named: a temporary would end before the loop
    for (const auto& event : so_far.as_array())
      if (event.at("type") == Json("tab.loading") && event.at("page") == Json(page))
        best = std::max(best, event.at("progress").as_double());
    run.check(best == 1, "progress reaches 1", best);
    sc_json* listed = nullptr;
    if (sc_tab_list(session, &listed, &error) != SC_OK) throw std::runtime_error("list");
    const auto tabs = Json::parse(text_of(listed));
    bool listed_state = false;
    for (const auto& entry : tabs.at("tabs").as_array())
      if (entry.at("page") == Json(page))
        listed_state = entry.at("loading") == Json(false) && entry.at("canGoBack") == finished.at("canGoBack") &&
                       entry.at("title") == Json("Changed");
    run.check(listed_state, "the tab lists as loaded", tabs);

    // Agent calls claim the tab, so the person lets it go first.
    const auto evaluate = [&](const std::string& expression) {
      sc_tab released;
      sc_tab_init(&released);
      sc_tab_release(session, page, &released, &error);
      const auto arguments = Json{{"expression", expression}}.dump();
      sc_json* reply = nullptr;
      if (sc_agent_call(session, page, "evaluate", arguments.data(), arguments.size(), 0, 0, &reply, &error) != SC_OK)
        throw std::runtime_error("evaluate " + expression + ": " + error.message);
      return text_of(reply);
    };
    const auto value_is = [&](const std::string& expression, const std::string& expected) {
      return evaluate("String(" + expression + ")").find("\"" + expected + "\"") != std::string::npos;
    };
    // For a value a page sets once a promise or callback runs.
    const auto settles = [&](const std::string& expression, const std::string& expected) {
      const auto deadline = Clock::now() + 5s;
      while (Clock::now() < deadline) {
        if (value_is(expression, expected)) return true;
        std::this_thread::sleep_for(100ms);
      }
      return false;
    };

    // 2. Stopping a slow load. tabs.navigate returns at the commit, while
    // the page's image still hangs.
    run.stage("stop");
    events->mark();
    const auto navigate_at = Clock::now();
    const auto committed = sc_tab_navigate(session, page, pages.Url("/stall").c_str(), &tab, &error);
    const auto commit_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - navigate_at).count();
    run.check(committed == SC_OK && commit_ms < 10000,
              "sc_tab_navigate returns at the commit of a page still loading",
              Json{{"ms", commit_ms}, {"error", committed == SC_OK ? "" : error.message}});
    const auto slow = events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_LOADING)(kind, event) && event.at("loading") == Json(true);
    });
    run.check(slow.is_object(), "the slow page is loading");
    sc_json* still = nullptr;
    sc_tab_list(session, &still, &error);
    const auto still_tabs = Json::parse(text_of(still));
    bool still_loading = false;
    for (const auto& entry : still_tabs.at("tabs").as_array())
      if (entry.at("page") == Json(page)) still_loading = entry.at("loading") == Json(true);
    run.check(still_loading, "and it is still loading after the navigation returned", still_tabs);
    std::this_thread::sleep_for(500ms);
    const auto stop_at = Clock::now();
    events->mark();
    run.check(sc_tab_stop(session, page, &tab, &error) == SC_OK && tab.control == SC_CONTROL_HUMAN,
              std::string("sc_tab_stop answers and takes the tab: ") + error.message);
    const auto stopped = events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_LOADING)(kind, event) && event.at("loading") == Json(false);
    }, 10s);
    const auto stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - stop_at).count();
    run.check(stopped.is_object() && stopped.at("canGoBack") == Json(true) && stop_ms < 5000,
              "stopping ends the load at once, well before the server would", Json{{"ms", stop_ms}, {"event", stopped}});

    // 3. Dialogs as prompts.
    run.stage("dialogs");
    if (sc_tab_navigate(session, page, pages.Url("/page").c_str(), &tab, &error) != SC_OK)
      throw std::runtime_error(std::string("navigate back: ") + error.message);
    const auto prompt_opened = [&](const std::string& dialog, const std::string& message) {
      const auto opened = events->wait([&](sc_event_kind kind, const Json& event) {
        return kind == SC_EVENT_PROMPT_OPENED && event.at("prompt").at("page") == Json(page) &&
               event.at("prompt").at("dialog") == Json(dialog) && event.at("prompt").at("message") == Json(message);
      });
      if (!opened.is_object()) throw std::runtime_error("no prompt for the " + dialog + " " + message);
      return opened.at("prompt");
    };
    const auto prompt_closed = [&](const std::string& id) {
      return events->wait([&](sc_event_kind kind, const Json& event) {
        return kind == SC_EVENT_PROMPT_CLOSED && event.at("prompt") == Json(id);
      });
    };
    events->mark();
    evaluate("setTimeout(()=>{window.answer=confirm('Proceed?')},0);1");
    const auto confirm = prompt_opened("confirm", "Proceed?");
    run.check(confirm.at("kind") == Json("dialog") && confirm.at("url").as_string().starts_with(pages.Url("")),
              "a confirm is SC_EVENT_PROMPT_OPENED", confirm);
    run.check(sc_prompt_answer_dialog(session, confirm.at("id").as_string().c_str(), 1, nullptr, &error) == SC_OK,
              std::string("the host accepts it: ") + error.message);
    const auto accepted = prompt_closed(confirm.at("id").as_string());
    run.check(accepted.is_object() && accepted.at("outcome") == Json("accepted"), "it closes, accepted", accepted);
    run.check(value_is("window.answer", "true"), "the page got true");

    events->mark();
    evaluate("setTimeout(()=>{window.answer=prompt('Name?','nobody')},0);1");
    const auto asked = prompt_opened("prompt", "Name?");
    run.check(asked.at("defaultValue") == Json("nobody"), "a prompt carries its default", asked);
    run.check(sc_prompt_answer_dialog(session, asked.at("id").as_string().c_str(), 1, "Ada", &error) == SC_OK,
              "the host answers it with text");
    run.check(prompt_closed(asked.at("id").as_string()).is_object(), "it closes");
    run.check(value_is("window.answer", "Ada"), "the page got the text");
    run.check(sc_prompt_answer_dialog(session, asked.at("id").as_string().c_str(), 1, nullptr, &error) == SC_REFUSED,
              "a second answer is refused");

    events->mark();
    evaluate("setTimeout(()=>{window.answer=confirm('Go?')},0);1");
    const auto declined = prompt_opened("confirm", "Go?");
    sc_prompt_answer_dialog(session, declined.at("id").as_string().c_str(), 0, nullptr, &error);
    run.check(prompt_closed(declined.at("id").as_string()).at("outcome") == Json("dismissed"), "a dismissed confirm");
    run.check(value_is("window.answer", "false"), "the page got false");

    // A navigation while a dialog is open closes it, unanswered.
    events->mark();
    evaluate("setTimeout(()=>{alert('Interrupted')},0);1");
    const auto interrupted = prompt_opened("alert", "Interrupted");
    // The navigation runs on its own thread, so the dialog's closing is seen
    // whether or not the navigation returns.
    std::atomic<int> navigated{-1};
    std::string navigate_error;
    std::thread navigating([&] {
      sc_tab moved;
      sc_tab_init(&moved);
      sc_error failure;
      const auto status = sc_tab_navigate(session, page, pages.Url("/page?again").c_str(), &moved, &failure);
      if (status != SC_OK) navigate_error = failure.message;
      navigated = status;
    });
    const auto reset = prompt_closed(interrupted.at("id").as_string());
    navigating.join();
    run.note("navigating while a dialog is open", Json{{"status", navigated.load()}, {"error", navigate_error}});
    run.check(reset.is_object() && reset.at("outcome") == Json("closed"), "a navigation closes an open dialog", reset);
    run.check(navigated == SC_OK, "and the navigation completes", navigate_error);
    events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_LOADING)(kind, event) && event.at("loading") == Json(false);
    });

    // 4. The cursor over different elements, through an input-only view.
    run.stage("cursor");
    sc_view_options view_options;
    sc_view_options_init(&view_options);
    view_options.page = page;
    view_options.css_width = 800;
    view_options.css_height = 600;
    if (sc_view_attach(session, &view_options, &view, &error) != SC_OK)
      throw std::runtime_error(std::string("attach: ") + error.message);
    const auto cursor_at = [&](double x, double y, const std::string& want) {
      events->mark();
      sc_pointer_event move;
      sc_pointer_event_init(&move);
      move.x = x;
      move.y = y;
      if (sc_view_pointer(view, &move, &error) != SC_OK) throw std::runtime_error(std::string("move: ") + error.message);
      const auto got = events->wait([&](sc_event_kind kind, const Json& event) {
        return on(SC_EVENT_VIEW_CURSOR)(kind, event) && event.at("cursor") == Json(want);
      }, 5s);
      return got.is_object();
    };
    run.check(cursor_at(170, 40, "text"), "over the input the cursor is text");
    run.check(cursor_at(120, 120, "pointer"), "over the link it is pointer");
    run.check(cursor_at(120, 200, "wait"), "over cursor:wait it is wait");
    run.check(cursor_at(600, 500, "default"), "over the page it is default");

    // 5. IME composition and edit commands in the input.
    run.stage("ime");
    sc_pointer_event press;
    sc_pointer_event_init(&press);
    press.x = 170;
    press.y = 40;
    press.action = SC_POINTER_DOWN;
    sc_view_pointer(view, &press, &error);
    press.action = SC_POINTER_UP;
    sc_view_pointer(view, &press, &error);
    run.check(value_is("document.activeElement.id", "box"), "a click focuses the input");
    events->mark();
    sc_ime_composition composition;
    sc_ime_composition_init(&composition);
    composition.text = "\xe3\x81\x8b\xe3\x81\xaa";  // かな
    composition.size = 6;
    run.check(sc_view_ime_set(view, &composition, &error) == SC_OK, std::string("sc_view_ime_set: ") + error.message);
    const auto ime = events->wait(on(SC_EVENT_VIEW_IME), 5s);
    run.check(ime.is_object() && ime.at("range") == Json(Json::array{0, 2}) && !ime.at("bounds").as_array().empty(),
              "the composition's range and bounds are SC_EVENT_VIEW_IME", ime);
    const auto composing = evaluate("document.getElementById('box').value");
    run.note("value while composing", composing);
    composition.text = "\xe4\xbb\xae\xe5\x90\x8d";  // 仮名
    composition.size = 6;
    run.check(sc_view_ime_commit(view, &composition, &error) == SC_OK, "sc_view_ime_commit");
    run.check(value_is("document.getElementById('box').value", "\xe4\xbb\xae\xe5\x90\x8d"), "the committed text is in the input");
    composition.text = "x";
    composition.size = 1;
    sc_view_ime_set(view, &composition, &error);
    run.check(sc_view_ime_cancel(view, &error) == SC_OK, "sc_view_ime_cancel");
    run.check(value_is("document.getElementById('box').value", "\xe4\xbb\xae\xe5\x90\x8d"), "a cancelled composition leaves nothing");

    // Edits act on the focused input; the agent calls above left focus there.
    const auto edit = [&](sc_edit_command command) {
      if (sc_view_edit(view, command, &error) != SC_OK) throw std::runtime_error(std::string("edit: ") + error.message);
    };
    edit(SC_EDIT_SELECT_ALL);
    edit(SC_EDIT_DELETE);
    run.check(value_is("document.getElementById('box').value", ""), "select all and delete empty the input");
    edit(SC_EDIT_UNDO);
    run.check(value_is("document.getElementById('box').value", "\xe4\xbb\xae\xe5\x90\x8d"), "undo brings the text back");
    edit(SC_EDIT_REDO);
    run.check(value_is("document.getElementById('box').value", ""), "redo removes it again");
    edit(SC_EDIT_UNDO);
    edit(SC_EDIT_SELECT_ALL);
    edit(SC_EDIT_CUT);
    const bool cut = value_is("document.getElementById('box').value", "");
    edit(SC_EDIT_PASTE);
    edit(SC_EDIT_PASTE);
    const auto pasted = evaluate("document.getElementById('box').value");
    run.note("clipboard: cut then paste twice", Json{{"cutEmptied", cut}, {"after", pasted}});

    // 6. A popup from a click on a target=_blank link has its opener.
    run.stage("popup");
    events->mark();
    press.x = 120;
    press.y = 120;
    press.action = SC_POINTER_DOWN;
    sc_view_pointer(view, &press, &error);
    press.action = SC_POINTER_UP;
    sc_view_pointer(view, &press, &error);
    const auto popup = events->wait([&](sc_event_kind kind, const Json& event) {
      return kind == SC_EVENT_TAB_CHANGED && event.at("tab").contains("opener") && event.at("tab").at("opener") == Json(page);
    });
    run.check(popup.is_object(), "the popup is SC_EVENT_TAB_CHANGED with its opener", popup);
    const int popup_page = static_cast<int>(popup.at("tab").at("page").as_int());
    const auto popup_loaded = events->wait([&](sc_event_kind kind, const Json& event) {
      return kind == SC_EVENT_TAB_LOADING && event.at("page") == Json(popup_page) && event.at("loading") == Json(false);
    });
    run.check(popup_loaded.is_object(), "the popup reports its own loading");

    // 7. A person's prompts.
    run.stage("person");
    char scratch[] = "/tmp/sc-he-files-XXXXXX";
    if (!::mkdtemp(scratch)) throw std::runtime_error("scratch directory");
    const fs::path chosen_dir = scratch;
    const auto chosen = (chosen_dir / "chosen.txt").string();
    std::ofstream(chosen) << "chosen";
    const auto person_opened = [&](const char* kind, std::chrono::milliseconds limit = 15s) {
      return events->wait([&](sc_event_kind event_kind, const Json& event) {
        return event_kind == SC_EVENT_PROMPT_OPENED && event.at("prompt").at("page") == Json(page) &&
               event.at("prompt").at("kind") == Json(kind);
      }, limit);
    };
    const auto click = [&](double x, double y) {
      sc_pointer_event at;
      sc_pointer_event_init(&at);
      at.x = x;
      at.y = y;
      at.action = SC_POINTER_DOWN;
      sc_view_pointer(view, &at, &error);
      at.action = SC_POINTER_UP;
      sc_view_pointer(view, &at, &error);
    };
    // A file chooser, from the person's click on a file input.
    if (sc_tab_navigate(session, page, pages.Url("/files").c_str(), &tab, &error) != SC_OK)
      throw std::runtime_error(std::string("navigate to files: ") + error.message);
    events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_LOADING)(kind, event) && event.at("loading") == Json(false);
    });
    events->mark();
    click(170, 40);
    auto opened = person_opened("file");
    run.check(opened.is_object() && opened.at("prompt").at("mode") == Json("open"),
              "a click on a file input is a file prompt", opened);
    auto id = opened.at("prompt").at("id").as_string();
    const char* relative[] = {"chosen.txt"};
    run.check(sc_prompt_answer_files(session, id.c_str(), relative, 1, &error) == SC_REFUSED,
              "a relative path is refused", error.message);
    const char* paths[] = {chosen.c_str()};
    run.check(sc_prompt_answer_files(session, id.c_str(), paths, 1, &error) == SC_OK,
              std::string("sc_prompt_answer_files: ") + error.message);
    const auto picked = events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_TITLE)(kind, event) && event.at("title") == Json("files:chosen.txt");
    });
    run.check(picked.is_object(), "the page's input has the chosen file");
    events->mark();
    click(170, 40);
    opened = person_opened("file");
    run.check(opened.is_object(), "a second click opens another file prompt");
    id = opened.at("prompt").at("id").as_string();
    run.check(sc_prompt_cancel(session, id.c_str(), &error) == SC_OK, "sc_prompt_cancel cancels it");
    run.check(events->wait([&](sc_event_kind kind, const Json& event) {
                return kind == SC_EVENT_PROMPT_CLOSED && event.at("prompt") == Json(id) && event.at("outcome") == Json("dismissed");
              }).is_object(),
              "a cancelled chooser closes, dismissed");
    events->mark();
    click(170, 40);
    opened = person_opened("file");
    id = opened.is_object() ? opened.at("prompt").at("id").as_string() : "";
    if (sc_tab_navigate(session, page, pages.Url("/page?files-left").c_str(), &tab, &error) != SC_OK)
      throw std::runtime_error(std::string("navigate away: ") + error.message);
    const auto dropped = events->wait([&](sc_event_kind kind, const Json& event) {
      return kind == SC_EVENT_PROMPT_CLOSED && event.at("prompt") == Json(id);
    });
    run.check(!id.empty() && dropped.is_object() && dropped.at("outcome") == Json("closed"),
              "a navigation closes a held file chooser", dropped);

    // Permissions: each request asks; nothing is remembered. Which requests
    // reach CEF's permission prompt is CEF's, so each kind is tried and
    // noted; one that asks must ask again.
    struct Request {
      const char* name;
      const char* start;   // sets window.answer when the page hears back
      bool allow;
    };
    const Request requests[] = {
        {"geolocation", "navigator.geolocation.getCurrentPosition(()=>{window.answer='ok'},e=>{window.answer='error:'+e.code})", false},
        {"notifications", "Notification.requestPermission().then(r=>{window.answer=r})", true},
        {"midi-sysex", "navigator.requestMIDIAccess({sysex:true}).then(()=>{window.answer='ok'},e=>{window.answer=e.name})", false},
        {"clipboard-read", "navigator.clipboard.readText().then(()=>{window.answer='ok'},e=>{window.answer=e.name})", true},
        {"camera", "navigator.mediaDevices.getUserMedia({video:true}).then(()=>{window.answer='ok'},e=>{window.answer=e.name})", false},
    };
    const Request* asking = nullptr;
    Json tried = Json::object{};
    for (const auto& request : requests) {
      events->mark();
      evaluate(std::string("window.answer=undefined;") + request.start + ";1");
      opened = person_opened("permission", 6s);
      Json entry{{"prompt", opened}};
      if (opened.is_object()) {
        id = opened.at("prompt").at("id").as_string();
        entry["answered"] = sc_prompt_answer_permission(session, id.c_str(), request.allow ? 1 : 0, &error) == SC_OK;
        entry["closed"] = events->wait([&](sc_event_kind kind, const Json& event) {
          return kind == SC_EVENT_PROMPT_CLOSED && event.at("prompt") == Json(id);
        }, 5s);
        if (!asking && opened.at("prompt").at("origin").as_string().starts_with("http://127.0.0.1")) asking = &request;
      }
      std::this_thread::sleep_for(500ms);
      entry["page"] = evaluate("String(window.answer)");
      tried[request.name] = std::move(entry);
    }
    run.note("permission requests", tried);
    // Headless Linux CEF 156 refuses every kind above before it calls
    // OnShowPermissionPrompt, so there may be nothing to answer here.
    if (!asking) run.note("no permission request reached CEF's prompt on this engine", "not checked");
    if (asking) {
      events->mark();
      evaluate(std::string(asking->start) + ";1");
      opened = person_opened("permission");
      run.check(opened.is_object(), std::string("a second ") + asking->name + " request asks again: nothing was remembered");
      if (opened.is_object()) sc_prompt_cancel(session, opened.at("prompt").at("id").as_string().c_str(), &error);
    }

    // An HTTP login, answered by the person. The page starts the load, so
    // the agent holds the tab, without credentials of its own.
    events->mark();
    evaluate("location.href='/auth';1");
    opened = person_opened("auth");
    run.check(opened.is_object() && opened.at("prompt").at("realm") == Json("Box") &&
                  opened.at("prompt").at("scheme").as_string() == "basic" &&
                  opened.at("prompt").at("proxy") == Json(false),
              "a Basic challenge is an auth prompt", opened);
    id = opened.at("prompt").at("id").as_string();
    run.check(sc_prompt_answer_login(session, id.c_str(), "ann", kPassword, &error) == SC_OK,
              std::string("sc_prompt_answer_login: ") + error.message);
    const auto authed = events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_TITLE)(kind, event) && event.at("title") == Json("Authed");
    });
    run.check(authed.is_object(), "the login reaches the server");
    std::string leaked;
    const auto everything = events->all();
    if (everything.dump().find(kPassword) != std::string::npos) leaked = "events";
    sc_json* status_json = nullptr;
    if (sc_session_describe(session, &status_json, &error) == SC_OK && text_of(status_json).find(kPassword) != std::string::npos)
      leaked = "describe";
    for (const auto& entry : fs::recursive_directory_iterator(session_dir)) {
      if (!entry.is_regular_file()) continue;
      std::ifstream in(entry.path(), std::ios::binary);
      const std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      if (contents.find(kPassword) != std::string::npos) leaked = entry.path().string();
    }
    run.check(leaked.empty(), "the password is in no event, description or session file", leaked);

    // The agent's credentials on its own tab: they answer with nobody asked.
    const auto credentials = [&](const Json& value) {
      const auto arguments = Json{{"action", "credentials"}, {"credentials", value}}.dump();
      sc_json* reply = nullptr;
      if (sc_agent_call(session, page, "network", arguments.data(), arguments.size(), 0, 0, &reply, &error) != SC_OK)
        throw std::runtime_error(std::string("network credentials: ") + error.message);
    };
    credentials(Json{{"username", "bob"}, {"password", "pw-agent-5521"}});
    events->mark();
    evaluate("location.href='/auth-agent';1");
    opened = person_opened("auth", 5s);
    const auto agent_authed = events->wait([&](sc_event_kind kind, const Json& event) {
      return on(SC_EVENT_TAB_TITLE)(kind, event) && event.at("title") == Json("Authed");
    }, 5s);
    run.check(!opened.is_object() && agent_authed.is_object(),
              "on the agent's tab its credentials log in, and nobody is asked", opened);
    // Wrong ones are tried once, then the person is asked, and asked again
    // while the answer is wrong.
    credentials(Json{{"username", "carl"}, {"password", "wrong-agent-guess"}});
    events->mark();
    evaluate("location.href='/auth-retry';1");
    opened = person_opened("auth");
    run.check(opened.is_object() && opened.at("prompt").at("realm") == Json("Retry"),
              "rejected agent credentials ask the person", opened);
    id = opened.is_object() ? opened.at("prompt").at("id").as_string() : "";
    events->mark();
    run.check(sc_prompt_answer_login(session, id.c_str(), "carl", "wrong-person-guess", &error) == SC_OK,
              std::string("a wrong login is an answer: ") + error.message);
    opened = person_opened("auth");
    run.check(opened.is_object() && opened.at("prompt").at("id") != Json(id), "a rejected answer asks again", opened);
    id = opened.is_object() ? opened.at("prompt").at("id").as_string() : "";
    run.check(sc_prompt_answer_login(session, id.c_str(), "carl", "pw-retry-7730", &error) == SC_OK,
              std::string("the right login: ") + error.message);
    run.check(events->wait([&](sc_event_kind kind, const Json& event) {
                return on(SC_EVENT_TAB_TITLE)(kind, event) && event.at("title") == Json("Authed");
              }).is_object(),
              "the person's login reaches the server");
    credentials(Json(nullptr));

    // Downloads: on the agent's tab, the agent's policy; on the person's, a prompt.
    events->mark();
    evaluate("location.href='/download';1");
    opened = person_opened("download", 4s);
    run.check(!opened.is_object(), "a download on the agent's tab asks nobody", opened);
    events->mark();
    std::thread downloading([&] {
      sc_tab moved;
      sc_tab_init(&moved);
      sc_error failure;
      sc_tab_navigate(session, page, pages.Url("/download").c_str(), &moved, &failure);  // never commits
    });
    opened = person_opened("download");
    downloading.join();
    run.check(opened.is_object() && opened.at("prompt").at("suggestedName") == Json("report.txt"),
              "a download on the person's tab asks where", opened);
    id = opened.at("prompt").at("id").as_string();
    const auto saved = (chosen_dir / "saved.txt").string();
    run.check(sc_prompt_answer_download(session, id.c_str(), saved.c_str(), &error) == SC_OK,
              std::string("sc_prompt_answer_download: ") + error.message);
    const auto stored = events->wait([&](sc_event_kind kind, const Json& event) {
      return kind == SC_EVENT_PROMPT_CLOSED && event.at("prompt") == Json(id);
    }, 30s);
    std::string contents;
    std::getline(std::ifstream(saved), contents);
    run.check(stored.is_object() && stored.at("outcome") == Json("accepted") && stored.at("path") == Json(saved) &&
                  contents == "downloaded-bytes",
              "the download is saved where the person chose", stored);
    std::error_code removed;
    fs::remove_all(chosen_dir, removed);
    run.note("not exercised here", "client certificates (none in the profile), and a host leaving (see the fake engine tests)");

    sc_view_destroy(view);
    view = nullptr;
    run.stage("close");
    if (sc_session_close(session, 1, &error) != SC_OK) throw std::runtime_error(std::string("close: ") + error.message);
    sc_session_destroy(session);
    session = nullptr;
    const auto gone_by = Clock::now() + 15s;
    while (::kill(engine_pid, 0) == 0 && Clock::now() < gone_by) std::this_thread::sleep_for(100ms);
    run.check(::kill(engine_pid, 0) != 0, "the engine exits when the session closes");
    run.report["closed"] = true;
    run.stage("complete");
    run.report["ok"] = true;
  } catch (const std::exception& e) {
    run.report["error"] = e.what();
    std::cerr << "cef_host_events: " << e.what() << " (stage " << run.report["stage"].as_string() << ")\n";
  }
  if (events) run.report["events"] = events->all();
  if (view) sc_view_destroy(view);
  if (session) {
    sc_error ignored;
    sc_session_close(session, 1, &ignored);
    sc_session_destroy(session);
    const auto gone_by = Clock::now() + 15s;
    while (engine_pid > 0 && ::kill(engine_pid, 0) == 0 && Clock::now() < gone_by) std::this_thread::sleep_for(100ms);
  }
  std::error_code ignored;
  fs::remove_all(session_dir, ignored);
  fs::remove_all(profile_dir, ignored);
  run.write();
  Json summary = run.report;
  summary.as_object().erase("events");
  std::cout << summary.dump() << "\n";
  return run.report["ok"].as_bool() ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) { return host_events_main(argc, argv); }
