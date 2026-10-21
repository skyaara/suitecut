// SPDX-License-Identifier: MIT
#include "page/source.h"
namespace suitecut::browser {
auto Source::FailChildBootstrap(const std::string& error) -> void {
  if (closing_page_ || child_bootstrap_failed_) return;
  child_bootstrap_failed_ = true;
  pending_child_bootstraps_.clear();
  Error(error.c_str());
  if (browser_) browser_->GetHost()->CloseBrowser(true);
}

auto Source::WakeChildBootstrapDeadlines() -> void {
  if (child_bootstrap_timer_ || pending_child_bootstraps_.empty() || closing_page_ ||
      child_bootstrap_failed_)
    return;
  child_bootstrap_timer_ = true;
  CefPostDelayedTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] {
                       self->child_bootstrap_timer_ = false;
                       if (self->closing_page_ || self->child_bootstrap_failed_) return;
                       for (const auto& [session, bootstrap] : self->pending_child_bootstraps_) {
                         if (bootstrap.deadline <= Now()) {
                           self->FailChildBootstrap("Native child target initialization timed out");
                           return;
                         }
                       }
                       self->WakeChildBootstrapDeadlines();
                     }),
                     1000);
}

auto Source::ChildBootstrapFinished(const std::string& session, const std::string& frame, bool ok,
                                    const std::string& error) -> void {
  const auto found = pending_child_bootstraps_.find(session);
  if (found == pending_child_bootstraps_.end() || found->second.frame != frame ||
      found->second.phase != 1 || closing_page_ || child_bootstrap_failed_ ||
      !sessions_.contains(session) || sessions_.at(session) != frame)
    return;
  if (!ok) {
    FailChildBootstrap("Native child target initialization failed: " + error);
    return;
  }
  if (--found->second.remaining == 0) {
    found->second.phase = 2;
    WakeChildBootstraps();
  }
}

auto Source::DeferChildBootstrap(const std::string& session, const std::string& frame, bool ignored)
    -> void {
  if (closing_page_ || child_bootstrap_failed_ || !browser_) return;
  if (pending_child_bootstraps_.size() >= 1024 && !pending_child_bootstraps_.contains(session)) {
    FailChildBootstrap("Native child target initialization capacity exceeded");
    return;
  }
  pending_child_bootstraps_.try_emplace(
      session, ChildBootstrap{frame, ignored, ignored ? 2 : 0, 8, Now() + 30000});
  WakeChildBootstraps();
  WakeChildBootstrapDeadlines();
}

auto Source::WakeChildBootstraps() -> void {
  if (child_bootstrap_task_ || pending_child_bootstraps_.empty() || closing_page_ ||
      child_bootstrap_failed_)
    return;
  std::vector<std::string> runnable;
  for (const auto& [session, bootstrap] : pending_child_bootstraps_)
    if (bootstrap.phase != 1) runnable.push_back(session);
  if (runnable.empty()) return;
  child_bootstrap_task_ = true;
  CefPostTask(
      TID_UI, new Task([self = CefRefPtr<Source>(this), runnable = std::move(runnable)] {
        // Fetch.enable changes Chromium's observer lists. Bootstrap after the
        // Target.attachedToTarget notification returns, then wait for real native
        // route/binding acknowledgments before resuming child author execution.
        for (const auto& session : runnable) {
          if (self->closing_page_ || !self->browser_ || self->child_bootstrap_failed_) break;
          const auto found = self->pending_child_bootstraps_.find(session);
          if (found == self->pending_child_bootstraps_.end()) continue;
          const auto frame = found->second.frame;
          if (!found->second.ignored &&
              (!self->sessions_.contains(session) || self->sessions_.at(session) != frame)) {
            self->pending_child_bootstraps_.erase(found);
            continue;
          }
          if (found->second.phase == 2) {
            self->pending_child_bootstraps_.erase(found);
            self->NativePristineChildResumed(session);
            if (!self->SendDevTools("Runtime.runIfWaitingForDebugger", nullptr, session)) {
              self->FailChildBootstrap("CEF rejected native child target resume");
              break;
            }
            continue;
          }
          if (found->second.phase != 0) continue;
          found->second.phase = 1;
          const int tree = self->AllocateDevToolsId();
          if (!tree) {
            self->FailChildBootstrap("Native child frame bootstrap command capacity exceeded");
            break;
          }
          if (!found->second.ignored && !self->NativeFrameDebuggerTreePrepare(tree, session)) break;
          self->EnableSession(session);
          if (self->file_chooser_subscribed_) {
            auto params = CefDictionaryValue::Create();
            params->SetBool("enabled", true);
            self->SendDevTools("Page.setInterceptFileChooserDialog", params, session);
          }
          self->session_trees_[tree] = session;
          if (!self->SubmitDevTools(tree, "Page.getFrameTree", nullptr, session)) {
            self->session_trees_.erase(tree);
            self->FailChildBootstrap("CEF rejected native child frame bootstrap");
            break;
          }
          self->AutoAttach(session);
          const auto coverage_deadline = found->second.deadline;
          auto ready = [self, session, frame](bool ok, const std::string& error) {
            self->ChildBootstrapFinished(session, frame, ok, error);
          };
          if (self->network_)
            self->network_->SessionAttached(session, ready);
          else
            ready(true, "");
          if (self->bindings_)
            self->bindings_->SessionAttached(session, ready);
          else
            ready(true, "");
          self->NativeMediaChild(session, ready);
          if (self->native_idle_)
            self->NativeIdleChild(session, coverage_deadline, ready);
          else
            ready(true, "");
          self->SettingsChild(session, ready);
          self->NativeCoverageChild(session, frame, coverage_deadline, ready);
          self->NativeWebSocketPolicyAttach(session, ready);
          const auto waiting = self->native_pristine_child_waiting_.find(session);
          self->NativePristineChildStart(
              session, frame,
              waiting != self->native_pristine_child_waiting_.end() && waiting->second,
              coverage_deadline, ready);
        }
        self->child_bootstrap_task_ = false;
        self->WakeChildBootstraps();
      }));
}

auto Source::WakeInitSnapshotContinuations() -> void {
  if (init_snapshot_continuation_task_ || init_snapshot_continuations_.empty() || closing_page_)
    return;
  init_snapshot_continuation_task_ = true;
  CefPostTask(TID_UI, new Task([self = CefRefPtr<Source>(this)] {
                auto pending = std::move(self->init_snapshot_continuations_);
                self->init_snapshot_continuations_.clear();
                for (const auto& [params, session] : pending) {
                  if (self->closing_page_ || !self->browser_) break;
                  --self->init_snapshot_continuation_count_;
                  self->SendDevTools("Fetch.continueRequest", params, session);
                }
                self->init_snapshot_continuation_count_ = self->init_snapshot_continuations_.size();
                self->init_snapshot_continuation_task_ = false;
                self->WakeInitSnapshotContinuations();
              }));
}

auto Source::InitSnapshotTraffic(const std::string& method, Dict data, const std::string& session)
    -> bool {
  const auto request = data->GetDictionary("request");
  const bool private_request =
      request && request->GetString("url").ToString().starts_with("suitecut-init:");
  if (method == "Fetch.requestPaused" && private_request) {
    auto params = CefDictionaryValue::Create();
    params->SetString("requestId", data->GetString("requestId"));
    // CEF synchronously notifies DevTools observers. Continuing here can
    // re-enter Chromium's observer list while its original notification is
    // active. One bounded UI task continues after that notification returns.
    if (init_snapshot_continuation_count_ >= 64) {
      Error("Native init snapshot continuation capacity exceeded");
      return true;
    }
    init_snapshot_continuations_.emplace_back(params, session);
    ++init_snapshot_continuation_count_;
    WakeInitSnapshotContinuations();
    return true;
  }
  if (!method.starts_with("Network.")) return false;
  const auto key = session + "\n" + data->GetString("requestId").ToString();
  if (method == "Network.requestWillBeSent" && private_request) {
    if (init_snapshot_requests_.size() >= 1024) {
      Error("Native init snapshot request tracking capacity exceeded");
      return true;
    }
    init_snapshot_requests_.insert(key);
    return true;
  }
  if (!init_snapshot_requests_.contains(key)) return false;
  if (method == "Network.loadingFinished" || method == "Network.loadingFailed")
    init_snapshot_requests_.erase(key);
  return true;
}

}  // namespace suitecut::browser
