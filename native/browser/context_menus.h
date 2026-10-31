// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <map>
#include <utility>
#include <vector>

#include "accepted_task_work.h"
#include "include/cef_browser.h"
#include "include/cef_task.h"

namespace suitecut::browser {

class ContextMenuTask : public CefTask {
 public:
  explicit ContextMenuTask(std::function<void()> fn) : fn_(std::move(fn)) {}
  void Execute() override {
    const suitecut::AcceptedTaskFrame native_frame(native_work_);
    fn_();
  }

 private:
  suitecut::AcceptedTaskReceipt native_work_;  // Captures below destroy first.
  std::function<void()> fn_;
  IMPLEMENT_REFCOUNTING(ContextMenuTask);
};

// A Chrome-style context menu outlives a tab closed while it is showing, and
// CEF's dismissal then runs against the destroyed browser, crashing the
// browser process. Closing such a browser cancels the menu first and closes
// only once the menu has stopped tracking and its own loop has returned.
class ContextMenus {
 public:
  static ContextMenus& Get() {
    static ContextMenus menus;
    return menus;
  }

  // Installed by the platform main: cancels every menu still tracking and
  // calls |ended| once none is.
  std::function<void(std::function<void()> ended)> cancel;

  void Opened(int browser) { open_[browser]; }

  // CEF reports this while the menu is still tracking, so a pending close
  // keeps waiting for the platform instead.
  void Dismissed(int browser) {
    const auto found = open_.find(browser);
    if (found != open_.end() && found->second.empty()) open_.erase(found);
  }

  void Close(const CefRefPtr<CefBrowser>& browser, bool force) {
    const int id = browser->GetIdentifier();
    const auto found = open_.find(id);
    if (found == open_.end() || !cancel) {
      browser->GetHost()->CloseBrowser(force);
      return;
    }
    found->second.emplace_back(browser, force);
    if (found->second.size() > 1) return;
    cancel([id] { CefPostTask(TID_UI, new ContextMenuTask([id] { Get().Release(id); })); });
    // A menu that never stops tracking must not keep the page open.
    CefPostDelayedTask(TID_UI, new ContextMenuTask([id] { Get().Release(id); }), 2000);
  }

 private:
  void Release(int browser) {
    const auto found = open_.find(browser);
    if (found == open_.end()) return;
    auto closes = std::move(found->second);
    open_.erase(found);
    for (auto& [pending, force] : closes) pending->GetHost()->CloseBrowser(force);
  }

  std::map<int, std::vector<std::pair<CefRefPtr<CefBrowser>, bool>>> open_;
};

inline void CloseBrowser(const CefRefPtr<CefBrowser>& browser, bool force) {
  ContextMenus::Get().Close(browser, force);
}

}  // namespace suitecut::browser
