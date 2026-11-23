// SPDX-License-Identifier: MIT
#include "context/context_permission_scopes.h"
#include <cassert>
#include <string>
int main() {
  using Ledger = suitecut::NativePermissionScopes<int>;
  Ledger pairs;
  assert(pairs.Observe("first", "first"));
  assert(pairs.entries().size() == 1);
  assert(pairs.Observe("second", "first"));
  assert(pairs.entries().size() == 3);
  assert(pairs.Observe("second", "first"));
  assert(pairs.entries().size() == 3);
  assert(pairs.Observe("first", "second"));
  assert(pairs.entries().size() == 4);
  Ledger many;
  for (int i = 0; i < 5000; ++i) assert(many.Observe("origin" + std::to_string(i), "top"));
  assert(many.entries().size() == 10000);
  for (int i = 0; i < 100000; ++i) {
    Ledger cycle;
    assert(cycle.Observe("first", ""));
    assert(cycle.Observe("second", "first"));
    assert(cycle.Observe("third", "first"));
    assert(cycle.entries().size() == 5);
    for (const auto& [scope, value] : cycle.entries()) {
      assert(!scope.first.empty() && !scope.second.empty());
      assert(value == 0);
    }
  }
}
