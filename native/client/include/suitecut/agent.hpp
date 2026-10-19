// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "suitecut/browser.hpp"

namespace suitecut {
enum class AgentActionStatus { succeeded, failed, skipped };
enum class AgentExecution { not_sent, completed, outcome_unknown };
enum class AgentEffect { unverified, confirmed, unconfirmed };
enum class AgentPhase { validation, action, verification, observation };
enum class AgentErrorCode {
  invalid_batch, action_failed, observation_failed, verification_failed, budget_exceeded
};
struct AgentError {
  AgentErrorCode code;
  std::string message;
  std::optional<std::size_t> index;
  AgentPhase phase = AgentPhase::action;
};
struct AgentEvidence {
  std::string kind;
  bool matched = false;
};
struct AgentActionResult {
  std::size_t index = 0;
  std::string action;
  AgentActionStatus status = AgentActionStatus::skipped;
  AgentExecution execution = AgentExecution::not_sent;
  Json value;
  std::optional<AgentError> error;
  AgentEffect effect = AgentEffect::unverified;
  std::optional<AgentEvidence> evidence;
};
struct AgentBatchOptions {
  std::size_t max_actions = 64;       // Hard maximum: 256.
  std::size_t max_bytes = 256 * 1024; // Hard maximum: 4 MiB.
  bool final_observation = true;
  Timeout max_duration{60000}; // Whole batch, including readbacks/observation; 1..300000 ms.
};
struct AgentBatchResult {
  bool ok = false;
  std::vector<AgentActionResult> actions;
  std::optional<AgentError> error;
  std::optional<Json> observation;
  std::optional<AgentError> observation_error;
  Json json() const;
};
// Canonical actions are objects with an "action" name. Supported actions:
// navigate(url, waitUntil?), fill(target,value), click(target),
// press(key,target?), check(target,checked?), select(target,values[]),
// pointerClick(x,y), insertText(text), observe(boxes?,depth?).
// Also: hover/focus/clear/scrollIntoView(target), type(target,value),
// wait(target,state?), upload(target,files[]), back/forward/reload(waitUntil?),
// evaluate(expression), command(family,fields). Generic commands remain bounded
// to the same page and batch deadline; backend-specific unsupported fields fail.
// Targets: {kind:ref|css|role|text|label|placeholder|testid,value:string,name?,exact?,nth?}.
// name is role-only; exact is role/text/label-only; nth is optional >=0.
// Each action may have one expect: {kind:inputValue|text|count, target, value}
// or {kind:url,value}. Values are strings for inputValue/url, string or null
// for textContent (text), and nonnegative integers for count. Checks read once.
// confirmed means this supplied postcondition matched, not task completion.
// Evidence records only kind/matched; expected and observed values are omitted.
// All shapes, types and serialized bounds are validated before any dispatch.
// Actions execute in order, stop at the first failure, and are never retried.
// One invocation can replace multiple agent tool calls; this does not make
// engine commands atomic or pipeline dependent UI mutations concurrently.
// completed means the engine acknowledged success. Existing Page exceptions
// do not expose transport admission, so every dispatched failure is conservatively
// outcome_unknown, including timeouts. Inspect/reobserve before repeating it.
// A failed postcondition keeps execution=completed and stops later actions.
// Final observation runs after execution/failure if the batch budget permits.
// Validation errors never observe or otherwise touch the browser.
// Synchronize use with other commands mutating the same Page.
class Agent {
 public:
  static AgentBatchResult run(const Page& page, const Json& actions,
                              AgentBatchOptions options = {});
};
} // namespace suitecut
