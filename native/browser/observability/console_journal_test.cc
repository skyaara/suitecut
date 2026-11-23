// SPDX-License-Identifier: MIT
#include "observability/console_journal.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <random>

int main() {
  SuiteCutConsoleJournal journal;
  assert(!journal.Push(0, "{}"));
  assert(!journal.Push(1, ""));
  assert(journal.Push(1, std::string(1048577, 'x')));
  journal.Clear();
  assert(journal.Push(1, "before"));
  journal.Navigation(1);
  assert(journal.Push(2, "after"));
  assert(journal.Read(false) == std::vector<std::string>{"after"});
  assert(journal.Read(true).size() == 2);
  journal.Clear();
  assert(journal.Size() == 0 && journal.Bytes() == 0);
  for (std::uint64_t index = 1; index <= 301; ++index)
    assert(journal.Push(index, std::to_string(index)));
  const auto all = journal.Read(true);
  assert(all.size() == 301 && all.front() == "1" && all.back() == "301");
  journal.Clear();
  const std::string large(1048576, 'x');
  for (std::uint64_t index = 1; index <= 8; ++index) assert(journal.Push(index, large));
  assert(journal.Size() == 8 && journal.Bytes() == 8 * large.size() && journal.Oldest() == 1);
  journal.Clear();
  std::mt19937_64 random(0xc07501e);
  for (std::uint64_t index = 1; index <= 100000; ++index) {
    if (random() % 127 == 0) journal.Clear();
    if (random() % 11 == 0) journal.Navigation(index - 1);
    assert(journal.Push(index, std::string(random() % 1024 + 1, 'x')));
    assert(journal.Bytes() >= journal.Size());
  }
  journal.Clear();
  assert(journal.Size() == 0 && journal.Bytes() == 0);
}
