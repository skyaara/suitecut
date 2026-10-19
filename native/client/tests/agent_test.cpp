// SPDX-License-Identifier: MIT
// Controlled protocol seam: exercises real transport, not browser semantics.
#include "suitecut/agent.hpp"
#include "suitecut/agent_driver.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unistd.h>

using suitecut::Json;
using namespace std::chrono_literals;
namespace {
void check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void packet(Json header, const Json& result = {}) {
  const auto payload = header.at("type").as_string() == "ready" ? std::string{} : result.dump();
  header["version"] = 1; header["bytes"] = payload.size();
  const auto text = header.dump();
  const auto size = static_cast<std::uint32_t>(text.size());
  const char prefix[4]{static_cast<char>(size), static_cast<char>(size >> 8),
                       static_cast<char>(size >> 16), static_cast<char>(size >> 24)};
  std::cout.write(prefix, 4).write(text.data(), static_cast<std::streamsize>(text.size()));
  std::cout.write(payload.data(), static_cast<std::streamsize>(payload.size())).flush();
}
int engine() {
  packet(Json{{"type", "ready"}, {"pageId", 1}, {"cefVersion", "agent-test"}, {"timestampMs", 1}});
  std::size_t mutations = 0;
  std::string input, url;
  bool slow_readback = false, slow_observation = false;
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto request = Json::parse(line);
    const auto& command = request.at("command").as_string();
    Json result;
    bool ok = true;
    std::string error;
    if (command == "snapshotReferences") {
      if (slow_observation) {
        slow_observation = false;
        std::this_thread::sleep_for(80ms);
      }
      result = Json{{"snapshotJSON", Json(Json::array{Json{{"mutations", mutations},{"timeoutMs",request.at("timeoutMs")}}}).dump()}};
    } else if (command == "queryEvaluate") {
      if (request.contains("count"))
        result = Json{{"result", Json{{"value", Json::array{Json("primitive"), Json(1)}}}}};
      else result = Json{{"result", Json{{"value", input}}}};
    } else if (command == "evaluate") {
      check(request.at("expression").as_string() == "location.href", "Only fixed URL readback is allowed");
      result = Json{{"result", Json{{"value", url}}}};
    } else if (command == "locator" && request.at("action").as_string() == "inputValue") {
      if (slow_readback) {
        slow_readback = false;
        std::this_thread::sleep_for(80ms);
      }
      if (input == "read-error-secret") {
        ok = false; error = "Read failed with read-error-secret";
      } else result = input;
    } else if (command != "cancel") {
      ++mutations;
      if (command == "navigateFrame") url = request.at("url").as_string();
      if (command == "locator" && request.at("action").as_string() == "fill")
        input = request.at("value").as_string();
      if (request.contains("key") && request.at("key").as_string() == "SLOW_READ") slow_readback = true;
      if (request.contains("key") && request.at("key").as_string() == "SLOW_OBSERVE") slow_observation = true;
      if (request.contains("key") && request.at("key").as_string() == "FAIL") {
        ok = false; error = "Fixture rejected action after dispatch";
      }
      if (request.contains("key") && request.at("key").as_string() == "SLOW")
        std::this_thread::sleep_for(80ms);
      if (command == "form" && request.at("action").as_string() == "selectOption")
        result = request.at("options");
    }
    packet(Json{{"type", "response"}, {"id", request.at("id")}, {"ok", ok}, {"error", error}}, result);
  }
  return 0;
}
std::size_t mutations(const suitecut::Page& page) {
  return static_cast<std::size_t>(page.observe().at(0).at("mutations").as_int());
}
Json target(const char* kind, const char* value) { return Json{{"kind", kind}, {"value", value}}; }
} // namespace
int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i)
    if (std::string(argv[i]) == "--engine") return engine();
  try {
    char path[4096];
    const auto length = ::readlink("/proc/self/exe", path, sizeof(path));
    check(length > 0 && static_cast<std::size_t>(length) < sizeof(path), "Resolve test executable");
    suitecut::LaunchOptions options;
    options.executable = std::string(path, static_cast<std::size_t>(length));
    options.arguments = {"--engine"};
    auto browser = suitecut::Browser::launch(options);
    auto page = browser.main_page();
    suitecut::AgentDriver driver(browser);
    const auto original_timeout=driver.call("snapshot").at(0).at("timeoutMs").as_int();
    const auto bounded_snapshot=driver.call_with_timeout("snapshot",Json::object{},17ms);
    check(bounded_snapshot.at(0).at("timeoutMs").as_int()<=17,"Bounded driver caps the real page command timeout");
    check(driver.call("snapshot").at(0).at("timeoutMs").as_int()==original_timeout,"Bounded driver restores the page timeout after success");
    bool scoped_failure=false;
    try{driver.call_with_timeout("get",Json{{"kind","unsupported"},{"selector","input"}},5ms);}catch(const std::invalid_argument&){scoped_failure=true;}
    check(scoped_failure&&driver.call("snapshot").at(0).at("timeoutMs").as_int()==original_timeout,"Bounded driver restores the page timeout after exceptions");
    suitecut::AgentBatchOptions no_observe; no_observe.final_observation = false;
    const Json invalid = Json::array{
        Json{{"action", "click"}, {"target", target("css", "#submit")}},
        Json{{"action", "fill"}, {"target", target("label", "Name")}, {"value", 123}}};
    const auto before = browser.transport()->stats().commands;
    auto rejected = suitecut::Agent::run(page, invalid);
    check(!rejected.ok && rejected.error->code == suitecut::AgentErrorCode::invalid_batch &&
              rejected.error->index == 1, "Whole batch validation identifies the invalid item");
    check(rejected.actions.size() == 2 && rejected.actions[0].execution == suitecut::AgentExecution::not_sent &&
              rejected.actions[1].status == suitecut::AgentActionStatus::skipped,
          "Validation skips every action");
    check(before == browser.transport()->stats().commands, "Validation cannot mutate or observe");
    for (const auto& bad : Json::array{
             Json(Json::object{}),
             Json(Json::array{Json{{"action", "click"}, {"target", target("unknown", "x")}}}),
             Json(Json::array{Json{{"action", "press"}, {"key", "Enter"}, {"retry", true}}}),
             Json(Json::array{Json{{"action", "pointerClick"}, {"x", -1}, {"y", 0}}}),
             Json(Json::array{Json{{"action", "observe"}, {"depth", 100}}}),
             Json(Json::array{Json{{"action", "insertText"}, {"text", std::string(17000, 'x')}}})}) {
      check(!suitecut::Agent::run(page, bad).ok, "Invalid canonical action must fail before dispatch");
    }
    for (const auto& bad_expect : Json::array{
             Json{{"kind", "inputValue"}, {"value", 1}, {"target", target("css", "input")}},
             Json{{"kind", "text"}, {"value", true}, {"target", target("css", "p")}},
             Json{{"kind", "count"}, {"value", -1}, {"target", target("css", "input")}},
             Json{{"kind", "count"}, {"value", 1.0}, {"target", target("css", "input")}},
             Json{{"kind", "inputValue"}, {"value", "x"}},
             Json{{"kind", "url"}, {"value", "x"}, {"target", target("css", "p")}},
             Json{{"kind", "url"}, {"value", "x"}, {"script", "true"}},
             Json{{"kind", "script"}, {"value", "true"}}}) {
      auto bad = suitecut::Agent::run(page, Json::array{
          Json{{"action", "press"}, {"key", "Enter"}},
          Json{{"action", "press"}, {"key", "Enter"}, {"expect", bad_expect}}});
      check(!bad.ok && bad.error->index == 1 && bad.error->phase == suitecut::AgentPhase::validation,
            "All postcondition shapes validate before any action");
    }
    for (const auto invalid_duration : {0ms, 300001ms}) {
      auto limits = no_observe; limits.max_duration = invalid_duration;
      check(!suitecut::Agent::run(page, Json::array{}, limits).ok, "Batch duration bounds validate");
    }
    check(before == browser.transport()->stats().commands, "Every invalid shape remains side-effect free");
    Json::array too_many(65, Json{{"action", "observe"}});
    check(!suitecut::Agent::run(page, Json(too_many)).ok, "Action count is bounded");
    auto small = no_observe; small.max_bytes = 5;
    check(!suitecut::Agent::run(page, Json::array{Json{{"action", "observe"}}}, small).ok,
          "Serialized batch size is bounded");
    const Json actions = Json::array{
        Json{{"action", "navigate"}, {"url", "https://example.test"}},
        Json{{"action", "fill"}, {"target", target("label", "Name")}, {"value", "café 東京"}},
        Json{{"action", "click"}, {"target", Json{{"kind", "role"}, {"value", "button"}, {"name", "Save"}, {"exact", false}}}},
        Json{{"action", "check"}, {"target", target("ref", "r_test")}, {"checked", false}},
        Json{{"action", "select"}, {"target", target("css", "select")}, {"values", Json::array{Json("Team")}}},
        Json{{"action", "press"}, {"target", target("text", "Next")}, {"key", "Enter"}},
        Json{{"action", "press"}, {"key", "Tab"}},
        Json{{"action", "pointerClick"}, {"x", 12.5}, {"y", 20}},
        Json{{"action", "insertText"}, {"text", "secret"}},
        Json{{"action", "observe"}, {"boxes", true}, {"depth", 3}}};
    auto success = suitecut::Agent::run(page, actions);
    check(success.ok && success.actions.size() == 10 && success.observation.has_value(),
          "All canonical action families execute with final observation");
    check(success.observation->at(0).at("mutations").as_int() == 9, "Sequential commands execute once");
    check(success.actions[4].value.at(0).at("valueOrLabel").as_string() == "Team", "Selection return is preserved");
    check(success.json().at("actions").at(0).at("execution").as_string() == "completed", "Typed result serializes");
    check(success.actions[0].effect == suitecut::AgentEffect::unverified &&
              !success.actions[0].evidence,
          "Acknowledgment alone never confirms an effect");
    const auto prior = mutations(page);
    auto failure = suitecut::Agent::run(page, Json::array{
        Json{{"action", "press"}, {"key", "FAIL"}},
        Json{{"action", "insertText"}, {"text", "must not execute"}}});
    check(!failure.ok && failure.actions[0].status == suitecut::AgentActionStatus::failed &&
              failure.actions[0].execution == suitecut::AgentExecution::outcome_unknown,
          "Dispatched failures conservatively preserve execution uncertainty");
    check(failure.error->message == "Fixture rejected action after dispatch", "Original engine error remains intact");
    check(failure.actions[1].status == suitecut::AgentActionStatus::skipped &&
              failure.actions[1].execution == suitecut::AgentExecution::not_sent,
          "Stop on first error skips dependent actions");
    check(failure.observation->at(0).at("mutations").as_int() == static_cast<std::int64_t>(prior + 1),
          "Recovery observation follows failure without retry");
    page.set_timeout(15ms);
    auto timeout = suitecut::Agent::run(page, Json::array{
        Json{{"action", "press"}, {"key", "SLOW"}},
        Json{{"action", "press"}, {"key", "Enter"}}}, no_observe);
    check(!timeout.ok && timeout.error->code == suitecut::AgentErrorCode::action_failed &&
              timeout.actions[0].execution == suitecut::AgentExecution::outcome_unknown &&
              timeout.actions[1].execution == suitecut::AgentExecution::not_sent,
          "Timed out mutation must never claim notSent or retry");
    page.set_timeout(1000ms);
    check(mutations(page) == prior + 2, "Timed out mutation was applied exactly once");
    auto verified = suitecut::Agent::run(page, Json::array{
        Json{{"action", "fill"}, {"target", target("css", "input")}, {"value", "password-secret"},
             {"expect", Json{{"kind", "inputValue"}, {"target", target("css", "input")}, {"value", "password-secret"}}}},
        Json{{"action", "press"}, {"key", "Enter"},
             {"expect", Json{{"kind", "text"}, {"target", target("css", "p")}, {"value", "password-secret"}}}},
        Json{{"action", "press"}, {"key", "Enter"},
             {"expect", Json{{"kind", "count"}, {"target", target("css", "p")}, {"value", 1}}}},
        Json{{"action", "navigate"}, {"url", "https://fixture.test/next"},
             {"expect", Json{{"kind", "url"}, {"value", "https://fixture.test/next"}}}}}, no_observe);
    check(verified.ok && verified.actions[0].effect == suitecut::AgentEffect::confirmed &&
              verified.actions[3].evidence->matched, "Typed one-shot readbacks confirm supplied postconditions");
    check(verified.json().dump().find("password-secret") == std::string::npos &&
              verified.json().dump().find("fixture.test") == std::string::npos,
          "Evidence never echoes expected or observed input/URL values");
    auto mismatch = suitecut::Agent::run(page, Json::array{
        Json{{"action", "press"}, {"key", "Enter"},
             {"expect", Json{{"kind", "inputValue"}, {"target", target("css", "input")}, {"value", "different-secret"}}}},
        Json{{"action", "press"}, {"key", "Enter"}}});
    check(!mismatch.ok && mismatch.actions[0].execution == suitecut::AgentExecution::completed &&
              mismatch.actions[0].effect == suitecut::AgentEffect::unconfirmed &&
              !mismatch.actions[0].evidence->matched &&
              mismatch.error->code == suitecut::AgentErrorCode::verification_failed &&
              mismatch.error->phase == suitecut::AgentPhase::verification &&
              mismatch.actions[1].execution == suitecut::AgentExecution::not_sent && mismatch.observation,
          "Failed postcondition preserves acknowledgment, stops, and observes");
    check(mismatch.json().at("error").at("code").as_string() == "verificationFailed" &&
              mismatch.json().dump().find("different-secret") == std::string::npos,
          "Verification failure serializes without value leakage");
    auto unavailable = suitecut::Agent::run(page, Json::array{
        Json{{"action", "fill"}, {"target", target("css", "input")}, {"value", "read-error-secret"},
             {"expect", Json{{"kind", "inputValue"}, {"target", target("css", "input")}, {"value", "read-error-secret"}}}}}, no_observe);
    check(!unavailable.ok && unavailable.actions[0].execution == suitecut::AgentExecution::completed &&
              unavailable.actions[0].effect == suitecut::AgentEffect::unverified &&
              !unavailable.actions[0].evidence && unavailable.json().dump().find("read-error-secret") == std::string::npos,
          "Readback exceptions remain unverified and redact engine diagnostics");
    auto budget = no_observe; budget.max_duration = 130ms;
    const auto before_budget = mutations(page);
    auto exhausted = suitecut::Agent::run(page, Json::array{
        Json{{"action", "press"}, {"key", "SLOW"}},
        Json{{"action", "press"}, {"key", "SLOW"}},
        Json{{"action", "press"}, {"key", "Enter"}}}, budget);
    check(!exhausted.ok && exhausted.actions[0].execution == suitecut::AgentExecution::completed &&
              exhausted.actions[1].execution == suitecut::AgentExecution::outcome_unknown &&
              exhausted.actions[2].execution == suitecut::AgentExecution::not_sent &&
              exhausted.error->code == suitecut::AgentErrorCode::budget_exceeded &&
              exhausted.error->phase == suitecut::AgentPhase::action,
          "One cumulative budget caps later dispatch and preserves timeout uncertainty");
    check(exhausted.error->message.starts_with("Batch time budget exhausted: ")&&
              exhausted.error->message.size()>std::string("Batch time budget exhausted: ").size(),
          "Action deadline preserves its underlying native/transport cause");
    check(page.timeout() == 1000ms && mutations(page) == before_budget + 2,
          "Capped views do not change caller timeout or retry input");
    budget.max_duration = 15ms;
    auto read_budget = suitecut::Agent::run(page, Json::array{
        Json{{"action", "press"}, {"key", "SLOW_READ"},
             {"expect", Json{{"kind", "inputValue"}, {"target", target("css", "input")}, {"value", "x"}}}},
        Json{{"action", "press"}, {"key", "Enter"}}}, budget);
    check(!read_budget.ok && read_budget.error->code == suitecut::AgentErrorCode::budget_exceeded &&
              read_budget.error->phase == suitecut::AgentPhase::verification &&
              read_budget.actions[0].execution == suitecut::AgentExecution::completed &&
              read_budget.actions[1].execution == suitecut::AgentExecution::not_sent,
          "Verification consumes the same deadline without losing completed action status");
    (void)mutations(page); // Drain the delayed fixture reply before the next bounded batch.
    budget.final_observation = true;
    auto observe_budget = suitecut::Agent::run(page, Json::array{
        Json{{"action", "press"}, {"key", "SLOW_OBSERVE"}}}, budget);
    check(!observe_budget.ok && observe_budget.actions[0].execution == suitecut::AgentExecution::completed &&
              observe_budget.observation_error->code == suitecut::AgentErrorCode::budget_exceeded &&
              observe_budget.observation_error->phase == suitecut::AgentPhase::observation,
          "Final observation shares the batch deadline and reports a separate budget error");
    (void)mutations(page);
    const auto before_driver_batch=mutations(page);
    const auto bounded_batch=driver.call_with_timeout("batch",Json{
        {"actions",Json::array{Json{{"action","press"},{"key","SLOW"}}}},
        {"maxDurationMs",300000},{"finalObservation",false}},15ms);
    check(!bounded_batch.at("ok").as_bool()&&bounded_batch.at("error").at("code")==Json("budgetExceeded")&&
              bounded_batch.at("actions").at(0).at("execution")==Json("outcomeUnknown"),
          "Bounded driver caps nested batch duration without claiming input was not sent");
    check(mutations(page)==before_driver_batch+1&&
              driver.call("snapshot").at(0).at("timeoutMs").as_int()==original_timeout,
          "Bounded batch applies input once and restores the caller page timeout");
    browser.close();
    auto closed = suitecut::Agent::run(page, Json::array{}, {});
    check(!closed.ok && closed.observation_error->code == suitecut::AgentErrorCode::observation_failed,
          "Final observation failure is separate from action outcomes");
    std::cout << "PASS canonical batch validation, ordering, stop/skip, uncertainty and timeout recovery\n";
    return 0;
  } catch (const std::exception& failure) {
    std::cerr << failure.what() << '\n'; return 1;
  }
}
