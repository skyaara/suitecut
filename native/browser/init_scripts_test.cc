// SPDX-License-Identifier: MIT
#include "init_scripts.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
  SuiteCutInitScripts scripts; std::string error;
  assert(!scripts.Add("", error));
  assert(!scripts.Add(std::string("a\0b", 3), error));
  assert(!scripts.Add(std::string(65537, 'a'), error));
  assert(scripts.Bytes() == 0 && scripts.Version() == 0);
  for (int i = 1; i <= 128; ++i) {
    assert(scripts.Add(std::string(8192, 'a'), error));
    assert(scripts.Version() == i && scripts.Entries().back().id == i);
  }
  assert(scripts.Bytes() == 1048576);
  assert(!scripts.Add("x", error));
  auto copy = scripts; scripts = SuiteCutInitScripts();
  assert(copy.Version() == 128 && scripts.Version() == 0);
  uint64_t seed = 0x17294031;
  for (int cycle = 0; cycle < 10000; ++cycle) {
    SuiteCutInitScripts registry; size_t expected = 0;
    for (int i = 0; i < 24; ++i) {
      seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
      const size_t size = 1 + seed % 65536;
      const bool accepted = registry.Add(std::string(size, 'b'), error);
      assert(accepted == (expected + size <= 1048576));
      if (accepted) expected += size;
      assert(registry.Bytes() == expected);
    }
  }
  std::cout << "Native init script ownership: 240000 bounded registrations, copies, resets passed\n";
}
