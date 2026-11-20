// SPDX-License-Identifier: MIT
// The session service's state: one SuiteCut engine, the tabs every owner has
// in it, who controls each tab, one agent per owner, events and grants.
// protocol.cpp speaks protocol 0 and protocol 1 over it.
//
// A tab is controlled by nobody, an agent or a person. A person's input or
// navigation takes it; while they hold it, agent calls on it are refused with
// code `human_in_control` until they release it.
#pragma once
#include "suitecut/agent_driver.hpp"
#include "suitecut/browser.hpp"
#include "suitecut/simple_agent.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace suitecut::session {

// A refusal with the protocol's error code.
struct Refused : std::runtime_error {
  std::string code;
  Refused(std::string code, const std::string& message) : std::runtime_error(message), code(std::move(code)) {}
};

enum class Control { unclaimed, agent, human };
const char* control_name(Control);

struct Tab {
  std::string owner;
  Control control = Control::unclaimed;
  std::uint64_t generation = 1;
  std::string url = "about:blank";
  bool adopted = true;  // false until the owner's agent has a handle for a popup
  int opener = 0;       // the page that opened this popup, if any
  // From the engine's host events (protocol 1 only).
  bool loading = false, can_go_back = false, can_go_forward = false;
  double progress = 0;
};

// A question the page asked that a person, or for dialogs the agent driving
// the tab, answers: today JavaScript dialogs.
struct Prompt {
  std::string owner;
  int page = 0;
  std::string engine_id;  // the engine's dialog id on that page
  Json info;              // what prompt.opened carries
  std::string outcome;    // set once answered through the session
};

// |maximum| 0 means any length.
bool safe_token(const std::string& value, std::size_t maximum);

struct HostOptions {
  LaunchOptions launch;
  std::string tools = "core,debug,media";      // tool groups agents may use
  std::set<std::string> schemes{"http", "https"};  // what tabs.open and tabs.navigate accept
};

// An agent call's claim on its tab, per protocol.
enum class Claim {
  after,   // protocol 0: an unclaimed tab becomes the agent's once the call has run
  before,  // protocol 1: it becomes the agent's before the call runs
};

class Host {
 public:
  explicit Host(HostOptions options);
  ~Host();
  Host(const Host&) = delete;
  Host& operator=(const Host&) = delete;

  // False once the engine connection has died. The worker then withdraws and
  // exits, so the app's next start launches a fresh engine instead of every
  // owner getting the dead one's error.
  bool Alive() const { return !engine_lost_; }
  void Close();
  // Throws engine_exited once the engine is gone.
  void Live() const;

  std::optional<int> EnginePid() const;
  std::string EngineVersion() const;
  const std::string& ToolGroups() const { return options_.tools; }
  // The service's tool names: in its groups, less what the service keeps to itself.
  const std::set<std::string>& Tools() const { return tools_; }
  // The tool names in |groups| that this service offers.
  std::set<std::string> ToolsIn(const std::string& groups) const;
  Json Catalog(const std::set<std::string>& allowed) const;

  // Tabs.
  Json List(const std::string& owner);
  Json Open(const std::string& owner, Control control, const std::optional<std::string>& url);
  void CloseTab(const std::string& owner, int page);
  // A person's navigation: fields are the engine command and its arguments.
  Json Navigate(const std::string& owner, int page, Json fields);
  // Stops the tab's load; a person's action, so it takes the tab.
  Json Stop(const std::string& owner, int page);
  Json Take(const std::string& owner, int page);
  // Lets go of a tab held as |from|; a tab held otherwise is left as it is.
  Json Release(const std::string& owner, int page, Control from);
  Json Describe(int page);
  // A copy of the owner's tab, or unknown_page.
  Tab TabOf(const std::string& owner, int page);
  std::string CheckedUrl(const Json& value) const;

  // Views. A view is identified by its page.
  int Attach(const std::string& owner, int page, const std::string& port, bool replace, bool exclusive);
  void Detach(const std::string& owner, int page);
  Json Screenshot(const std::string& owner, int page);
  void Resize(const std::string& owner, int page, int width, int height);
  // Sends a viewer's input command; |takes| makes the tab the person's first.
  void Input(const std::string& owner, int page, Json fields, bool takes);

  // Agent calls run one at a time per owner; different owners' calls do not
  // wait for each other. |expected| is protocol 1's expectGeneration.
  struct CallResult {
    Json result;
    std::uint64_t generation = 0;
    bool control_lost = false;
  };
  CallResult Agent(const std::string& owner, int page, const std::string& tool, const Json& arguments, Claim claim,
                   std::optional<std::uint64_t> expected);

  // Protocol 0's long-poll: the session-wide change counter.
  Json Watch(const std::string& owner, std::uint64_t cursor, std::chrono::milliseconds timeout);

  // Protocol 1's events, per owner. A reader is a connection; its events are
  // kept until every reader of that owner has seen them, and dropped while
  // an owner has no reader (the next one gets `resync`).
  Json NextEvents(std::uint64_t reader, const std::string& owner, std::uint64_t cursor,
                  std::chrono::milliseconds timeout);
  void ReaderGone(std::uint64_t reader);

  // Whether the engine sends host events (loading, titles, cursors, IME).
  bool HostEvents() const { return host_events_; }

  // Prompts. While an owner has a connection that said it shows prompts,
  // that owner's pages hold their dialogs for an answer (the engine's 30 s
  // deadline still applies); with none, dialogs get their default answers,
  // as before. |host| is false for an agent, which may answer only a dialog
  // on a tab it holds.
  void PromptHost(std::uint64_t connection, const std::string& owner);
  Json AnswerPrompt(const std::string& owner, bool host, const std::string& prompt, const Json& answer);

  // Grants: one owner, chosen tool groups, an optional lifetime.
  struct Grant {
    std::string owner;
    std::string groups;
    std::optional<std::chrono::steady_clock::time_point> expires;
  };
  std::string AddGrant(Grant grant);
  bool Revoke(const std::string& owner, const std::string& token);
  std::optional<Grant> GrantFor(const std::string& token);

 private:
  struct OwnerAgent {
    std::mutex mutex;
    std::unique_ptr<AgentDriver> driver;
    std::unique_ptr<SimpleAgent> simple;
  };
  struct Events {
    // Cursors start at 1, so a cursor of 0 always means a first read.
    std::uint64_t sequence = 1;  // the last event's number
    std::uint64_t floor = 1;     // cursors below this have missed events
    std::deque<std::pair<std::uint64_t, Json>> kept;
    std::map<std::uint64_t, std::uint64_t> readers;  // reader -> the cursor it last asked from
  };

  OwnerAgent& AgentOf(const std::string& owner);
  Json Run(OwnerAgent& agent, const std::string& name, const Json& arguments);
  std::string Title(int page);
  void NavigateEngine(int page, Json fields);
  void TakeLocked(int page, Tab& tab);
  Json Engine(int page, Json fields, std::chrono::milliseconds timeout = std::chrono::milliseconds(10000));
  Tab& Owned(const std::string& owner, int page);
  Json TabJson(int page, const Tab& tab, const std::string& title) const;
  // Turns dialog holding on or off for |pages|, outside state_.
  void HoldDialogs(const std::vector<int>& pages, bool hold);
  // With state_ held: the page's prompts are gone, with |outcome|.
  void DropPrompts(int page, const std::string& outcome);
  void HostPacket(int page, const Json& event);
  void DialogPacket(int page, const Json& dialog);
  // With state_ held: the change counter, and an event for |owner|.
  void Changed();
  void Emit(const std::string& owner, Json event);
  void EmitTab(int page, const Tab& tab);
  // With the owner's call lock held: its driver drops the pages the engine
  // closed and takes a handle for each new popup.
  void SyncPages(const std::string& owner, OwnerAgent& agent);
  void Pump();

  HostOptions options_;
  std::unique_ptr<Browser> browser_;
  std::shared_ptr<Transport> transport_;
  std::set<std::string> tools_;
  Json catalog_ = Json::array{};

  std::mutex owners_mutex_;
  std::map<std::string, std::unique_ptr<OwnerAgent>> owners_;

  mutable std::mutex state_;
  std::condition_variable changed_;
  std::map<int, Tab> tabs_;
  std::set<int> views_;  // pages with an attached view, for view_busy
  std::uint64_t version_ = 1;
  std::map<std::string, Events> events_;
  std::map<std::string, Grant> grants_;
  // Pages the engine closed that an owner's driver still holds, until
  // SyncPages drops them.
  std::map<std::string, std::vector<int>> gone_;
  bool host_events_ = false;
  std::map<std::string, Prompt> prompts_;
  std::map<std::string, std::set<std::uint64_t>> prompt_hosts_;  // owner -> connections that show prompts

  std::atomic<bool> stopping_{false}, closed_{false}, engine_lost_{false};
  std::thread pump_;
};

}  // namespace suitecut::session
