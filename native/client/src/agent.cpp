// SPDX-License-Identifier: MIT
#include "suitecut/agent.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace suitecut {
namespace {
using Keys = std::initializer_list<std::string_view>;
void require(bool value, const char* message) {
  if (!value) throw std::invalid_argument(message);
}
void keys(const Json& object, Keys allowed, bool action = false) {
  require(object.is_object(), "Expected an action or target object");
  for (const auto& [key, unused] : object.as_object()) {
    (void)unused;
    if (!(action && key == "expect") && std::find(allowed.begin(), allowed.end(), key) == allowed.end())
      throw std::invalid_argument("Unknown field: " + key);
  }
}
const std::string& string(const Json& object, std::string_view key, bool empty = false) {
  const auto& value = object.at(key);
  require(value.is_string(), "Expected a string field");
  require(empty || !value.as_string().empty(), "Expected a nonempty string field");
  return value.as_string();
}
void boolean(const Json& object, std::string_view key) {
  if (object.contains(key)) require(object.at(key).is_bool(), "Expected a boolean field");
}
void target(const Json& object) {
  keys(object, {"kind", "value", "name", "exact", "nth"});
  const auto& kind = string(object, "kind");
  string(object, "value");
  require(kind == "ref" || kind == "css" || kind == "role" || kind == "text" ||
              kind == "label" || kind == "placeholder" || kind == "testid", "Unknown target kind");
  if (object.contains("name")) {
    require(kind == "role", "Target name is only valid for role targets");
    string(object, "name", true);
  }
  if (object.contains("exact")) {
    require(kind == "role" || kind == "text" || kind == "label" || kind == "placeholder",
            "Target exact is only valid for role, text or label targets");
    boolean(object, "exact");
  }
  if (object.contains("nth")) {
    const auto& n = object.at("nth");
    require(n.is_integer() && n.as_int() >= 0 && n.as_int() <= std::numeric_limits<int>::max(),
            "Target nth must be an integer in [0, INT_MAX]");
  }
}
void validate(const Json& a) {
  require(a.is_object(), "Expected an action object");
  const auto& op = string(a, "action");
  if (op == "navigate") {
    keys(a, {"action", "url", "waitUntil"}, true); string(a, "url");
    if (a.contains("waitUntil")) {
      const auto& wait = string(a, "waitUntil");
      require(wait == "load" || wait == "domcontentloaded" || wait == "commit" ||
                  wait == "networkidle", "Unknown navigation waitUntil");
    }
  } else if (op == "hover" || op == "focus" || op == "clear" || op == "scrollIntoView") {
    keys(a,{"action","target"},true); target(a.at("target"));
  } else if (op == "type") {
    keys(a,{"action","target","value"},true); target(a.at("target")); string(a,"value",true);
  } else if (op == "evaluate") {
    keys(a,{"action","expression"},true); string(a,"expression");
  } else if (op == "command") {
    keys(a,{"action","family","fields"},true); string(a,"family");
    require(a.at("fields").is_object(),"Command fields must be an object");
    // Control envelope fields always belong to this batch's owning page/budget.
    for (const auto* field : {"command","pageId","id","version","timeoutMs"})
      require(!a.at("fields").contains(field),"Batch command fields cannot override its control envelope");
  } else if (op == "wait") {
    keys(a,{"action","target","state"},true); target(a.at("target"));
    if (a.contains("state")) {
      const auto& state = string(a,"state");
      require(state == "visible" || state == "hidden" || state == "attached" || state == "detached","Unknown element wait state");
    }
  } else if (op == "upload") {
    keys(a,{"action","target","files"},true); target(a.at("target"));
    require(a.at("files").is_array() && a.at("files").as_array().size() <= 1000,"Upload files must be an array with at most 1000 paths");
    for (const auto& path : a.at("files").as_array())
      require(path.is_string() && !path.as_string().empty(),"Upload path must be a nonempty string");
  } else if (op == "back" || op == "forward" || op == "reload") {
    keys(a,{"action","waitUntil"},true);
    if (a.contains("waitUntil")) {
      const auto& wait = string(a,"waitUntil");
      require(wait == "commit" || wait == "load" || wait == "domcontentloaded" || wait == "networkidle","Unknown navigation waitUntil");
    }
  } else if (op == "click" || op == "fill" || op == "check" || op == "select") {
    if (op == "click") keys(a, {"action", "target"}, true);
    else if (op == "fill") { keys(a, {"action", "target", "value"}, true); string(a, "value", true); }
    else if (op == "check") { keys(a, {"action", "target", "checked"}, true); boolean(a, "checked"); }
    else {
      keys(a, {"action", "target", "values"}, true);
      const auto& choices = a.at("values");
      require(choices.is_array() && choices.as_array().size() <= 256,
              "Select values must be an array of at most 256 strings");
      for (const auto& value : choices.as_array()) require(value.is_string(), "Select value must be a string");
    }
    target(a.at("target"));
  } else if (op == "press") {
    keys(a, {"action", "target", "key"}, true); string(a, "key");
    if (a.contains("target")) target(a.at("target"));
  } else if (op == "pointerClick") {
    keys(a, {"action", "x", "y"}, true);
    for (auto name : {"x", "y"}) {
      const auto& number = a.at(name);
      require(number.is_number() && std::isfinite(number.as_double()) &&
                  number.as_double() >= 0 && number.as_double() <= 1000000,
              "Pointer coordinates must be finite numbers in [0, 1000000]");
    }
  } else if (op == "insertText") {
    keys(a, {"action", "text"}, true); string(a, "text", true);
  } else if (op == "observe") {
    keys(a, {"action", "boxes", "depth"}, true); boolean(a, "boxes");
    if (a.contains("depth")) {
      const auto& depth = a.at("depth");
      require(depth.is_integer() && depth.as_int() >= -1 && depth.as_int() <= 64,
              "Observation depth must be an integer in [-1, 64]");
    }
  } else throw std::invalid_argument("Unknown action: " + op);
  if (a.contains("expect")) {
    const auto& check = a.at("expect");
    require(check.is_object(), "Expected an expect object");
    const auto& kind = string(check, "kind");
    require(kind == "inputValue" || kind == "text" || kind == "count" || kind == "url",
            "Unknown expect kind");
    if (kind == "url") keys(check, {"kind", "value"});
    else {
      keys(check, {"kind", "value", "target"});
      target(check.at("target"));
    }
    const auto& expected = check.at("value");
    if (kind == "count")
      require(expected.is_integer() && expected.as_int() >= 0,
              "Expected count must be a nonnegative integer");
    else if (kind == "text")
      require(expected.is_string() || expected.is_null(), "Expected text must be a string or null");
    else require(expected.is_string(), "Expected value must be a string");
  }
  // Target descriptions can appear twice in a wire request; leave ample room
  // for protocol fields beneath the native transport's 64 KiB command bound.
  JsonLimits limits; limits.max_bytes = 16 * 1024; limits.max_string_bytes = 16 * 1024;
  a.dump(limits);
}
Locator locator(const Page& page, const Json& target) {
  const auto& kind = target.at("kind").as_string();
  const auto& value = target.at("value").as_string();
  const bool exact = !target.contains("exact") || target.at("exact").as_bool();
  auto result = [&] {
    if (kind == "ref") return page.get_by_ref(value);
    if (kind == "css") return page.locator(value);
    if (kind == "role") return page.get_by_role(value,
        target.contains("name") ? target.at("name").as_string() : "", exact);
    if (kind == "text") return page.get_by_text(value, exact);
    if (kind == "placeholder") return page.get_by_placeholder(value,exact);
    if (kind == "testid") return page.get_by_test_id(value);
    return page.get_by_label(value, exact);
  }();
  return target.contains("nth") ? result.nth(static_cast<int>(target.at("nth").as_int())) : result;
}
Json execute(const Page& page, const Json& a) {
  const auto& op = a.at("action").as_string();
  if (op == "navigate") return page.navigate(a.at("url").as_string(),
      a.contains("waitUntil") ? a.at("waitUntil").as_string() : "load");
  if (op == "observe") return page.observe(a.contains("boxes") && a.at("boxes").as_bool(),
      a.contains("depth") ? static_cast<int>(a.at("depth").as_int()) : -1);
  if (op == "evaluate") return page.evaluate(a.at("expression").as_string());
  if (op == "command") return page.command(a.at("family").as_string(),a.at("fields"));
  if (op == "back") return page.go_back(a.contains("waitUntil") ? a.at("waitUntil").as_string() : "load");
  if (op == "forward") return page.go_forward(a.contains("waitUntil") ? a.at("waitUntil").as_string() : "load");
  if (op == "reload") return page.reload(a.contains("waitUntil") ? a.at("waitUntil").as_string() : "load");
  if (op == "pointerClick") page.mouse_click(a.at("x").as_double(), a.at("y").as_double());
  else if (op == "insertText") page.insert_text(a.at("text").as_string());
  else if (op == "press") {
    if (a.contains("target")) locator(page, a.at("target")).press(a.at("key").as_string());
    else page.keyboard_press(a.at("key").as_string());
  } else {
    auto element = locator(page, a.at("target"));
    if (op == "click") element.click();
    else if (op == "hover") element.hover();
    else if (op == "focus") element.focus();
    else if (op == "clear") element.clear();
    else if (op == "scrollIntoView") element.scroll_into_view();
    else if (op == "type") element.type(a.at("value").as_string());
    else if (op == "wait") element.wait_for(a.contains("state") ? a.at("state").as_string() : "visible");
    else if (op == "upload") {
      std::vector<std::string> files;
      for (const auto& path : a.at("files").as_array()) files.push_back(path.as_string());
      element.set_input_files(files);
    }
    else if (op == "fill") element.fill(a.at("value").as_string());
    else if (op == "check") element.check(!a.contains("checked") || a.at("checked").as_bool());
    else if (op == "select") {
      std::vector<std::string> values;
      for (const auto& value : a.at("values").as_array()) values.push_back(value.as_string());
      return element.select_options(values);
    }
  }
  return Json{};
}
bool verify(const Page& page, const Json& check) {
  const auto& kind = check.at("kind").as_string();
  Json observed;
  if (kind == "url") observed = page.evaluate("location.href");
  else {
    const auto element = locator(page, check.at("target"));
    if (kind == "inputValue") observed = element.input_value();
    else if (kind == "text") observed = element.text_content();
    else observed = element.count();
  }
  return observed == check.at("value");
}
const char* phase_name(AgentPhase phase) {
  switch (phase) {
    case AgentPhase::validation: return "validation";
    case AgentPhase::action: return "action";
    case AgentPhase::verification: return "verification";
    case AgentPhase::observation: return "observation";
  }
  return "action";
}
Json error_json(const AgentError& error) {
  const char* code = error.code == AgentErrorCode::invalid_batch ? "invalidBatch" :
      error.code == AgentErrorCode::action_failed ? "actionFailed" :
      error.code == AgentErrorCode::verification_failed ? "verificationFailed" :
      error.code == AgentErrorCode::budget_exceeded ? "budgetExceeded" : "observationFailed";
  Json out{{"code", code}, {"message", error.message}, {"phase", phase_name(error.phase)}};
  if (error.index) out["index"] = *error.index;
  return out;
}
class BudgetExceeded : public std::runtime_error {
 public:
  BudgetExceeded() : std::runtime_error("Batch time budget exhausted") {}
};
std::string action_budget_message(const std::exception& failure,const Json& action) {
  if(dynamic_cast<const BudgetExceeded*>(&failure))return "Batch time budget exhausted";
  // Preserve the native cause without echoing input or expected values that
  // an engine might have included in its diagnostic. No dispatch claim changes.
  std::vector<std::string> secrets;
  for(const auto* key:{"value","text"})if(action.contains(key)&&action.at(key).is_string()&&!action.at(key).as_string().empty())secrets.push_back(action.at(key).as_string());
  if(action.contains("expect")&&action.at("expect").contains("value")&&action.at("expect").at("value").is_string()&&!action.at("expect").at("value").as_string().empty())secrets.push_back(action.at("expect").at("value").as_string());
  const std::string cause=failure.what();std::string safe;
  for(std::size_t i=0;i<cause.size()&&safe.size()<4096;){std::size_t matched=0;for(const auto& secret:secrets)if(secret.size()>matched&&cause.compare(i,secret.size(),secret)==0)matched=secret.size();if(matched){safe+="[redacted]";i+=matched;}else safe+=cause[i++];}
  return "Batch time budget exhausted: "+safe;
}
using Clock = std::chrono::steady_clock;
Page capped_page(const Page& page, Clock::time_point deadline) {
  const auto now = Clock::now();
  if (now >= deadline) throw BudgetExceeded{};
  const auto remaining = std::chrono::ceil<Timeout>(deadline - now);
  return page.with_timeout(std::min(page.timeout(), remaining));
}
} // namespace
AgentBatchResult Agent::run(const Page& page, const Json& actions, AgentBatchOptions options) {
  AgentBatchResult result;
  const auto started = Clock::now();
  std::optional<std::size_t> validating;
  try {
    require(options.max_duration.count() >= 1 && options.max_duration.count() <= 300000,
            "max_duration must be in [1, 300000] milliseconds");
    require(options.max_actions >= 1 && options.max_actions <= 256,
            "max_actions must be in [1, 256]");
    require(options.max_bytes >= 1 && options.max_bytes <= 4 * 1024 * 1024,
            "max_bytes must be in [1, 4194304]");
    require(actions.is_array(), "Actions must be an array");
    require(actions.as_array().size() <= options.max_actions, "Action count exceeds max_actions");
    JsonLimits limits; limits.max_bytes = options.max_bytes; limits.max_string_bytes = options.max_bytes;
    actions.dump(limits);
    result.actions.resize(actions.as_array().size());
    for (std::size_t i = 0; i < actions.as_array().size(); ++i) {
      result.actions[i].index = i;
      const auto& action = actions.at(i);
      if (action.is_object() && action.contains("action") && action.at("action").is_string())
        result.actions[i].action = action.at("action").as_string();
    }
    for (std::size_t i = 0; i < actions.as_array().size(); ++i) {
      validating = i; validate(actions.at(i));
    }
  } catch (const std::exception& failure) {
    result.error = AgentError{AgentErrorCode::invalid_batch, failure.what(), validating, AgentPhase::validation};
    return result;
  }
  const auto deadline = started + options.max_duration;
  result.ok = true;
  for (std::size_t i = 0; i < result.actions.size(); ++i) {
    auto& item = result.actions[i];
    AgentPhase phase = AgentPhase::action;
    try {
      auto view = capped_page(page, deadline);
      item.execution = AgentExecution::outcome_unknown;
      item.value = execute(view, actions.at(i));
      item.execution = AgentExecution::completed;
      item.status = AgentActionStatus::succeeded;
      if (Clock::now() >= deadline) throw BudgetExceeded{};
      if (actions.at(i).contains("expect")) {
        phase = AgentPhase::verification;
        const auto& check = actions.at(i).at("expect");
        auto read_view = capped_page(page, deadline);
        const bool matched = verify(read_view, check);
        item.evidence = AgentEvidence{check.at("kind").as_string(), matched};
        item.effect = matched ? AgentEffect::confirmed : AgentEffect::unconfirmed;
        if (Clock::now() >= deadline) {
          item.effect = AgentEffect::unverified;
          item.evidence.reset();
          throw BudgetExceeded{};
        }
        if (!matched) throw std::runtime_error("Postcondition did not match");
      }
    } catch (const std::exception& failure) {
      item.status = AgentActionStatus::failed;
      const bool budget = dynamic_cast<const BudgetExceeded*>(&failure) || Clock::now() >= deadline;
      const auto code = budget ? AgentErrorCode::budget_exceeded :
          phase == AgentPhase::verification ? AgentErrorCode::verification_failed : AgentErrorCode::action_failed;
      const std::string message = budget ? (phase == AgentPhase::action ? action_budget_message(failure,actions.at(i)) : "Batch time budget exhausted") :
          phase == AgentPhase::verification ? (item.evidence ? "Postcondition did not match" : "Postcondition readback failed") :
          failure.what();
      item.error = AgentError{code, message, i, phase};
      result.error = item.error;
      result.ok = false;
      break;
    }
  }
  if (options.final_observation) {
    try {
      auto view = capped_page(page, deadline);
      auto observation = view.observe();
      if (Clock::now() >= deadline) throw BudgetExceeded{};
      result.observation = std::move(observation);
    } catch (const std::exception& failure) {
      const bool budget = dynamic_cast<const BudgetExceeded*>(&failure) || Clock::now() >= deadline;
      result.observation_error = AgentError{budget ? AgentErrorCode::budget_exceeded : AgentErrorCode::observation_failed,
          budget ? "Batch time budget exhausted" : failure.what(), {}, AgentPhase::observation};
      result.ok = false;
    }
  }
  return result;
}
Json AgentBatchResult::json() const {
  Json::array items;
  items.reserve(actions.size());
  for (const auto& item : actions) {
    const char* status = item.status == AgentActionStatus::succeeded ? "succeeded" :
        item.status == AgentActionStatus::failed ? "failed" : "skipped";
    const char* execution = item.execution == AgentExecution::completed ? "completed" :
        item.execution == AgentExecution::not_sent ? "notSent" : "outcomeUnknown";
    const char* effect = item.effect == AgentEffect::confirmed ? "confirmed" :
        item.effect == AgentEffect::unconfirmed ? "unconfirmed" : "unverified";
    Json row{{"index", item.index}, {"action", item.action}, {"status", status}, {"execution", execution},
             {"effect", effect}};
    if (item.evidence) row["evidence"] = Json{{"kind", item.evidence->kind}, {"matched", item.evidence->matched}};
    if (item.execution == AgentExecution::completed) row["value"] = item.value;
    if (item.error) row["error"] = error_json(*item.error);
    items.push_back(std::move(row));
  }
  Json out{{"ok", ok}, {"actions", std::move(items)}};
  if (error) out["error"] = error_json(*error);
  if (observation) out["observation"] = *observation;
  if (observation_error) out["observationError"] = error_json(*observation_error);
  return out;
}
} // namespace suitecut
