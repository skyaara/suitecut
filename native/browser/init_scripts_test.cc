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
  assert(error.find("NUL") != std::string::npos);
  assert(scripts.Bytes() == 0 && scripts.Version() == 0);
  // Larger than the old 64 KiB per-script cap; only the 64 MiB total remains.
  for (int i = 1; i <= 128; ++i) {
    assert(scripts.Add(std::string(65537, 'a'), error));
    assert(scripts.Version() == i && scripts.Entries().back().id == i);
  }
  assert(scripts.Bytes() == 128 * 65537);
  auto copy = scripts; scripts = SuiteCutInitScripts();
  assert(copy.Version() == 128 && scripts.Version() == 0);
  SuiteCutInitScripts full;
  assert(full.Add(std::string(SuiteCutInitScripts::kMaxBytes, 'a'), error));
  assert(!full.Add("x", error) && error.find("64 MiB") != std::string::npos);
  uint64_t seed = 0x17294031;
  for (int cycle = 0; cycle < 200; ++cycle) {
    SuiteCutInitScripts registry; size_t expected = 0;
    for (int i = 0; i < 24; ++i) {
      seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
      const size_t size = 1 + seed % (SuiteCutInitScripts::kMaxBytes / 12);
      const bool accepted = registry.Add(std::string(size, 'b'), error);
      assert(accepted == (expected + size <= SuiteCutInitScripts::kMaxBytes));
      if (accepted) expected += size;
      assert(registry.Bytes() == expected);
    }
  }
  std::cout << "Native init script ownership: 4800 registrations up to the 64 MiB total, copies, resets passed\n";
}
