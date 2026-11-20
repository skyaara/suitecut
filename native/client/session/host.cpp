// SPDX-License-Identifier: MIT
// See host.hpp. Moved from examples/shared_session.cpp; protocol 0's
// behaviour is unchanged (tests/unit/shared_session_test.cpp pins it).
#include "host.hpp"

#include "suitecut/agent_tools.hpp"

#include <openssl/rand.h>

#include <unistd.h>

#include <algorithm>
#include <climits>
#include <filesystem>
#include <system_error>

namespace suitecut::session {
namespace {
using Clock = std::chrono::steady_clock;

// Why |path| cannot answer a chooser in |mode|, or "". A path to read is
// absolute, exists and is a readable regular file, or a directory for a
// folder; a path to save to is absolute, in a writable directory, and not a
// directory itself.
std::string path_problem(const std::string& mode, const std::string& path) {
  namespace fs = std::filesystem;
  if (path.empty() || path.find('\0') != std::string::npos || !fs::path(path).is_absolute())
    return "Not an absolute path: " + path;
  std::error_code error;
  const auto status = fs::status(path, error);
  if (mode == "save") {
    if (fs::is_directory(status)) return "A directory, not a file: " + path;
    const auto parent = fs::path(path).parent_path();
    if (!fs::is_directory(parent, error) || ::access(parent.c_str(), W_OK) != 0)
      return "Not in a writable directory: " + path;
    return "";
  }
  if (error || !fs::exists(status)) return "No such file: " + path;
  if (mode == "openFolder" ? !fs::is_directory(status) : !fs::is_regular_file(status))
    return (mode == "openFolder" ? "Not a directory: " : "Not a regular file: ") + path;
  if (::access(path.c_str(), R_OK) != 0) return "Not readable: " + path;
  return "";
}
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
  // An engine that sends host events says so when it is ready; an older one
  // ignores the switch and this session offers none of them.
  try {
    const auto ready = Json::parse(transport_->ready().header);
    host_events_ = ready.contains("capabilities") && ready.at("capabilities").is_object() &&
                   ready.at("capabilities").contains("hostEvents") &&
                   ready.at("capabilities").at("hostEvents") == Json(true);
  } catch (...) {
  }
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
  // With the engine gone, a download still being saved fails at once.
  std::vector<std::thread> savers;
  {
    std::lock_guard lock(savers_mutex_);
    savers = std::move(savers_);
  }
  for (auto& saver : savers)
    if (saver.joinable()) saver.join();
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
  bool dialogs = false, person = false;
  {
    std::lock_guard lock(state_);
    const auto found = prompt_hosts_.find(owner);
    dialogs = found != prompt_hosts_.end() && !found->second.empty();
    person = PersonHost(owner);
  }
  if (dialogs || person) Hold({page}, dialogs ? 1 : -1, person ? 1 : -1);
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
  // A page showing a dialog is blocked in it, so a navigation would wait for
  // the dialog's deadline. The person navigating has moved on: dismiss its
  // alerts, confirms and prompts, as closed. A beforeunload stays theirs to
  // answer, since it is about this very navigation.
  std::vector<std::string> dismiss;
  {
    std::lock_guard lock(state_);
    for (auto& [id, prompt] : prompts_)
      if (prompt.page == page && prompt.kind == "dialog" && prompt.outcome.empty() &&
          prompt.info.at("dialog") != Json("beforeunload")) {
        prompt.outcome = "closed";
        dismiss.push_back(prompt.engine_id);
      }
  }
  for (const auto& engine_id : dismiss) {
    try {
      Engine(page, Json{{"command", "dialog"}, {"action", "answer"}, {"dialogId", engine_id}, {"accept", false},
                        {"promptText", ""}});
    } catch (...) {
      // It closed meanwhile; its prompt.closed comes from the engine.
    }
  }
  NavigateEngine(page, std::move(fields));
  return Describe(page);
}

Json Host::Stop(const std::string& owner, int page) {
  if (!host_events_) throw Refused("unsupported", "This engine cannot stop a load for a host");
  Take(owner, page);
  Engine(page, Json{{"command", "host"}, {"action", "stop"}});
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
  // the page; several changes to one tab in a reply read as one, where the
  // first was, so a tab still comes before its other events.
  Json::array out;
  std::map<int, std::size_t> described;  // page -> its tab.changed in out
  for (auto& event : picked) {
    if (event.at("type") == Json("tab.changed")) {
      const int page = static_cast<int>(event.at("tab").at("page").as_int());
      if (const auto found = described.find(page); found != described.end()) {
        out[found->second] = std::move(event);
        continue;
      }
      described.emplace(page, out.size());
    }
    out.push_back(std::move(event));
  }
  for (const auto& [page, at] : described) {
    try {
      out[at]["tab"]["title"] = Title(page);
    } catch (...) {
    }
  }
  return Json{{"events", std::move(out)}, {"cursor", static_cast<std::int64_t>(latest)}};
}

void Host::ReaderGone(std::uint64_t reader) {
  std::vector<int> dialogs, person;
  {
    std::lock_guard lock(state_);
    for (auto& [owner, events] : events_) {
      (void)owner;
      if (!events.readers.erase(reader) || !events.readers.empty()) continue;
      // With nobody reading, only state is kept: the next reader resyncs.
      events.kept.clear();
      events.floor = events.sequence;
    }
    for (auto& [owner, connections] : prompt_hosts_) {
      if (!connections.erase(reader) || !connections.empty()) continue;
      // The last window that shows this owner's prompts went: its pages go
      // back to default answers, and what they hold is dismissed.
      for (const auto& [id, tab] : tabs_)
        if (tab.owner == owner) {
          dialogs.push_back(id);
          DropPrompts(id, "dismissed", "dialog");
        }
    }
    for (auto& [owner, connections] : person_hosts_) {
      if (!connections.erase(reader) || !connections.empty()) continue;
      // And today's policy answers a person's prompts again.
      for (const auto& [id, tab] : tabs_)
        if (tab.owner == owner) {
          person.push_back(id);
          DropPrompts(id, "dismissed", "person");
        }
    }
  }
  if (!dialogs.empty()) Hold(dialogs, 0, -1);
  if (!person.empty()) Hold(person, -1, 0);
}

void Host::PromptHost(std::uint64_t connection, const std::string& owner, bool person) {
  std::vector<int> pages;
  bool dialogs = false, people = false;
  {
    std::lock_guard lock(state_);
    auto& connections = prompt_hosts_[owner];
    dialogs = connections.insert(connection).second && connections.size() == 1;
    if (person && host_events_) {
      auto& hosts = person_hosts_[owner];
      people = hosts.insert(connection).second && hosts.size() == 1;
    }
    if (!dialogs && !people) return;
    for (const auto& [id, tab] : tabs_)
      if (tab.owner == owner) pages.push_back(id);
  }
  Hold(pages, dialogs ? 1 : -1, people ? 1 : -1);
}

bool Host::PersonHost(const std::string& owner) const {
  const auto found = person_hosts_.find(owner);
  return found != person_hosts_.end() && !found->second.empty();
}

void Host::Hold(const std::vector<int>& pages, int dialogs, int person) {
  for (const int page : pages) {
    try {
      if (dialogs >= 0)
        Engine(page, Json{{"command", "dialog"}, {"action", "subscribe"}, {"enabled", dialogs > 0}},
               std::chrono::milliseconds(5000));
      if (person >= 0 && host_events_)
        Engine(page, Json{{"command", "host"}, {"action", "prompts"}, {"enabled", person > 0}},
               std::chrono::milliseconds(5000));
    } catch (...) {
      // A page that closed meanwhile holds nothing.
    }
  }
}

void Host::DropPrompts(int page, const std::string& outcome, const std::string& kinds) {
  for (auto it = prompts_.begin(); it != prompts_.end();) {
    const bool dialog = it->second.kind == "dialog";
    if (it->second.page != page || (kinds == "dialog" && !dialog) || (kinds == "person" && dialog)) {
      ++it;
      continue;
    }
    Emit(it->second.owner, Json{{"type", "prompt.closed"},
                                {"prompt", it->first},
                                {"page", page},
                                {"outcome", it->second.outcome.empty() ? outcome : it->second.outcome}});
    it = prompts_.erase(it);
  }
}

Json Host::AnswerPrompt(const std::string& owner, bool host, const std::string& prompt, const Json& answer) {
  if (!answer.is_object()) throw Refused("invalid_request", "An answer is an object");
  if (!prompt.starts_with("dialog:")) {
    Prompt held;
    {
      std::lock_guard lock(state_);
      const auto found = prompts_.find(prompt);
      if (found == prompts_.end() || found->second.owner != owner) {
        for (const auto* kind : {"file:", "permission:", "auth:", "certificate:", "download:"})
          if (found == prompts_.end() && prompt.starts_with(kind))
            throw Refused("prompt_gone", "The prompt was answered or has closed");
        throw Refused("unknown_prompt", "No such prompt");
      }
      if (!host) throw Refused("forbidden", "A person's prompt is answered by a host");
      if (!found->second.outcome.empty()) throw Refused("prompt_gone", "The prompt was already answered");
      held = found->second;
    }
    return AnswerPerson(prompt, std::move(held), answer);
  }
  if (!answer.contains("accept") || !answer.at("accept").is_bool())
    throw Refused("invalid_request", "A dialog's answer has accept: true or false");
  if (answer.contains("text") && !answer.at("text").is_string()) throw Refused("invalid_request", "text is a string");
  const bool accept = answer.at("accept").as_bool();
  std::string text;
  int page = 0;
  std::string engine_id;
  {
    std::lock_guard lock(state_);
    const auto found = prompts_.find(prompt);
    if (found == prompts_.end()) {
      if (prompt.starts_with("dialog:")) throw Refused("prompt_gone", "The prompt was answered or has closed");
      throw Refused("unknown_prompt", "No such prompt");
    }
    auto& held = found->second;
    if (held.owner != owner) throw Refused("unknown_prompt", "No such prompt");
    if (!held.outcome.empty()) throw Refused("prompt_gone", "The prompt was already answered");
    if (!host) {
      const auto tab = tabs_.find(held.page);
      if (tab == tabs_.end() || tab->second.control != Control::agent)
        throw Refused("forbidden", "An agent answers a dialog only on a tab it holds");
    }
    held.outcome = accept ? "accepted" : "dismissed";
    // A prompt dialog answered without text keeps its default value.
    text = answer.contains("text") ? answer.at("text").as_string() : held.info.at("defaultValue").as_string();
    page = held.page;
    engine_id = held.engine_id;
  }
  try {
    Engine(page, Json{{"command", "dialog"}, {"action", "answer"}, {"dialogId", engine_id}, {"accept", accept},
                      {"promptText", text}});
  } catch (const std::exception& error) {
    std::lock_guard lock(state_);
    if (const auto found = prompts_.find(prompt); found != prompts_.end()) {
      found->second.outcome.clear();
      DropPrompts(page, "closed");
    }
    throw Refused("prompt_gone", error.what());
  }
  return Json{{"answered", true}};
}

// A person's answer, checked here and given to the engine. Nothing in an
// answer is logged, kept or echoed: a password goes to the engine only.
Json Host::AnswerPerson(const std::string& prompt, Prompt held, const Json& answer) {
  const bool cancel = answer.contains("cancel") && answer.at("cancel") == Json(true);
  Json given = Json::object{};
  std::string outcome = cancel ? "dismissed" : "accepted", path;
  const auto string = [&](const char* key) -> const std::string& {
    if (!answer.contains(key) || !answer.at(key).is_string())
      throw Refused("invalid_request", std::string("The answer has ") + key + ", or cancel: true");
    return answer.at(key).as_string();
  };
  if (cancel) {
    given["cancel"] = true;
  } else if (held.kind == "file") {
    if (!answer.contains("paths") || !answer.at("paths").is_array() || answer.at("paths").as_array().empty())
      throw Refused("invalid_request", "A file chooser's answer has paths, or cancel: true");
    const auto mode = held.info.at("mode").as_string();
    if (mode != "openMultiple" && answer.at("paths").as_array().size() > 1)
      throw Refused("invalid_request", "This file chooser takes one path");
    for (const auto& item : answer.at("paths").as_array()) {
      if (!item.is_string()) throw Refused("invalid_request", "Each path is a string");
      if (const auto problem = path_problem(mode, item.as_string()); !problem.empty())
        throw Refused("invalid_path", problem);
    }
    given["paths"] = answer.at("paths");
  } else if (held.kind == "permission") {
    if (!answer.contains("accept") || !answer.at("accept").is_bool())
      throw Refused("invalid_request", "A permission's answer has accept: true or false");
    given["accept"] = answer.at("accept");
    outcome = answer.at("accept").as_bool() ? "accepted" : "dismissed";
  } else if (held.kind == "auth") {
    given["username"] = string("username");
    given["password"] = string("password");
  } else if (held.kind == "certificate") {
    const auto count = held.info.at("certificates").as_array().size();
    if (!answer.contains("index") || !answer.at("index").is_integer() || answer.at("index").as_int() < 0 ||
        static_cast<std::size_t>(answer.at("index").as_int()) >= count)
      throw Refused("invalid_request", "A certificate's answer has the index of one listed, or cancel: true");
    given["index"] = answer.at("index");
  } else if (held.kind == "download") {
    path = string("path");
    if (const auto problem = path_problem("save", path); !problem.empty()) throw Refused("invalid_path", problem);
  }
  {
    std::lock_guard lock(state_);
    const auto found = prompts_.find(prompt);
    if (found == prompts_.end()) throw Refused("prompt_gone", "The prompt was answered or has closed");
    if (!found->second.outcome.empty()) throw Refused("prompt_gone", "The prompt was already answered");
    found->second.outcome = outcome;
  }
  if (held.kind == "download") {
    if (cancel) {
      try {
        Engine(held.page, Json{{"command", "download"}, {"action", "cancel"}, {"downloadId", held.engine_id}});
      } catch (...) {
        // It finished or failed first; the person's no still stands.
      }
      std::lock_guard lock(state_);
      if (const auto found = prompts_.find(prompt); found != prompts_.end()) {
        Emit(held.owner, Json{{"type", "prompt.closed"}, {"prompt", prompt}, {"page", held.page}, {"outcome", outcome}});
        prompts_.erase(found);
      }
      return Json{{"answered", true}};
    }
    // Saved once the download completes: prompt.closed says where, or why not.
    std::lock_guard lock(savers_mutex_);
    savers_.emplace_back([this, prompt, page = held.page, download = held.engine_id, path] {
      SaveDownload(prompt, page, download, path);
    });
    return Json{{"answered", true}};
  }
  try {
    Engine(held.page, Json{{"command", "host"}, {"action", "answer"}, {"prompt", held.engine_id}, {"answer", given}});
  } catch (const std::exception& error) {
    std::lock_guard lock(state_);
    if (const auto found = prompts_.find(prompt); found != prompts_.end()) {
      Emit(held.owner, Json{{"type", "prompt.closed"}, {"prompt", prompt}, {"page", held.page}, {"outcome", "closed"}});
      prompts_.erase(found);
    }
    throw Refused("prompt_gone", error.what());
  }
  // The engine's report that it closed carries the outcome to prompt.closed.
  return Json{{"answered", true}};
}

void Host::SaveDownload(const std::string& prompt, int page, std::string download, std::string path) {
  Json closed{{"type", "prompt.closed"}, {"prompt", prompt}, {"page", page}};
  try {
    // Wait for the download to settle, however long it takes: the engine's
    // wait is a read that ends within its longest command (120 s), so it is
    // asked again until the download finishes or the prompt goes. Then the
    // engine copies the file, once.
    for (;;) {
      try {
        const auto settled = Engine(page,
                                    Json{{"command", "download"}, {"action", "failure"}, {"downloadId", download},
                                         {"timeoutMs", 110000}},
                                    std::chrono::milliseconds(120000));
        if (settled.contains("failure") && settled.at("failure").is_string())
          throw std::runtime_error(settled.at("failure").as_string());
        break;
      } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find("Native operation timed out") == std::string::npos) throw;
        std::lock_guard lock(state_);
        if (!prompts_.contains(prompt)) return;  // its tab closed and said so
      }
    }
    Engine(page,
           Json{{"command", "download"}, {"action", "saveAs"}, {"downloadId", download}, {"path", path},
                {"timeoutMs", 120000}},
           std::chrono::milliseconds(125000));
    closed["outcome"] = "accepted";
    closed["path"] = path;
  } catch (const std::exception& error) {
    closed["outcome"] = "failed";
    closed["error"] = error.what();
  }
  std::lock_guard lock(state_);
  const auto found = prompts_.find(prompt);
  if (found == prompts_.end()) return;  // its tab closed and said so
  Emit(found->second.owner, std::move(closed));
  prompts_.erase(found);
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

Json Host::TabJson(int page, const Tab& tab, const std::string& title) const {
  Json out{{"page", page},
           {"url", tab.url},
           {"title", title},
           {"control", control_name(tab.control)},
           {"generation", static_cast<std::int64_t>(tab.generation)}};
  // Protocol 1 only; protocol 0 replies leave these out.
  if (tab.opener) out["opener"] = tab.opener;
  if (host_events_) {
    out["loading"] = tab.loading;
    out["canGoBack"] = tab.can_go_back;
    out["canGoForward"] = tab.can_go_forward;
  }
  return out;
}

// Listings read document.title when asked, as protocol 0 always has; a page
// that can't answer has none. Host events' tab.title carries the engine's own
// title instead, which for a page without one may be its URL.
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
  // The engine's navigateFrame returns at commit; its older navigate command
  // waited for the load's end whatever it was asked. Without host events the
  // older command stays, so protocol 0 clients see today's timing and history.
  if (host_events_ && fields.at("command") == Json("navigate")) {
    fields["command"] = "navigateFrame";
    fields["api"] = "page.goto";
  }
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
    std::vector<int> hold, hold_person;  // popups whose prompts a host should see
    std::optional<std::pair<int, std::string>> policy;  // a held request today's policy answers
    try {
      const auto header = Json::parse(packet->header);
      const auto type = header.at("type").as_string();
      if ((type == "host" || type == "dialog") && header.contains("pageId") && !packet->payload.empty()) {
        const int page = static_cast<int>(header.at("pageId").as_int());
        const auto body = Json::parse(std::string(packet->payload.begin(), packet->payload.end()));
        std::lock_guard lock(state_);
        if (type == "host") {
          if (auto held = HostPacket(page, body)) policy.emplace(page, std::move(*held));
        } else {
          DialogPacket(page, body);
        }
      } else if (type == "download" && !packet->payload.empty()) {
        const auto body = Json::parse(std::string(packet->payload.begin(), packet->payload.end()));
        if (!body.contains("pageId") || !body.at("pageId").is_integer()) continue;
        std::lock_guard lock(state_);
        DownloadPacket(static_cast<int>(body.at("pageId").as_int()), body);
      } else if (type == "navigation" && header.contains("pageId")) {
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
          tab.opener = opener;
          const auto& placed = tabs_.emplace(page, std::move(tab)).first->second;
          Changed();
          EmitTab(page, placed);
          const auto hosts = prompt_hosts_.find(placed.owner);
          if (hosts != prompt_hosts_.end() && !hosts->second.empty()) hold.push_back(page);
          if (PersonHost(placed.owner)) hold_person.push_back(page);
        } else if (name == "page-closed") {
          if (const auto found = tabs_.find(page); found != tabs_.end()) {
            const auto owner = found->second.owner;
            if (found->second.adopted) gone_[owner].push_back(page);
            tabs_.erase(found);
            views_.erase(page);
            DropPrompts(page, "closed");
            Changed();
            Emit(owner, Json{{"type", "tab.closed"}, {"page", page}});
          }
        }
      }
    } catch (...) {
      // A malformed or unexpected event changes no tab.
    }
    if (!hold.empty()) Hold(hold, 1, -1);
    if (!hold_person.empty()) Hold(hold_person, -1, 1);
    if (policy) {
      try {
        Engine(policy->first, Json{{"command", "host"}, {"action", "answer"}, {"prompt", policy->second},
                                   {"answer", Json{{"policy", true}}}});
      } catch (...) {
        // It went first.
      }
    }
  }
}

// With state_ held. Loading, progress and titles update the tab; cursors and
// IME ranges go to the owner's windows as they come; a person's prompts
// become prompts.
std::optional<std::string> Host::HostPacket(int page, const Json& event) {
  if (!event.contains("event") || !event.at("event").is_string()) return std::nullopt;
  const auto found = tabs_.find(page);
  if (event.at("event") == Json("prompt") && event.contains("id") && event.at("id").is_string() &&
      event.contains("kind") && event.at("kind").is_string()) {
    const auto& engine_id = event.at("id").as_string();
    const auto& kind = event.at("kind").as_string();
    const auto id = kind + ":" + std::to_string(page) + ":" + engine_id;
    const auto state = event.contains("state") && event.at("state").is_string() ? event.at("state").as_string() : "";
    if (state == "opened") {
      // Not a tab of an owner with a host, or an agent's tab where its own
      // credentials answer: today's policy, with no prompt.
      if (found == tabs_.end() || !PersonHost(found->second.owner)) return engine_id;
      if (kind == "auth" && found->second.control == Control::agent && event.contains("agentCredentials") &&
          event.at("agentCredentials") == Json(true))
        return engine_id;
      Prompt prompt;
      prompt.owner = found->second.owner;
      prompt.page = page;
      prompt.kind = kind;
      prompt.engine_id = engine_id;
      prompt.info = Json{{"id", id}, {"page", page}, {"kind", kind}};
      for (const auto& [key, value] : event.as_object())
        if (key != "id" && key != "kind" && key != "state" && key != "event" && key != "agentCredentials")
          prompt.info[key] = value;
      Emit(prompt.owner, Json{{"type", "prompt.opened"}, {"prompt", prompt.info}});
      prompts_[id] = std::move(prompt);
    } else if (state == "closed") {
      const auto held = prompts_.find(id);
      if (held == prompts_.end()) return std::nullopt;
      const auto reason = event.contains("reason") && event.at("reason").is_string() ? event.at("reason").as_string() : "";
      const auto outcome = !held->second.outcome.empty() ? held->second.outcome
                           : reason == "policy"          ? std::string("dismissed")
                                                         : std::string("closed");
      Emit(held->second.owner, Json{{"type", "prompt.closed"}, {"prompt", id}, {"page", page}, {"outcome", outcome}});
      prompts_.erase(held);
    }
    return std::nullopt;
  }
  if (found == tabs_.end()) return std::nullopt;
  auto& tab = found->second;
  const auto& name = event.at("event").as_string();
  auto loading = [&] {
    return Json{{"type", "tab.loading"}, {"page", page}, {"loading", tab.loading}, {"progress", tab.progress},
                {"canGoBack", tab.can_go_back}, {"canGoForward", tab.can_go_forward}};
  };
  if (name == "loading") {
    tab.loading = event.at("loading").as_bool();
    tab.can_go_back = event.at("canGoBack").as_bool();
    tab.can_go_forward = event.at("canGoForward").as_bool();
    if (tab.loading) tab.progress = 0;
    Emit(tab.owner, loading());
  } else if (name == "progress") {
    tab.progress = event.at("progress").as_double();
    Emit(tab.owner, loading());
  } else if (name == "title") {
    Emit(tab.owner, Json{{"type", "tab.title"}, {"page", page}, {"title", event.at("title")}});
  } else if (name == "cursor") {
    Json out{{"type", "view.cursor"}, {"view", page}, {"cursor", event.at("cursor")}};
    for (const auto* key : {"hotspotX", "hotspotY", "width", "height"})
      if (event.contains(key)) out[key] = event.at(key);
    Emit(tab.owner, std::move(out));
  } else if (name == "ime") {
    Emit(tab.owner, Json{{"type", "view.ime"}, {"view", page}, {"range", event.at("range")}, {"bounds", event.at("bounds")}});
  }
  return std::nullopt;
}

// With state_ held. A download that starts on a tab the agent does not hold,
// for an owner with a host, asks where to save it. Today's policy has already
// started it into the profile's download directory; an answer copies it from
// there once it completes, or cancels it.
void Host::DownloadPacket(int page, const Json& download) {
  if (!host_events_ || download.at("event") != Json("created") || !download.contains("id") ||
      !download.at("id").is_string())
    return;
  if (download.contains("failure") && download.at("failure").is_string()) return;  // it never started
  const auto found = tabs_.find(page);
  if (found == tabs_.end() || found->second.control == Control::agent || !PersonHost(found->second.owner)) return;
  const auto id = "download:" + std::to_string(page) + ":" + std::to_string(++download_prompts_);
  Prompt prompt;
  prompt.owner = found->second.owner;
  prompt.page = page;
  prompt.kind = "download";
  prompt.engine_id = download.at("id").as_string();
  auto text = [&](const char* key) {
    return download.contains(key) && download.at(key).is_string() ? download.at(key).as_string() : std::string();
  };
  prompt.info = Json{{"id", id},     {"page", page}, {"kind", "download"}, {"url", text("url")},
                     {"suggestedName", text("suggestedFilename")}, {"mimeType", text("mimeType")}};
  if (download.contains("totalBytes") && download.at("totalBytes").is_number())
    prompt.info["totalBytes"] = download.at("totalBytes");
  Emit(prompt.owner, Json{{"type", "prompt.opened"}, {"prompt", prompt.info}});
  prompts_[id] = std::move(prompt);
}

// With state_ held. The engine's held dialogs become prompts.
void Host::DialogPacket(int page, const Json& dialog) {
  const auto found = tabs_.find(page);
  if (found == tabs_.end() || !dialog.contains("id") || !dialog.at("id").is_string()) return;
  const auto id = "dialog:" + std::to_string(page) + ":" + dialog.at("id").as_string();
  const auto state = dialog.contains("state") && dialog.at("state").is_string() ? dialog.at("state").as_string() : "";
  if (state == "opened") {
    auto text = [&](const char* key) {
      return dialog.contains(key) && dialog.at(key).is_string() ? dialog.at(key).as_string() : std::string();
    };
    Prompt prompt;
    prompt.owner = found->second.owner;
    prompt.page = page;
    prompt.kind = "dialog";
    prompt.engine_id = dialog.at("id").as_string();
    prompt.info = Json{{"id", id},          {"page", page},
                       {"kind", "dialog"},  {"dialog", text("type")},
                       {"message", text("message")}, {"defaultValue", text("defaultValue")},
                       {"url", text("url")},
                       {"deadlineMs", 30000}};  // the engine's, dialog_controls.h
    Emit(prompt.owner, Json{{"type", "prompt.opened"}, {"prompt", prompt.info}});
    prompts_[id] = std::move(prompt);
  } else if (state == "closed") {
    const auto held = prompts_.find(id);
    if (held == prompts_.end()) return;
    // Unanswered, it closed at the engine's deadline, or a navigation reset it.
    const bool reset = dialog.contains("reason") && dialog.at("reason") == Json("reset");
    Emit(held->second.owner, Json{{"type", "prompt.closed"},
                                  {"prompt", id},
                                  {"page", page},
                                  {"outcome", !held->second.outcome.empty() ? held->second.outcome
                                              : reset                      ? "closed"
                                                                           : "timeout"}});
    prompts_.erase(held);
  }
}

}  // namespace suitecut::session
