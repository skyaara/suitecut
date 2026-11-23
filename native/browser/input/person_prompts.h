// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "include/cef_values.h"

// Prompts that go to the person: file choosers, permission requests, HTTP
// authentication and client certificates. UI thread only.
//
// While a window that shows prompts is connected (the session subscribes the
// page), each such request's CEF callback is held here until it is answered,
// the page resets or closes it, or the subscription ends. There is no deadline
// of SuiteCut's own. An unsubscribe answers every held request as today's
// policy would have. With no subscription, nothing is held and callers apply
// today's policy at once.
//
// Events: {"id","kind","state":"opened",...} when held, and
// {"id","kind","state":"closed","reason":R} when it leaves, with R one of
// "answered", "policy" (today's policy answered it), "reset" (the page or
// Chromium dropped the request). An answer's contents are never copied into
// an event, so a password does not leave this object.
class SuiteCutPersonPrompts {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Emit = std::function<void(Dict)>;
  struct Hold {
    // Gives |answer| to CEF and returns "", or returns why the answer does not
    // fit (the request stays held). Never names a secret.
    std::function<std::string(Dict answer)> answer;
    std::function<void()> policy;  // today's answer
    std::function<void()> cancel;  // the request is gone: release its callback
    std::string key;               // matches a dismissal (permission prompt id)
    bool document = false;         // a navigation drops it
  };
  explicit SuiteCutPersonPrompts(Emit emit) : emit_(std::move(emit)) {}
  ~SuiteCutPersonPrompts() { Clear(); }
  SuiteCutPersonPrompts(const SuiteCutPersonPrompts&) = delete;
  SuiteCutPersonPrompts& operator=(const SuiteCutPersonPrompts&) = delete;

  bool On() const { return on_; }
  void Subscribe(bool on) {
    on_ = on;
    if (on) return;
    auto held = std::move(pending_);
    pending_.clear();
    for (auto& [id, state] : held) {
      if (state.hold.policy) state.hold.policy();
      Close(state.info, "policy");
    }
  }
  // Holds the request if a window shows prompts; returns false otherwise.
  bool Show(const std::string& kind, Dict info, Hold hold) {
    if (!on_) return false;
    const auto id = std::to_string(++sequence_);
    info->SetString("id", id);
    info->SetString("kind", kind);
    info->SetString("state", "opened");
    auto summary = CefDictionaryValue::Create();
    summary->SetString("id", id);
    summary->SetString("kind", kind);
    pending_.emplace(id, Pending{std::move(hold), summary});
    emit_(info);
    return true;
  }
  // {"policy":true} answers it as today's policy would.
  bool Answer(const std::string& id, Dict answer, std::string& error) {
    const auto found = pending_.find(id);
    if (found == pending_.end()) {
      error = "The prompt was answered or has closed";
      return false;
    }
    if (answer && answer->GetType("policy") == VTYPE_BOOL && answer->GetBool("policy")) {
      auto state = std::move(found->second);
      pending_.erase(found);
      if (state.hold.policy) state.hold.policy();
      Close(state.info, "policy");
      return true;
    }
    // Out of the map before CEF hears the answer: a permission callback's
    // Continue() calls OnDismissPermissionPrompt at once, which would
    // otherwise find this prompt, cancel it and erase it under us.
    auto state = std::move(found->second);
    pending_.erase(found);
    error = state.hold.answer ? state.hold.answer(answer) : "This prompt takes no answer";
    if (!error.empty()) {
      pending_.emplace(id, std::move(state));  // a refused answer keeps the prompt
      return false;
    }
    Close(state.info, "answered");
    return true;
  }
  // Chromium dropped a request it had asked about (a permission prompt).
  void Dismissed(const std::string& key) {
    for (auto it = pending_.begin(); it != pending_.end(); ++it)
      if (!key.empty() && it->second.hold.key == key) {
        auto state = std::move(it->second);
        pending_.erase(it);
        if (state.hold.cancel) state.hold.cancel();
        Close(state.info, "reset");
        return;
      }
  }
  // A navigation: what belongs to the document goes.
  void Reset() {
    std::vector<Pending> dropped;
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (!it->second.hold.document) {
        ++it;
        continue;
      }
      dropped.push_back(std::move(it->second));
      it = pending_.erase(it);
    }
    for (auto& state : dropped) {
      if (state.hold.cancel) state.hold.cancel();
      Close(state.info, "reset");
    }
  }
  // The page is closing: release every callback, report nothing.
  void Clear() {
    auto held = std::move(pending_);
    pending_.clear();
    for (auto& [id, state] : held)
      if (state.hold.cancel) state.hold.cancel();
  }
  size_t Size() const { return pending_.size(); }

 private:
  struct Pending {
    Hold hold;
    Dict info;
  };
  void Close(Dict info, const char* reason) {
    info->SetString("state", "closed");
    info->SetString("reason", reason);
    emit_(info);
  }
  Emit emit_;
  bool on_ = false;
  std::uint64_t sequence_ = 0;
  std::map<std::string, Pending> pending_;
};

// On a tab the agent holds, its matching network.credentials answer an HTTP
// authentication challenge first, once. The same challenge again before a
// document commits means the server rejected them, so a person is asked
// instead, and asked again while their answers are rejected. UI thread only.
class SuiteCutAgentAuthTries {
 public:
  static std::string Key(bool proxy, const std::string& host, int port, const std::string& realm,
                         const std::string& scheme) {
    return std::string(proxy ? "proxy|" : "server|") + host + "|" + std::to_string(port) + "|" +
           scheme + "|" + realm;
  }
  // Whether the agent's credentials have not yet been tried on this challenge.
  bool Untried(const std::string& key) const { return !tried_.contains(key); }
  void Tried(const std::string& key) { tried_.insert(key); }
  // A main-frame document committed: what was tried no longer says anything.
  void Reset() { tried_.clear(); }

 private:
  std::set<std::string> tried_;
};
