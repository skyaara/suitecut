// SPDX-License-Identifier: MIT
#include "shutdown_fence.h"

#include <cassert>
#include <deque>
#include <iostream>

int main() {
  using Fence = suitecut::NativeShutdownFence;
  std::deque<Fence::Work> queue;
  auto post = [&](Fence::Work work) { queue.push_back(std::move(work)); return true; };
  auto run = [&] { auto work = std::move(queue.front()); queue.pop_front(); work(); };
  {
    Fence fence;
    bool closing = false, source_owned = true;
    unsigned physical = 0, quits = 0;
    auto ready = [&] { return closing && !source_owned && physical == 0; };
    auto quit = [&] { ++quits; };
    fence.Request(ready, post, quit);
    assert(queue.empty());
    closing = true;
    // Page count has reached zero inside OnBeforeClose, but its Source is
    // still registered. A logical page-close receipt cannot quit the loop.
    fence.Request(ready, post, quit);
    assert(queue.empty() && quits == 0);
    source_owned = false;
    for (unsigned i = 0; i < 64; ++i) fence.Request(ready, post, quit);
    assert(queue.size() == 1 && quits == 0);
    physical = 1;  // A late physical continuation invalidates queued readiness.
    run();
    assert(queue.empty() && quits == 0);
    physical = 0;
    fence.Request(ready, post, [&] {
      ++quits;
      fence.Request(ready, post, quit);  // Reentrant idle during teardown.
    });
    assert(quits == 0 && queue.size() == 1);
    run();
    for (unsigned i = 0; i < 64; ++i) fence.Request(ready, post, quit);
    assert(quits == 1 && queue.empty());
  }
  {
    Fence fence;
    unsigned quits = 0;
    auto ready = [] { return true; };
    auto quit = [&] { ++quits; };
    fence.Request(ready, [](Fence::Work) { return false; }, quit);
    assert(quits == 0 && queue.empty());
    fence.Request(ready, post, quit);
    assert(quits == 0 && queue.size() == 1);
    run();
    assert(quits == 1 && queue.empty());
  }
  std::cout << "Shutdown waits for Source retirement and callback unwind; late physical work, reentry and rejected posts remain bounded\n";
}
