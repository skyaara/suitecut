// SPDX-License-Identifier: MIT
#include "init/init_scripts.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
int main() {
  SuiteCutInitScripts scripts; std::string error;
  assert(!scripts.Add("", error));
  assert(!scripts.Add(std::string("a\0b", 3), error));
  assert(error.find("NUL") != std::string::npos);
  assert(scripts.Bytes() == 0 && scripts.Version() == 0);
  // Larger than the old 64 KiB per-script cap and past the old 64 MiB total:
  // there is no size or count limit, every script is kept whole.
  for (int i = 1; i <= 128; ++i) {
    assert(scripts.Add(std::string(65537, 'a'), error));
    assert(scripts.Version() == i && scripts.Entries().back().id == i);
  }
  assert(scripts.Bytes() == 128 * 65537);
  auto copy = scripts; scripts = SuiteCutInitScripts();
  assert(copy.Version() == 128 && scripts.Version() == 0);
  SuiteCutInitScripts full; const size_t old_total = size_t(64) << 20;
  assert(full.Add(std::string(old_total, 'a'), error));
  assert(full.Add("x", error) && full.Bytes() == old_total + 1 && full.Entries().back().source == "x");
  uint64_t seed = 0x17294031;
  for (int cycle = 0; cycle < 50; ++cycle) {
    SuiteCutInitScripts registry; size_t expected = 0;
    for (int i = 0; i < 24; ++i) {
      seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
      const size_t size = 1 + seed % 65536;
      assert(registry.Add(std::string(size, 'b'), error));
      expected += size;
      assert(registry.Bytes() == expected && registry.Version() == i + 1);
    }
  }
  std::cout << "Native init script ownership: unbounded registrations past the old 64 MiB total, copies, resets passed\n";
}
