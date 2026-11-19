// SPDX-License-Identifier: MIT
// suitecut-shared-session: one SuiteCut engine that people and agents use
// together, for an app that embeds the browser in its own window.
//
// The worker owns the engine and serves its private Unix socket (see
// control_session.hpp for framing and startup). Every request names its
// `owner`, the app's name for whoever the tabs belong to (a chat, a project).
// Exactly one of these keys says what it is:
//
//   request  an agent's tool call on `page`, from the native agent catalogue
//   tabs     list, open, close, navigate, take or release an owner's tabs
//   view     attach a page's frames to a Mach port, resize it, send input
//   watch    wait until the owner's tabs change after `cursor`
//
// A tab is controlled by nobody, an agent or a person. A person's input or
// navigation takes it; while they hold it, agent calls on it are refused
// with code `human_in_control` until they release it.
#include "suitecut/agent_driver.hpp"
#include "suitecut/agent_tools.hpp"
#include "suitecut/simple_agent.hpp"
#include "control_session.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using suitecut::Json;
using Clock = std::chrono::steady_clock;

struct Refused : std::runtime_error {
  std::string code;
  Refused(std::string code, const std::string& message) : std::runtime_error(message), code(std::move(code)) {}
};

enum class Control { unclaimed, agent, human };
const char* control_name(Control control) {
  switch (control) {
    case Control::unclaimed: return "unclaimed";
    case Control::agent: return "agent";
    case Control::human: return "human";
  }
  return "unclaimed";
}

struct Tab {
  std::string owner;
  Control control = Control::unclaimed;
  std::uint64_t generation = 1;
  std::string url = "about:blank";
  bool adopted = true;  // false until the driver has a handle for a popup
};

// |maximum| 0 means any length.
bool safe_token(const std::string& value, std::size_t maximum) {
  if (value.empty() || (maximum && value.size() > maximum)) return false;
  for (const unsigned char c : value)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
          c == '_' || c == ':'))
      return false;
  return true;
}

std::string web_url(const Json& value) {
  if (!value.is_string()) throw Refused("invalid_url", "A URL must be a string");
  const auto& url = value.as_string();
  if (!(url.starts_with("http://") || url.starts_with("https://")))
    throw Refused("invalid_url", "Only http and https URLs open in the shared browser");
  return url;
}

int page_of(const Json& object) {
  if (!object.contains("page") || !object.at("page").is_integer())
    throw Refused("invalid_request", "A page ID is required");
  const auto page = object.at("page").as_int();
  if (page < 2 || page > INT_MAX) throw Refused("unknown_page", "No such tab");
  return static_cast<int>(page);
}

double number_in(const Json& object, const char* key, double low, double high) {
  if (!object.contains(key) || !object.at(key).is_number()) throw Refused("invalid_input", std::string("Missing ") + key);
  const auto value = object.at(key).as_double();
  if (!(value >= low && value <= high)) throw Refused("invalid_input", std::string("Out of range: ") + key);
  return value;
}

class Host {
 public:
  explicit Host(const suitecut::LaunchOptions& options)
      : browser_(std::make_unique<suitecut::Browser>(suitecut::Browser::launch(options))),
        driver_(std::make_unique<suitecut::AgentDriver>(*browser_)),
        simple_(std::make_unique<suitecut::SimpleAgent>(*driver_)),
        transport_(browser_->transport()) {
    // Page 1 is the engine's main page; it stays blank and belongs to nobody.
    // Every owner's tab is a page made for it.
    const auto catalog = suitecut::agent_tool_catalog("core,debug,media");
    for (const auto& tool : catalog.as_array()) {
      const auto& name = tool.at("name").as_string();
      // Tabs and engine events belong to this worker, not to one agent.
      if (name != "pages" && name != "events" && name != "profiles") tools_.insert(name);
      if (name != "pages" && name != "events" && name != "profiles") catalog_.as_array().push_back(tool);
    }
    pump_ = std::thread([this] { Pump(); });
  }
  ~Host() { Close(); }

  // False once the engine connection has died. The worker then withdraws and
  // exits, so the app's next start launches a fresh engine instead of every
  // owner getting the dead one's error.
  bool Alive() const { return !engine_lost_; }

  Json Handle(const Json& envelope) {
    try {
      if (engine_lost_)
        throw Refused("engine_exited", "The shared browser's engine stopped; start the session again");
      if (!envelope.is_object() || !envelope.contains("owner") || !envelope.at("owner").is_string() ||
          !safe_token(envelope.at("owner").as_string(), 0))
        throw Refused("invalid_owner", "Every request names its owner");
      const auto& owner = envelope.at("owner").as_string();
      Json result;
      if (envelope.contains("request")) result = Agent(owner, envelope);
      else if (envelope.contains("tabs")) result = Tabs(owner, envelope.at("tabs"));
      else if (envelope.contains("view")) result = View(owner, envelope.at("view"));
      else if (envelope.contains("watch")) result = Watch(owner, envelope.at("watch"));
      else if (envelope.contains("catalog")) result = catalog_;
      else throw Refused("invalid_request", "Unknown shared session request");
      return Json{{"ok", true}, {"result", std::move(result)}};
    } catch (const Refused& refused) {
      return Json{{"ok", false}, {"error", Json{{"code", refused.code}, {"message", refused.what()}}}};
    } catch (const std::exception& error) {
      return Json{{"ok", false}, {"error", Json{{"code", "failed"}, {"message", error.what()}}}};
    }
  }

  void Close() {
    if (closed_.exchange(true)) return;
    stopping_ = true;
    if (pump_.joinable()) pump_.join();
    std::lock_guard agent(agent_);
    simple_.reset();
    driver_.reset();
    if (browser_) browser_->close();
    browser_.reset();
  }

 private:
  // Agent tool calls run one at a time: the driver keeps one active page and
  // one set of refs. Viewer input and navigation never wait for them.
  Json Agent(const std::string& owner, const Json& envelope) {
    const auto& request = envelope.at("request");
    if (!request.is_object() || !request.contains("tool") || !request.at("tool").is_string())
      throw Refused("invalid_request", "A tool call names its tool");
    const auto tool = request.at("tool").as_string();
    if (!tools_.contains(tool)) throw Refused("unknown_tool", "Not a shared browser tool: " + tool);
    const int page = page_of(envelope);
    const Json arguments = request.contains("arguments") ? request.at("arguments") : Json(Json::object{});
    if (!arguments.is_object()) throw Refused("invalid_request", "Tool arguments are an object");
    std::lock_guard agent(agent_);
    AdoptPending();
    {
      std::lock_guard lock(state_);
      const auto& tab = Owned(owner, page);
      if (tab.control == Control::human)
        throw Refused("human_in_control",
                      "The person is using this tab. Wait until they hand it back, or ask them.");
    }
    driver_->call("pages", Json{{"action", "switch"}, {"pageId", page}});
    auto result = Run(tool, arguments);
    {
      std::lock_guard lock(state_);
      if (auto found = tabs_.find(page); found != tabs_.end() && found->second.control == Control::unclaimed) {
        found->second.control = Control::agent;
        ++found->second.generation;
        Changed();
      }
    }
    return result;
  }

  Json Run(const std::string& name, const Json& arguments) {
    auto prepared = arguments;
    if (name == "observe" || name == "act" || name == "wait") {
      if (name == "observe" && !prepared.contains("format")) prepared["format"] = "compact";
      if (name != "observe" && !prepared.contains("response")) prepared["response"] = "compact";
      return simple_->call(name, prepared);
    }
    // Refs from a compact observation (e12) name the same targets in the
    // driver's own tools.
    if (prepared.contains("ref") && prepared.at("ref").is_string() && prepared.at("ref").as_string().starts_with('e')) {
      prepared["target"] = simple_->target_ref(prepared.at("ref").as_string());
      prepared.as_object().erase("ref");
    }
    for (const auto* key : {"target", "source"})
      if (prepared.contains(key)) {
        const auto& target = prepared.at(key);
        if (target.is_object() && target.contains("kind") && target.at("kind") == Json("ref") &&
            target.contains("value") && target.at("value").is_string() &&
            target.at("value").as_string().starts_with('e'))
          prepared[key] = simple_->target_ref(target.at("value").as_string());
      }
    return driver_->call(name, prepared);
  }

  Json Tabs(const std::string& owner, const Json& tabs) {
    if (!tabs.is_object() || !tabs.contains("op") || !tabs.at("op").is_string())
      throw Refused("invalid_request", "A tabs request names its op");
    const auto op = tabs.at("op").as_string();
    if (op == "list") return List(owner);
    if (op == "open") {
      const auto by = tabs.contains("by") && tabs.at("by").is_string() ? tabs.at("by").as_string() : "agent";
      if (by != "agent" && by != "human") throw Refused("invalid_request", "A tab opens for an agent or a person");
      std::optional<std::string> url;
      if (tabs.contains("url")) url = web_url(tabs.at("url"));
      std::lock_guard agent(agent_);
      AdoptPending();
      const auto created = driver_->call("pages", Json{{"action", "new"}});
      const int page = static_cast<int>(created.at("pageId").as_int());
      {
        std::lock_guard lock(state_);
        auto& tab = tabs_[page];
        tab.owner = owner;
        tab.control = by == "agent" ? Control::agent : Control::unclaimed;
        Changed();
      }
      // Navigation runs without the driver: a slow page doesn't hold other
      // agents' calls. Its outcome is the tab's state, which watch reports.
      if (url) Navigate(page, Json{{"command", "navigate"}, {"url", *url}});
      return Describe(page);
    }
    const int page = page_of(tabs);
    if (op == "close") {
      std::lock_guard agent(agent_);
      AdoptPending();
      { std::lock_guard lock(state_); Owned(owner, page); }
      driver_->call("pages", Json{{"action", "close"}, {"pageId", page}});
      std::lock_guard lock(state_);
      tabs_.erase(page);
      Changed();
      return Json{{"closed", true}};
    }
    if (op == "navigate") {
      // A person's navigation from the app's toolbar.
      Json fields;
      if (tabs.contains("url")) fields = Json{{"command", "navigate"}, {"url", web_url(tabs.at("url"))}};
      else {
        const auto history = tabs.contains("history") && tabs.at("history").is_string() ? tabs.at("history").as_string() : "";
        // The engine names each history step by the Playwright call it is.
        if (history == "back") fields = Json{{"command", "historyBack"}, {"api", "page.goBack"}};
        else if (history == "forward") fields = Json{{"command", "historyForward"}, {"api", "page.goForward"}};
        else if (history == "reload") fields = Json{{"command", "reload"}, {"api", "page.reload"}};
        else throw Refused("invalid_request", "Navigate to a URL, or back, forward or reload");
      }
      Take(owner, page, Control::human);
      Navigate(page, std::move(fields));
      return Describe(page);
    }
    if (op == "take") {
      Take(owner, page, Control::human);
      return Describe(page);
    }
    if (op == "release") {
      std::lock_guard lock(state_);
      auto& tab = Owned(owner, page);
      if (tab.control == Control::human) {
        tab.control = Control::unclaimed;
        ++tab.generation;
        Changed();
      }
      return TabJson(page, tab, "");
    }
    throw Refused("invalid_request", "Unknown tabs op: " + op);
  }

  Json View(const std::string& owner, const Json& view) {
    if (!view.is_object() || !view.contains("op") || !view.at("op").is_string())
      throw Refused("invalid_request", "A view request names its op");
    const auto op = view.at("op").as_string();
    const int page = page_of(view);
    { std::lock_guard lock(state_); Owned(owner, page); }
    if (op == "attach") {
      // macOS bootstrap service names are at most 128 bytes (BOOTSTRAP_MAX_NAME_LEN).
      if (!view.contains("port") || !view.at("port").is_string() || !safe_token(view.at("port").as_string(), 128))
        throw Refused("invalid_request", "Attach names the receiver's Mach port");
      const auto pid = transport_->process_id();
      if (!pid) throw Refused("unsupported", "This engine can't share frames");
      Engine(page, Json{{"command", "surfaceOutput"}, {"port", view.at("port")}});
      return Json{{"enginePid", *pid}};
    }
    if (op == "detach") {
      Engine(page, Json{{"command", "surfaceOutput"}, {"port", Json{}}});
      return Json{{"detached", true}};
    }
    if (op == "screenshot") {
      // What the person sees, for the app's composer. A viewer's request:
      // it reads the tab without taking it, and works while a person or the
      // agent holds it.
      const auto shot = browser_->page(page).command(
          "artifact", Json{{"action", "screenshot"}, {"format", "png"}, {"scale", "css"}, {"caret", "hide"},
                           {"animations", "allow"}, {"fullPage", false}, {"omitBackground", false},
                           {"maskColor", "#FF00FF"}});
      if (!shot.contains("data") || !shot.at("data").is_string())
        throw Refused("failed", "The engine returned no screenshot");
      return Json{{"base64", shot.at("data")}, {"mimeType", "image/png"}};
    }
    if (op == "resize") {
      // No suitecut size cap; the engine and CEF enforce theirs.
      const int width = static_cast<int>(number_in(view, "width", 1, INT_MAX));
      const int height = static_cast<int>(number_in(view, "height", 1, INT_MAX));
      Engine(page, Json{{"command", "viewport"}, {"width", width}, {"height", height}});
      return Json{{"width", width}, {"height", height}};
    }
    if (op == "input") {
      if (!view.contains("event") || !view.at("event").is_object()) throw Refused("invalid_input", "Input carries an event");
      const auto& event = view.at("event");
      auto [fields, takes] = InputFields(event);
      if (takes) Take(owner, page, Control::human);
      Engine(page, std::move(fields));
      return Json{{"delivered", true}};
    }
    throw Refused("invalid_request", "Unknown view op: " + op);
  }

  // Builds the engine's input command from a viewer's event, field by field,
  // and says whether it is the kind of input that takes the tab: a press, a
  // key or typed text. Moving the pointer or scrolling doesn't.
  static std::pair<Json, bool> InputFields(const Json& event) {
    const auto kind = event.contains("kind") && event.at("kind").is_string() ? event.at("kind").as_string() : "";
    const auto action = event.contains("action") && event.at("action").is_string() ? event.at("action").as_string() : "";
    if (kind == "mouse") {
      Json fields{{"command", "input"}, {"kind", "mouse"}, {"action", action}};
      if (action == "move") {
        fields["x"] = number_in(event, "x", 0, INT_MAX);
        fields["y"] = number_in(event, "y", 0, INT_MAX);
        fields["steps"] = 1;
        return {fields, false};
      }
      if (action == "down" || action == "up") {
        const auto button = event.contains("button") && event.at("button").is_string() ? event.at("button").as_string() : "left";
        if (button != "left" && button != "right" && button != "middle") throw Refused("invalid_input", "Unknown button");
        fields["button"] = button;
        fields["clickCount"] = static_cast<int>(number_in(event, "clickCount", 1, INT_MAX));
        return {fields, action == "down"};
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

  Json Watch(const std::string& owner, const Json& watch) {
    const auto cursor = watch.is_object() && watch.contains("cursor") && watch.at("cursor").is_integer()
                            ? static_cast<std::uint64_t>(std::max<std::int64_t>(0, watch.at("cursor").as_int()))
                            : 0;
    const auto timeout = watch.is_object() && watch.contains("timeoutMs") && watch.at("timeoutMs").is_integer()
                             ? std::clamp<std::int64_t>(watch.at("timeoutMs").as_int(), 0, 25000)
                             : 20000;
    std::uint64_t version = 0;
    {
      std::unique_lock lock(state_);
      changed_.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return version_ > cursor || stopping_; });
      version = version_;
    }
    auto listed = List(owner);
    listed["cursor"] = static_cast<std::int64_t>(version);
    return listed;
  }

  Json List(const std::string& owner) {
    std::vector<std::pair<int, Tab>> owned;
    {
      std::lock_guard lock(state_);
      for (const auto& [id, tab] : tabs_)
        if (tab.owner == owner) owned.emplace_back(id, tab);
    }
    Json::array listed;
    for (const auto& [id, tab] : owned) listed.push_back(TabJson(id, tab, Title(id)));
    std::lock_guard lock(state_);
    return Json{{"tabs", std::move(listed)}, {"cursor", static_cast<std::int64_t>(version_)}};
  }

  Json Describe(int page) {
    Tab tab;
    {
      std::lock_guard lock(state_);
      const auto found = tabs_.find(page);
      if (found == tabs_.end()) throw Refused("unknown_page", "No such tab");
      tab = found->second;
    }
    return TabJson(page, tab, Title(page));
  }

  static Json TabJson(int page, const Tab& tab, const std::string& title) {
    return Json{{"page", page},
                {"url", tab.url},
                {"title", title},
                {"control", control_name(tab.control)},
                {"generation", static_cast<std::int64_t>(tab.generation)}};
  }

  // Titles are read when asked for rather than tracked: the engine reports
  // navigations but not title changes. A page that can't answer has none.
  std::string Title(int page) {
    try {
      const auto reply = Engine(page, Json{{"command", "devtools"}, {"method", "Runtime.evaluate"},
                                           {"params", Json{{"expression", "document.title"}, {"returnByValue", true}}}},
                                std::chrono::milliseconds(800));
      if (reply.contains("result") && reply.at("result").contains("value") && reply.at("result").at("value").is_string()) {
        auto title = reply.at("result").at("value").as_string();
        if (title.size() > 512) title.resize(512);
        return title;
      }
    } catch (...) {
    }
    return "";
  }

  // Returns once the new document commits: the load's outcome, its title
  // and URL, reach viewers through watch as the engine reports them.
  void Navigate(int page, Json fields) {
    fields["waitUntil"] = "commit";
    try {
      Engine(page, std::move(fields), std::chrono::milliseconds(30000));
    } catch (const std::exception& error) {
      // A page that fails to load still changed; its URL comes from the
      // engine's navigation events.
      throw Refused("navigation_failed", error.what());
    }
  }

  void Take(const std::string& owner, int page, Control control) {
    std::lock_guard lock(state_);
    auto& tab = Owned(owner, page);
    if (tab.control != control) {
      tab.control = control;
      ++tab.generation;
      Changed();
    }
  }

  Json Engine(int page, Json fields, std::chrono::milliseconds timeout = std::chrono::milliseconds(10000)) {
    fields["pageId"] = page;
    const auto packet = transport_->call_json(std::move(fields), timeout);
    const auto header = Json::parse(packet.header);
    if (header.contains("ok") && header.at("ok").is_bool() && !header.at("ok").as_bool()) {
      const auto message = header.contains("error") && header.at("error").is_string() ? header.at("error").as_string()
                                                                                       : std::string("Engine refused the command");
      throw std::runtime_error(message);
    }
    if (packet.payload.empty()) return Json::object{};
    return Json::parse(std::string(packet.payload.begin(), packet.payload.end()));
  }

  Tab& Owned(const std::string& owner, int page) {
    const auto found = tabs_.find(page);
    if (found == tabs_.end() || found->second.owner != owner) throw Refused("unknown_page", "No such tab");
    return found->second;
  }

  void Changed() {
    ++version_;
    changed_.notify_all();
  }

  void AdoptPending() {
    std::vector<int> pending;
    {
      std::lock_guard lock(state_);
      for (auto& [id, tab] : tabs_)
        if (!tab.adopted) {
          pending.push_back(id);
          tab.adopted = true;
        }
    }
    for (const int id : pending) {
      try {
        driver_->call("pages", Json{{"action", "adopt"}, {"pageId", id}});
      } catch (...) {
        std::lock_guard lock(state_);
        tabs_.erase(id);
        Changed();
      }
    }
  }

  // Engine events that change tabs: navigations, popups and closed pages.
  // Every other event is read and dropped here, so the bounded event queue
  // never fills.
  void Pump() {
    while (!stopping_) {
      std::optional<suitecut::Packet> packet;
      try {
        packet = transport_->poll(std::chrono::milliseconds(250));
      } catch (...) {
        std::lock_guard lock(state_);
        engine_lost_ = true;
        stopping_ = true;
        Changed();
        return;
      }
      if (!packet) continue;
      try {
        const auto header = Json::parse(packet->header);
        const auto type = header.at("type").as_string();
        if (type == "navigation" && header.contains("pageId")) {
          const int page = static_cast<int>(header.at("pageId").as_int());
          std::lock_guard lock(state_);
          if (auto found = tabs_.find(page); found != tabs_.end()) {
            found->second.url = header.at("url").as_string();
            Changed();
          }
        } else if (type == "context" && !packet->payload.empty()) {
          const auto event = Json::parse(std::string(packet->payload.begin(), packet->payload.end()));
          const auto name = event.at("event").as_string();
          if (!event.contains("pageId")) continue;
          const int page = static_cast<int>(event.at("pageId").as_int());
          std::lock_guard lock(state_);
          if (name == "page-created" && event.contains("openerId")) {
            const int opener = static_cast<int>(event.at("openerId").as_int());
            const auto found = tabs_.find(opener);
            if (found == tabs_.end() || tabs_.contains(page)) continue;
            // A popup belongs to its opener's owner, and to whoever was
            // driving the opener when it opened.
            Tab tab;
            tab.owner = found->second.owner;
            tab.control = found->second.control;
            tab.adopted = false;
            tabs_.emplace(page, std::move(tab));
            Changed();
          } else if (name == "page-closed" && tabs_.erase(page)) {
            Changed();
          }
        }
      } catch (...) {
        // A malformed or unexpected event changes no tab.
      }
    }
  }

  std::unique_ptr<suitecut::Browser> browser_;
  std::unique_ptr<suitecut::AgentDriver> driver_;
  std::unique_ptr<suitecut::SimpleAgent> simple_;
  std::shared_ptr<suitecut::Transport> transport_;
  std::set<std::string> tools_;
  Json catalog_ = Json::array{};
  std::mutex agent_;
  std::mutex state_;
  std::condition_variable changed_;
  std::map<int, Tab> tabs_;
  std::uint64_t version_ = 1;
  std::atomic<bool> stopping_{false}, closed_{false}, engine_lost_{false};
  std::thread pump_;
};

int usage() {
  std::cerr << "usage: suitecut-shared-session --session NAME --browser /absolute/suitecut-browser "
               "--profile /absolute/profile [--scale 1|2] [--session-dir DIR] [--idle-ms N] start|status|close\n";
  return 2;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    suitecut::LaunchOptions options;
    options.backend = suitecut::BrowserBackend::cef;
    options.scale = 2;
    suitecut::control::SessionOptions session;
    session.idle_ms = 30 * 60 * 1000;
    std::string command;
    for (int i = 1; i < argc; ++i) {
      const std::string argument = argv[i];
      auto value = [&]() -> std::string {
        if (i + 1 >= argc) throw std::invalid_argument("Missing value for " + argument);
        return argv[++i];
      };
      if (argument == "--session") session.name = value();
      else if (argument == "--session-dir") session.directory = value();
      else if (argument == "--browser") options.executable = value();
      else if (argument == "--profile") options.profile = value();
      else if (argument == "--scale") options.scale = static_cast<int>(Json::parse(value()).as_int());
      else if (argument == "--idle-ms") session.idle_ms = static_cast<int>(Json::parse(value()).as_int());
      else if (argument == "start" || argument == "status" || argument == "close") {
        if (!command.empty()) return usage();
        command = argument;
      } else return usage();
    }
    if (session.name.empty() || command.empty()) return usage();
    if (command == "start" && (options.executable.empty() || options.profile.empty())) return usage();
    auto worker = [&](int listener, int ready) {
      Host host(options);
      suitecut::control::serve_shared_session(
          session, listener, ready, 0, [&](const Json& envelope) { return host.Handle(envelope); },
          [&] { host.Close(); }, [&] { return host.Alive(); });
    };
    const auto tool = command == "close" ? "_session_close" : "_session_status";
    const auto reply = suitecut::control::session_request(session, Json{{"request", Json{{"tool", tool}}}},
                                                          command == "start", worker);
    std::cout << reply.dump() << '\n';
    return reply.at("ok").as_bool() ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
