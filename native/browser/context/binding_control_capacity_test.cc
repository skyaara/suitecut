#include "work/operation_registry.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
int main() {
  SuiteCutOperationRegistry r;
  // No operation count limit: past the old 64 ordinary + 64 control bounds
  // every identity is admitted; only a duplicate live id is refused.
  for (int i = 1; i <= 1000; ++i) assert(r.Begin(i, 0, 100));
  for (int i = 2001; i <= 3000; ++i) assert(r.BeginControl(i, 0, 100));
  for (int i = 101; i <= 164; ++i) assert(r.Finish(i) && r.BeginControl(i, 0, 100));
  assert(r.Size() == 2000 && r.Controls() == 1064);
  assert(!r.BeginControl(1, 0, 100));
  assert(!r.Begin(2001, 0, 100));
  const auto old = r.Get(101);
  assert(old);
  assert(r.Finish(101));
  assert(r.BeginControl(101, 5, 100));
  assert(!r.IsCurrent(101, old->generation));
  assert(r.Expired(100).size() == 1999);
  assert(r.Finish(1));
  assert(r.Begin(1, 5, 100));
  r.Clear();
  assert(r.Size() == 0 && r.Controls() == 0);
  assert(r.Begin(1, 0, 100));
  assert(r.BeginControl(2, 0, 100));
}
