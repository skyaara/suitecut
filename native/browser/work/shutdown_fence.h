// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <utility>

namespace suitecut {

// UI-thread owned. Post must enqueue work, never invoke it inline: the last
// close callback must return before Chromium begins process teardown.
class NativeShutdownFence {
 public:
  using Work = std::function<void()>;
  using Ready = std::function<bool()>;
  using Post = std::function<bool(Work)>;

  void Request(Ready ready, const Post& post, Work quit) {
    if (queued_ || committed_ || !ready()) return;
    queued_ = true;
    if (!post([this, ready = std::move(ready), quit = std::move(quit)] {
          queued_ = false;
          if (committed_ || !ready()) return;
          committed_ = true;
          quit();
        })) queued_ = false;
  }

 private:
  bool queued_ = false;
  bool committed_ = false;
};

}  // namespace suitecut
