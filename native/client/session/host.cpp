// SPDX-License-Identifier: MIT
// See host.hpp. Moved from examples/shared_session.cpp; protocol 0's
// behaviour is unchanged (tests/unit/shared_session_test.cpp pins it).
#include "host.hpp"

#include "suitecut/agent_tools.hpp"

#include <openssl/rand.h>

#include <algorithm>
#include <climits>

namespace suitecut::session {
namespace {
using Clock = std::chrono::steady_clock;
}  // namespace

const char* control_name(Control control) {
  switch (control) {
    case Control::unclaimed: return "unclaimed";
    case Control::agent: return "agent";
    case Control::human: return "human";
  }
  return "unclaimed";
}

bool safe_token(const std::string& value, std::size_t maximum) {
  if (value.empty() || (maximum && value.size() > maximum)) return false;
  for (const unsigned char c : value)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' ||
          c == '_' || c == ':'))
      return false;
  return true;
}

Host::Host(HostOptions options)
    : options_(std::move(options)),
      browser_(std::make_unique<Browser>(Browser::launch(options_.launch))),
      transport_(browser_->transport()) {
  // Page 1 is the engine's main page; it stays blank and belongs to nobody.
  // Every owner's tab is a page made for it.
  const auto catalog = agent_tool_catalog(options_.tools);
  for (const auto& tool : catalog.as_array()) {
    const auto& name = tool.at("name").as_string();
    // Tabs and engine events belong to this worker, not to one agent.
    if (name == "pages" || name == "events" || name == "profiles") continue;
    tools_.insert(name);
    catalog_.as_array().push_back(tool);
  }
  pump_ = std::thread([this] { Pump(); });
}

Host::~Host() { Close(); }

void Host::Close() {
  if (closed_.exchange(true)) return;
  stopping_ = true;
  changed_.notify_all();
  if (pump_.joinable()) pump_.join();
  {
    std::lock_guard owners(owners_mutex_);
    for (auto& [owner, agent] : owners_) {
      (void)owner;
      std::lock_guard lock(agent->mutex);
      agent->simple.reset();
      agent->driver.reset();
    }
    owners_.clear();
  }
  if (browser_) browser_->close();
  browser_.reset();
}

void Host::Live() const {
  if (engine_lost_)
    throw Refused("engine_exited", "The shared browser's engine stopped; start the session again");
}

std::optional<int> Host::EnginePid() const { return transport_->process_id(); }

std::string Host::EngineVersion() const {
  try {
    return browser_->version();
  } catch (...) {
    return "";
  }
}

std::set<std::string> Host::ToolsIn(const std::string& groups) const {
  std::set<std::string> out;
  const auto catalog = agent_tool_catalog(groups);
  for (const auto& tool : catalog.as_array()) {
    const auto& name = tool.at("name").as_string();
    if (tools_.contains(name)) out.insert(name);
  }
  return out;
}

Json Host::Catalog(const std::set<std::string>& allowed) const {
  Json::array out;
  for (const auto& tool : catalog_.as_array())
    if (allowed.contains(tool.at("name").as_string())) out.push_back(tool);
  return out;
}

Host::OwnerAgent& Host::AgentOf(const std::string& owner) {
  std::lock_guard lock(owners_mutex_);
  auto& slot = owners_[owner];
  if (!slot) {
    // Each owner gets its own driver over the shared Browser: its own active
    // page and refs, and its own lock, so one owner's slow call never holds
    // another's. Browser and Transport are safe to share between threads.
    slot = std::make_unique<OwnerAgent>();
    slot->driver = std::make_unique<AgentDriver>(*browser_);
    slot->simple = std::make_unique<SimpleAgent>(*slot->driver);
  }
  return *slot;
}

Host::CallResult Host::Agent(const std::string& owner, int page, const std::string& tool, const Json& arguments,
                             Claim claim, std::optional<std::uint64_t> expected) {
  auto& agent = AgentOf(owner);
  // One call at a time per owner: its driver keeps one active page and one
  // set of refs. Viewer input and navigation never wait for it.
  std::lock_guard calls(agent.mutex);
  if (!agent.driver) throw Refused("engine_exited", "The shared browser is closing");
  SyncPages(owner, agent);
  std::uint64_t held = 0;
  {
    std::lock_guard lock(state_);
    auto& tab = Owned(owner, page);
    if (tab.control == Control::human)
      throw Refused("human_in_control", "The person is using this tab. Wait until they hand it back, or ask them.");
    if (expected && *expected != tab.generation)
      throw Refused("stale_generation", "The tab's control changed since generation " + std::to_string(*expected));
    if (claim == Claim::before && tab.control == Control::unclaimed) {
      tab.control = Control::agent;
      ++tab.generation;
      Changed();
      EmitTab(page, tab);
    }
    held = tab.generation;
  }
  agent.driver->call("pages", Json{{"action", "switch"}, {"pageId", page}});
  CallResult out;
  out.result = Run(agent, tool, arguments);
  std::lock_guard lock(state_);
  const auto found = tabs_.find(page);
  if (claim == Claim::after) {
    if (found != tabs_.end() && found->second.control == Control::unclaimed) {
      found->second.control = Control::agent;
      ++found->second.generation;
      Changed();
      EmitTab(page, found->second);
    }
  } else {
    // A person who took the tab meanwhile did not cancel the call (the engine
    // cannot preempt one), but the agent should know it no longer holds it.
    out.control_lost = found == tabs_.end() || found->second.control != Control::agent || found->second.generation != held;
  }
  out.generation = found != tabs_.end() ? found->second.generation : held;
  return out;
}

Json Host::Run(OwnerAgent& agent, const std::string& name, const Json& arguments) {
  auto prepared = arguments;
  if (name == "observe" || name == "act" || name == "wait") {
    if (name == "observe" && !prepared.contains("format")) prepared["format"] = "compact";
    if (name != "observe" && !prepared.contains("response")) prepared["response"] = "compact";
    return agent.simple->call(name, prepared);
  }
  // Refs from a compact observation (e12) name the same targets in the
  // driver's own tools.
  if (prepared.contains("ref") && prepared.at("ref").is_string() && prepared.at("ref").as_string().starts_with('e')) {
    prepared["target"] = agent.simple->target_ref(prepared.at("ref").as_string());
    prepared.as_object().erase("ref");
  }
  for (const auto* key : {"target", "source"})
    if (prepared.contains(key)) {
      const auto& target = prepared.at(key);
      if (target.is_object() && target.contains("kind") && target.at("kind") == Json("ref") &&
          target.contains("value") && target.at("value").is_string() &&
          target.at("value").as_string().starts_with('e'))
        prepared[key] = agent.simple->target_ref(target.at("value").as_string());
    }
  return agent.driver->call(name, prepared);
}

std::string Host::CheckedUrl(const Json& value) const {
  if (!value.is_string()) throw Refused("invalid_url", "A URL must be a string");
  const auto& url = value.as_string();
  for (const auto& scheme : options_.schemes) {
    // http and https need their authority; other allowed schemes (about,
    // data) have none.
    const bool web = scheme == "http" || scheme == "https";
    if (url.starts_with(scheme + (web ? "://" : ":"))) return url;
  }
  if (options_.schemes == std::set<std::string>{"http", "https"})
    throw Refused("invalid_url", "Only http and https URLs open in the shared browser");
  std::string names;
  for (const auto& scheme : options_.schemes) names += (names.empty() ? "" : ", ") + scheme;
  throw Refused("invalid_url", "Only " + names + " URLs open in the shared browser");
}

Json Host::Open(const std::string& owner, Control control, const std::optional<std::string>& url) {
  auto& agent = AgentOf(owner);
  int page = 0;
  {
    std::lock_guard calls(agent.mutex);
    if (!agent.driver) throw Refused("engine_exited", "The shared browser is closing");
    SyncPages(owner, agent);
    const auto created = agent.driver->call("pages", Json{{"action", "new"}});
    page = static_cast<int>(created.at("pageId").as_int());
    std::lock_guard lock(state_);
    auto& tab = tabs_[page];
    tab.owner = owner;
    tab.control = control;
    Changed();
    EmitTab(page, tab);
  }
  // Navigation runs without the agent lock: a slow page doesn't hold the
  // owner's agent calls. Its outcome is the tab's state, which watch reports.
  if (url) NavigateEngine(page, Json{{"command", "navigate"}, {"url", *url}});
  return Describe(page);
}

void Host::CloseTab(const std::string& owner, int page) {
  auto& agent = AgentOf(owner);
  std::lock_guard calls(agent.mutex);
  if (!agent.driver) throw Refused("engine_exited", "The shared browser is closing");
  SyncPages(owner, agent);
  {
    std::lock_guard lock(state_);
    Owned(owner, page);
  }
  agent.driver->call("pages", Json{{"action", "close"}, {"pageId", page}});
  std::lock_guard lock(state_);
  if (tabs_.erase(page)) Emit(owner, Json{{"type", "tab.closed"}, {"page", page}});
  views_.erase(page);
  Changed();
}

Json Host::Navigate(const std::string& owner, int page, Json fields) {
  Take(owner, page);
  NavigateEngine(page, std::move(fields));
  return Describe(page);
}

Json Host::Take(const std::string& owner, int page) {
  {
    std::lock_guard lock(state_);
    TakeLocked(page, Owned(owner, page));
  }
  return Describe(page);
}

void Host::TakeLocked(int page, Tab& tab) {
  if (tab.control == Control::human) return;
  tab.control = Control::human;
  ++tab.generation;
  Changed();
  EmitTab(page, tab);
}

Json Host::Release(const std::string& owner, int page, Control from) {
  std::lock_guard lock(state_);
  auto& tab = Owned(owner, page);
  if (tab.control == from) {
    tab.control = Control::unclaimed;
    ++tab.generation;
    Changed();
    EmitTab(page, tab);
  }
  return TabJson(page, tab, "");
}

Tab Host::TabOf(const std::string& owner, int page) {
  std::lock_guard lock(state_);
  return Owned(owner, page);
}

int Host::Attach(const std::string& owner, int page, const std::string& port, bool replace, bool exclusive) {
  {
    std::lock_guard lock(state_);
    Owned(owner, page);
    if (exclusive && views_.contains(page) && !replace)
      throw Refused("view_busy", "The tab already has a view; pass replace to move it");
  }
  const auto pid = transport_->process_id();
  if (!pid) throw Refused("unsupported", "This engine can't share frames");
  Engine(page, Json{{"command", "surfaceOutput"}, {"port", port}});
  std::lock_guard lock(state_);
  views_.insert(page);
  return *pid;
}

void Host::Detach(const std::string& owner, int page) {
  {
    std::lock_guard lock(state_);
    Owned(owner, page);
  }
  Engine(page, Json{{"command", "surfaceOutput"}, {"port", Json{}}});
  std::lock_guard lock(state_);
  views_.erase(page);
}

Json Host::Screenshot(const std::string& owner, int page) {
  {
    std::lock_guard lock(state_);
    Owned(owner, page);
  }
  // What the person sees, for the app's composer. A viewer's request: it
  // reads the tab without taking it, and works while a person or the agent
  // holds it. It goes to the engine directly: a Page handle from
  // Browser::page owns the page and would close the tab when it goes.
  const auto shot = Engine(page, Json{{"command", "artifact"}, {"action", "screenshot"}, {"format", "png"},
                                      {"scale", "css"}, {"caret", "hide"}, {"animations", "allow"},
                                      {"fullPage", false}, {"omitBackground", false}, {"maskColor", "#FF00FF"}},
                           std::chrono::milliseconds(30000));
  if (!shot.contains("data") || !shot.at("data").is_string())
    throw Refused("failed", "The engine returned no screenshot");
  return Json{{"base64", shot.at("data")}, {"mimeType", "image/png"}};
}

void Host::Resize(const std::string& owner, int page, int width, int height) {
  {
    std::lock_guard lock(state_);
    Owned(owner, page);
  }
  // No suitecut size cap; the engine and CEF enforce theirs.
  Engine(page, Json{{"command", "viewport"}, {"width", width}, {"height", height}});
}

void Host::Input(const std::string& owner, int page, Json fields, bool takes) {
  {
    std::lock_guard lock(state_);
    auto& tab = Owned(owner, page);
    if (takes) TakeLocked(page, tab);
  }
  Engine(page, std::move(fields));
}

Json Host::Watch(const std::string& owner, std::uint64_t cursor, std::chrono::milliseconds timeout) {
  std::uint64_t version = 0;
  {
    std::unique_lock lock(state_);
    changed_.wait_for(lock, timeout, [&] { return version_ > cursor || stopping_; });
    version = version_;
  }
  auto listed = List(owner);
  listed["cursor"] = static_cast<std::int64_t>(version);
  return listed;
}

Json Host::NextEvents(std::uint64_t reader, const std::string& owner, std::uint64_t cursor,
                      std::chrono::milliseconds timeout) {
  std::vector<Json> picked;
  std::uint64_t latest = 0;
  {
    std::unique_lock lock(state_);
    auto& events = events_[owner];
    events.readers[reader] = cursor;
    // What every reader has seen can go.
    auto seen = cursor;
    for (const auto& [unused, at] : events.readers) {
      (void)unused;
      seen = std::min(seen, at);
    }
    while (!events.kept.empty() && events.kept.front().first <= seen) events.kept.pop_front();
    events.floor = std::max(events.floor, std::min(seen, events.sequence));
    if (cursor == 0 || cursor < events.floor || cursor > events.sequence) {
      // A first call, a reader that fell behind what was kept, or a cursor
      // from another worker: start again from the tabs as they are.
      events.readers[reader] = events.sequence;
      return Json{{"events", Json::array{Json{{"type", "resync"}}}},
                  {"cursor", static_cast<std::int64_t>(events.sequence)}};
    }
    changed_.wait_for(lock, timeout, [&] { return events.sequence > cursor || stopping_ || engine_lost_; });
    for (const auto& [sequence, event] : events.kept)
      if (sequence > cursor) picked.push_back(event);
    latest = events.sequence;
  }
  // A tab.changed carries the tab as it is now, with its title read from
  // the page; several changes to one tab in a reply read as one.
  Json::array out;
  std::set<int> described;
  for (auto it = picked.rbegin(); it != picked.rend(); ++it) {
    auto event = *it;
    if (event.at("type") == Json("tab.changed")) {
      const int page = static_cast<int>(event.at("tab").at("page").as_int());
      if (!described.insert(page).second) continue;
      try {
        event["tab"]["title"] = Title(page);
      } catch (...) {
      }
    }
    out.push_back(std::move(event));
  }
  std::reverse(out.begin(), out.end());
  return Json{{"events", std::move(out)}, {"cursor", static_cast<std::int64_t>(latest)}};
}

void Host::ReaderGone(std::uint64_t reader) {
  std::lock_guard lock(state_);
  for (auto& [owner, events] : events_) {
    (void)owner;
    if (!events.readers.erase(reader) || !events.readers.empty()) continue;
    // With nobody reading, only state is kept: the next reader resyncs.
    events.kept.clear();
    events.floor = events.sequence;
  }
}

std::string Host::AddGrant(Grant grant) {
  unsigned char bytes[32];
  if (RAND_bytes(bytes, sizeof bytes) != 1) throw Refused("failed", "No randomness for a grant");
  static const char* digits = "0123456789abcdef";
  std::string token;
  for (const auto byte : bytes) {
    token += digits[byte >> 4];
    token += digits[byte & 15];
  }
  std::lock_guard lock(state_);
  grants_[token] = std::move(grant);
  return token;
}

bool Host::Revoke(const std::string& owner, const std::string& token) {
  std::lock_guard lock(state_);
  const auto found = grants_.find(token);
  if (found == grants_.end() || found->second.owner != owner) return false;
  grants_.erase(found);
  return true;
}

std::optional<Host::Grant> Host::GrantFor(const std::string& token) {
  std::lock_guard lock(state_);
  const auto found = grants_.find(token);
  if (found == grants_.end()) return std::nullopt;
  if (found->second.expires && Clock::now() >= *found->second.expires) {
    grants_.erase(found);
    return std::nullopt;
  }
  return found->second;
}

Json Host::List(const std::string& owner) {
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

Json Host::Describe(int page) {
  Tab tab;
  {
    std::lock_guard lock(state_);
    const auto found = tabs_.find(page);
    if (found == tabs_.end()) throw Refused("unknown_page", "No such tab");
    tab = found->second;
  }
  return TabJson(page, tab, Title(page));
}

Json Host::TabJson(int page, const Tab& tab, const std::string& title) {
  return Json{{"page", page},
              {"url", tab.url},
              {"title", title},
              {"control", control_name(tab.control)},
              {"generation", static_cast<std::int64_t>(tab.generation)}};
}

// Titles are read when asked for rather than tracked: the engine reports
// navigations but not title changes. A page that can't answer has none.
std::string Host::Title(int page) {
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

// Returns once the new document commits: the load's outcome, its title and
// URL, reach viewers through watch as the engine reports them.
void Host::NavigateEngine(int page, Json fields) {
  fields["waitUntil"] = "commit";
  try {
    Engine(page, std::move(fields), std::chrono::milliseconds(30000));
  } catch (const std::exception& error) {
    // A page that fails to load still changed; its URL comes from the
    // engine's navigation events.
    throw Refused("navigation_failed", error.what());
  }
}

Json Host::Engine(int page, Json fields, std::chrono::milliseconds timeout) {
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

Tab& Host::Owned(const std::string& owner, int page) {
  const auto found = tabs_.find(page);
  if (found == tabs_.end() || found->second.owner != owner) throw Refused("unknown_page", "No such tab");
  return found->second;
}

void Host::Changed() {
  ++version_;
  changed_.notify_all();
}

void Host::Emit(const std::string& owner, Json event) {
  auto& events = events_[owner];
  ++events.sequence;
  if (events.readers.empty())
    events.floor = events.sequence;
  else
    events.kept.emplace_back(events.sequence, std::move(event));
  changed_.notify_all();
}

void Host::EmitTab(int page, const Tab& tab) {
  Emit(tab.owner, Json{{"type", "tab.changed"}, {"tab", TabJson(page, tab, "")}});
}

void Host::SyncPages(const std::string& owner, OwnerAgent& agent) {
  std::vector<int> pending, gone;
  {
    std::lock_guard lock(state_);
    if (const auto found = gone_.find(owner); found != gone_.end()) {
      gone = std::move(found->second);
      gone_.erase(found);
    }
    for (auto& [id, tab] : tabs_)
      if (!tab.adopted && tab.owner == owner) {
        pending.push_back(id);
        tab.adopted = true;
      }
  }
  for (const int id : gone) agent.driver->forget_page(id);
  for (const int id : pending) {
    try {
      agent.driver->call("pages", Json{{"action", "adopt"}, {"pageId", id}});
    } catch (...) {
      std::lock_guard lock(state_);
      if (tabs_.erase(id)) {
        views_.erase(id);
        Changed();
        Emit(owner, Json{{"type", "tab.closed"}, {"page", id}});
      }
    }
  }
}

// Engine events that change tabs: navigations, popups and closed pages.
// Every other event is read and dropped here, so the bounded event queue
// never fills.
void Host::Pump() {
  while (!stopping_) {
    std::optional<Packet> packet;
    try {
      packet = transport_->poll(std::chrono::milliseconds(250));
    } catch (...) {
      std::lock_guard lock(state_);
      engine_lost_ = true;
      stopping_ = true;
      for (auto& [owner, events] : events_) {
        (void)events;
        Emit(owner, Json{{"type", "engine.exited"}});
      }
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
          EmitTab(page, found->second);
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
          const auto& placed = tabs_.emplace(page, std::move(tab)).first->second;
          Changed();
          EmitTab(page, placed);
        } else if (name == "page-closed") {
          if (const auto found = tabs_.find(page); found != tabs_.end()) {
            const auto owner = found->second.owner;
            if (found->second.adopted) gone_[owner].push_back(page);
            tabs_.erase(found);
            views_.erase(page);
            Changed();
            Emit(owner, Json{{"type", "tab.closed"}, {"page", page}});
          }
        }
      }
    } catch (...) {
      // A malformed or unexpected event changes no tab.
    }
  }
}

}  // namespace suitecut::session
