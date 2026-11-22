// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "person_prompts.h"
#include "include/cef_api_hash.h"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {
using Dict = SuiteCutPersonPrompts::Dict;
struct Calls { int answered = 0, policy = 0, cancel = 0; std::string last; };
SuiteCutPersonPrompts::Hold Holding(Calls& calls, bool document = false, std::string key = {}) {
  SuiteCutPersonPrompts::Hold hold;
  hold.answer = [&calls](Dict answer) -> std::string {
    if (!answer || answer->GetType("value") != VTYPE_STRING) return "An answer has a value";
    ++calls.answered;
    calls.last = answer->GetString("value");
    return "";
  };
  hold.policy = [&calls] { ++calls.policy; };
  hold.cancel = [&calls] { ++calls.cancel; };
  hold.document = document;
  hold.key = std::move(key);
  return hold;
}
Dict Answer(const char* key, const char* value) {
  auto answer = CefDictionaryValue::Create();
  answer->SetString(key, value);
  return answer;
}
}  // namespace

int main() {
  assert(cef_api_hash(CEF_API_VERSION, 0));
  std::vector<Dict> events;
  SuiteCutPersonPrompts prompts([&](Dict event) { events.push_back(event->Copy(false)); });
  Calls calls;
  // Not subscribed: nothing is held; the caller applies today's policy.
  assert(!prompts.Show("file", CefDictionaryValue::Create(), Holding(calls)) && events.empty());
  prompts.Subscribe(true);
  auto info = CefDictionaryValue::Create();
  info->SetString("mode", "open");
  assert(prompts.Show("file", info, Holding(calls)) && events.size() == 1);
  assert(events[0]->GetString("state") == "opened" && events[0]->GetString("kind") == "file" &&
         events[0]->GetString("mode") == "open");
  const auto first = events[0]->GetString("id").ToString();
  std::string error;
  // An answer that does not fit keeps the prompt.
  assert(!prompts.Answer(first, Answer("other", "x"), error) && error == "An answer has a value" && prompts.Size() == 1);
  // The answer's contents never reach an event.
  assert(prompts.Answer(first, Answer("value", "s3cret"), error) && calls.answered == 1 && calls.last == "s3cret");
  assert(events.size() == 2 && events[1]->GetString("state") == "closed" && events[1]->GetString("reason") == "answered" &&
         events[1]->GetString("id") == first && !events[1]->HasKey("value") && !events[1]->HasKey("mode"));
  assert(!prompts.Answer(first, Answer("value", "again"), error) && error == "The prompt was answered or has closed");
  // {"policy":true} is today's answer.
  assert(prompts.Show("auth", CefDictionaryValue::Create(), Holding(calls)));
  auto policy = CefDictionaryValue::Create();
  policy->SetBool("policy", true);
  assert(prompts.Answer(events.back()->GetString("id"), policy, error) && calls.policy == 1);
  assert(events.back()->GetString("reason") == "policy");
  // A navigation drops document prompts only; a dismissal finds its key.
  assert(prompts.Show("file", CefDictionaryValue::Create(), Holding(calls, true)));
  assert(prompts.Show("permission", CefDictionaryValue::Create(), Holding(calls, false, "permission:7")));
  assert(prompts.Show("certificate", CefDictionaryValue::Create(), Holding(calls)));
  const auto before = events.size();
  prompts.Reset();
  assert(events.size() == before + 1 && events.back()->GetString("kind") == "file" &&
         events.back()->GetString("reason") == "reset" && calls.cancel == 1 && prompts.Size() == 2);
  prompts.Dismissed("permission:8");
  assert(prompts.Size() == 2);
  prompts.Dismissed("permission:7");
  assert(prompts.Size() == 1 && events.back()->GetString("kind") == "permission" && calls.cancel == 2);
  // The last window leaving answers what is held as today's policy.
  prompts.Subscribe(false);
  assert(prompts.Size() == 0 && calls.policy == 2 && events.back()->GetString("kind") == "certificate" &&
         events.back()->GetString("reason") == "policy");
  assert(!prompts.Show("file", CefDictionaryValue::Create(), Holding(calls)));
  // A closing page releases callbacks and reports nothing.
  prompts.Subscribe(true);
  assert(prompts.Show("auth", CefDictionaryValue::Create(), Holding(calls)));
  const auto count = events.size();
  prompts.Clear();
  assert(events.size() == count && calls.cancel == 3 && calls.policy == 2);
  // CEF dismisses a permission prompt from inside its answer (Continue calls
  // OnDismissPermissionPrompt at once): it closes once, answered, uncancelled.
  Calls reentered;
  auto hold = Holding(reentered, false, "permission:9");
  hold.answer = [&prompts, &reentered](Dict answer) -> std::string {
    if (!answer || answer->GetType("value") != VTYPE_STRING) return "An answer has a value";
    ++reentered.answered;
    prompts.Dismissed("permission:9");
    return "";
  };
  assert(prompts.Show("permission", CefDictionaryValue::Create(), std::move(hold)));
  const auto asked = events.back()->GetString("id").ToString();
  const auto shown = events.size();
  assert(!prompts.Answer(asked, Answer("other", "x"), error) && prompts.Size() == 1 && events.size() == shown);
  assert(prompts.Answer(asked, Answer("value", "allow"), error) && reentered.answered == 1 && reentered.cancel == 0);
  assert(prompts.Size() == 0 && events.size() == shown + 1 && events.back()->GetString("reason") == "answered");
  prompts.Clear();
  // The agent's credentials: one try per challenge until a document commits.
  SuiteCutAgentAuthTries tries;
  const auto key = SuiteCutAgentAuthTries::Key(false, "box.test", 443, "Box", "basic");
  assert(tries.Untried(key));
  tries.Tried(key);
  assert(!tries.Untried(key));
  assert(tries.Untried(SuiteCutAgentAuthTries::Key(true, "box.test", 443, "Box", "basic")));
  assert(tries.Untried(SuiteCutAgentAuthTries::Key(false, "box.test", 443, "Other", "basic")));
  tries.Reset();
  assert(tries.Untried(key));
  std::cout << "person_prompts_test: all checks passed\n";
  return 0;
}
