// SPDX-License-Identifier: MIT
#pragma once
#include <functional>
#include <map>
#include <string>
#include <cstdint>
#include <utility>
#include <vector>
#include <deque>
#include "include/cef_jsdialog_handler.h"
#include "include/cef_values.h"

// UI-thread-only ownership of genuine CEF dialog callbacks. The embedder's
// single deadline pump also expires unanswered dialogs; navigation resets them.
class SuiteCutDialogControls {
 public:
  using Dict = CefRefPtr<CefDictionaryValue>;
  using Emit = std::function<void(Dict)>;
  using Decision = std::function<void(bool)>;
  explicit SuiteCutDialogControls(Emit emit) : emit_(std::move(emit)) {}
  void Subscribe(bool enabled) {
    subscribed_ = enabled;
    if (!enabled) Clear(true);
  }
  bool Show(const std::string& type, const std::string& url,
            const std::string& message, const std::string& default_value,
            CefRefPtr<CefJSDialogCallback> callback, double now, Decision decision = {}) {
    if (!subscribed_) return false;
    if (pending_.size() >= 64 || message.size() > 524288 || default_value.size() > 262144 || url.size() > 8192) {
      if(decision)decision(false);
      callback->Continue(false, CefString()); return true;
    }
    const auto id = std::to_string(++sequence_);
    auto event = CefDictionaryValue::Create(); event->SetString("id", id);
    event->SetString("type", type); event->SetString("url", url);
    event->SetString("message", message); event->SetString("defaultValue", default_value);
    event->SetString("state", "opened");
    pending_.emplace(id, Pending{callback, now + 30000, event,std::move(decision)});
    emit_(event); return true;
  }
  bool Answer(const std::string& id, bool accept, const std::string& prompt, std::string& error) {
    const auto found = pending_.find(id);
    if (found == pending_.end()) { error = "Dialog is already handled or was canceled"; return false; }
    auto callback = found->second.callback;
    auto decision=std::move(found->second.decision);
    closing_.push_back(found->second.info); pending_.erase(found);
    if(decision)decision(accept);
    callback->Continue(accept, prompt); return true;
  }
  void Closed() {
    if (closing_.empty()) return;
    auto info = closing_.front(); closing_.pop_front();
    info->SetString("state", "closed"); emit_(info);
  }
  void Tick(double now) {
    std::vector<Pending> expired;
    for (auto it = pending_.begin(); it != pending_.end();) {
      if (now >= it->second.deadline) { closing_.push_back(it->second.info); expired.push_back(std::move(it->second)); it = pending_.erase(it); }
      else ++it;
    }
    for (const auto& state : expired){if(state.decision)state.decision(false);state.callback->Continue(false, CefString());}
  }
  void Clear(bool answer = false) {
    auto pending = std::move(pending_); pending_.clear();
    closing_.clear();
    for (const auto& [id, state] : pending){if(state.decision)state.decision(false);if(answer)state.callback->Continue(false, CefString());}
  }
  size_t Size() const { return pending_.size(); }
  bool AcceptedWorkReady()const noexcept{return pending_.empty()&&closing_.empty();}
 private:
  struct Pending { CefRefPtr<CefJSDialogCallback> callback; double deadline; Dict info; Decision decision; };
  Emit emit_; bool subscribed_ = false; uint64_t sequence_ = 0;
  std::map<std::string, Pending> pending_;
  std::deque<Dict> closing_;
};
