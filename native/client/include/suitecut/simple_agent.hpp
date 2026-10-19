// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include "suitecut/agent_driver.hpp"

namespace suitecut {
// Three synchronous tools over the existing native driver. Borrows driver;
// host serializes calls and destroys this wrapper before its driver/browser.
// Only refs from the latest semantic observation are accepted. Aliases are
// monotonic and never rebound; observing again retires all previous aliases.
// Browser acknowledgements and optional UI predicates are not task grades.
// observe({boxes?,depth?,frame?|framePath?,mode?:snapshot|frames|dialogs|screenshot,
//          options?:screenshotOptions,scope?:CSS,format?:full|compact,query?,delta?,
//          maxControls?:1..256,maxTextBytes?:1..65536,timeoutMs?})
// Full (default) returns {ok,snapshot,controls,url,title}. Compact returns
// bounded controls with allowed actions plus separate non-actionable context.
// Compact deltas return semantic changes and a CURRENT refIndex, or reset:true
// with controls when their page/scope/budget differs from the last compact call.
// A semantic match does not establish physical node identity or reuse old refs.
// Screenshot mode returns {ok,mimeType,base64,bytes}; other modes wrap their data.
// act({action,ref?,value?,key?,values?,event?,x?,y?,button?,deltaX?,deltaY?,steps?,
//      source?,target?,from?:{x,y},to?:{x,y},options?,expect?:waitCondition,
//      response?:full|compact|delta|none,timeoutMs?}) or
// act({actions:[individualUIActions...],timeoutMs?}) executes <=64 known actions,
// stops on failure and observes once. No evaluate, selector or raw command action.
// wait({ref?,state?,text?,url?,changed?,timeoutMs?}) polls visible UI evidence.
// Both act/wait return a fresh observation if their one deadline permits, unless
// response:none explicitly skips it. That mode retains refs until another
// snapshot, navigation or page switch; detached nodes still fail in the backend.
// Coordinate drag interpolates 20 steps by default (1..1000); ref drag uses
// the native locator gesture. Mouse event:doubleclick sends clickCount:2.
// act result: {ok,delivered:true|false|null,execution:completed|partial|notSent|outcomeUnknown,
//              effect:unverified|confirmed|unconfirmed,completion:unverified,...}.
// delivered=null means admission/execution cannot be inferred from an exception.
// A sequence's delivered=false means the entire sequence was not delivered;
// consult its per-action results for already completed and skipped actions.
// All tools default to 5000 ms, accept 1..30000 ms, and use one shared budget.
class SimpleAgent {
 public:
  explicit SimpleAgent(AgentDriver& driver) : driver_(driver) {}
  static Json tools();
  Json call(const std::string& tool, const Json& arguments = Json::object{});
  // Resolve a current short ref for another native driver operation. Borrowed
  // page identity is checked; callers must still serialize driver calls.
  Json target_ref(const std::string& alias) const;
 private:
  using Deadline = std::chrono::steady_clock::time_point;
  Json invoke(const std::string& tool, const Json& arguments, Deadline deadline);
  Json observe(const Json& arguments, Deadline deadline);
  Json action(const Json& arguments, Deadline deadline, bool observation = true,
              bool prepare_only = false);
  Json wait(const Json& arguments, Deadline deadline, bool observation = true);
  bool condition(const Json& arguments, Deadline deadline);
  Json resolve(const std::string& alias) const;
  void fresh(Json& result, Deadline deadline);
  Json present(Json result, const Json& arguments);
  AgentDriver& driver_;
  std::uint64_t next_alias_ = 1;
  std::map<std::string, Json> refs_;
  Json baseline_;
  Json baseline_scope_ = Json::object{};
  Json compact_baseline_;
  Json compact_scope_;
  std::uint64_t revision_ = 0;
  int observed_page_ = 0;
};
} // namespace suitecut
