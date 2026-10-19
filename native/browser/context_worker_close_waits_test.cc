// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "context_worker_close_waits.h"
#include <cassert>
#include <limits>
#include <memory>
#include <string>

int main() {
  using Waits = suitecut::NativeContextWorkerCloseWaits;
  Waits waits;
  int calls = 0;
  auto success = [&](bool ok, uint64_t cursor, const std::string& error) {
    assert(ok && cursor == 23 && error.empty()); ++calls;
  };
  assert(waits.Admit(1, 2, 3, 100, 1, success));
  waits.Closed(2, 4, 23, 2, true); // Another physical owner is not this receipt.
  waits.Closed(3, 3, 23, 2, true);
  assert(calls == 0 && waits.size() == 1);
  waits.Closed(2, 3, 23, 99, true);
  assert(calls == 1 && waits.size() == 0);
  waits.Closed(2, 3, 23, 99, true); assert(calls == 1);

  assert(waits.Admit(2, 2, 4, 100, 1, [&](bool ok, uint64_t, const std::string& error) {
    assert(!ok && error.find("timed out") != std::string::npos); ++calls;
  }));
  waits.Closed(2, 4, 23, 100, true); assert(calls == 2);
  assert(waits.Admit(3, 2, 5, 100, 1, success));
  waits.Cancel(3); waits.Closed(2, 5, 23, 2, true); assert(calls == 2);
  assert(waits.Admit(4, 2, 6, 0, 1, success));
  waits.Closed(2, 6, 23, 100000, true); assert(calls == 3);

  assert(!waits.Admit(0, 2, 1, 100, 1, success));
  assert(!waits.Admit(1, 2, 0, 100, 1, success));
  assert(!waits.Admit(1, 2, 1, -1, 1, success));
  assert(!waits.Admit(1, 2, 1, std::numeric_limits<double>::infinity(), 1, success));
  assert(!waits.Admit(1, 2, 1, 1, 1, success));
  for (int owner = 1; owner <= int(Waits::kWaits); ++owner)
    assert(waits.Admit(owner, 2, 7, 100, 1, [](bool ok, uint64_t, const std::string& error) {
      assert(!ok && error.find("disposed") != std::string::npos);
    }));
  assert(!waits.Admit(65, 2, 7, 100, 1, success));
  assert(!waits.Admit(1, 2, 7, 100, 1, success));
  waits.Clear(); assert(waits.size() == 0);

  assert(waits.Admit(1, 2, 8, 100, 1, [](bool ok, uint64_t, const std::string& error) {
    assert(!ok && error.find("cursor rejected") != std::string::npos);
  }));
  waits.Closed(2, 8, 23, 2, false);

  // Extraction permits cancellation/new observers from a completion callback.
  assert(waits.Admit(1, 2, 9, 100, 1, [&](bool ok, uint64_t, const std::string&) {
    assert(ok && waits.size() == 0);
    assert(waits.Admit(1, 2, 10, 100, 2, success));
  }));
  waits.Closed(2, 9, 23, 2, true); assert(waits.size() == 1);
  waits.Closed(2, 10, 23, 3, true); assert(calls == 4);

  auto owned = std::make_unique<Waits>();
  for (int id = 1; id <= 2; ++id)
    assert(owned->Admit(id, 2, 11, 100, 1, [&](bool ok, uint64_t, const std::string&) {
      assert(ok); owned.reset(); ++calls;
    }));
  owned->Closed(2, 11, 23, 2, true); assert(!owned && calls == 6);
}
