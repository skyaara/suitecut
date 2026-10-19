// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include "context_worker_cursor.h"

int main() {
  using Book = suitecut::NativeContextWorkerCursors;
  Book book;
  const auto first = book.Acquire(1); assert(first && first->initial == 0);
  assert(!book.Acquire(1));
  assert(!book.ContextGone(1));
  auto sequence = first->initial; assert(Book::Next(sequence) && sequence == 1);
  assert(Book::Next(sequence) && sequence == 2);
  assert(book.Closed(*first, sequence));
  const auto second = book.Acquire(1); assert(second && second->initial == 2 && second->token != first->token);
  assert(!book.Closed(*first, 99)); // Old real receipt cannot close a replacement.
  assert(!book.Uncreated(*first));
  assert(!book.Acquire(1));
  sequence = second->initial; assert(Book::Next(sequence) && sequence == 3);
  assert(book.Closed(*second, sequence));
  const auto rejected = book.Acquire(1); assert(rejected && rejected->initial == 3);
  assert(book.Uncreated(*rejected));
  const auto retry = book.Acquire(1); assert(retry && retry->initial == 3);
  assert(book.Closed(*retry, 3));
  assert(book.ContextGone(1) && book.size() == 0);

  for (int context = 1; context <= int(Book::kContexts); ++context) {
    const auto lease = book.Acquire(context); assert(lease); assert(book.Uncreated(*lease));
  }
  assert(book.size() == Book::kContexts && !book.Acquire(33));
  assert(book.ContextGone(2)); assert(book.Acquire(33));
  assert(book.size() == Book::kContexts);

  Book bounded;
  const auto lease = bounded.Acquire(7); assert(lease);
  assert(bounded.Closed(*lease, Book::kMaxSequence));
  assert(!bounded.Acquire(7));
  sequence = Book::kMaxSequence - 1;
  assert(!Book::Next(sequence) && sequence == Book::kMaxSequence - 1);
  assert(Book::Next(sequence, true) && sequence == Book::kMaxSequence);
  assert(!Book::Next(sequence) && sequence == Book::kMaxSequence);
  sequence = std::numeric_limits<uint64_t>::max();
  assert(!Book::Next(sequence) && sequence == std::numeric_limits<uint64_t>::max());
  assert(bounded.ContextGone(7));
  const auto invalid = bounded.Acquire(8); assert(invalid);
  assert(!bounded.Closed(*invalid, Book::kMaxSequence + 1));
  assert(!bounded.Acquire(8));
  assert(bounded.ContextGone(8));
  const auto stale = bounded.Acquire(9); assert(stale); assert(bounded.Closed(*stale, 12));
  const auto newer = bounded.Acquire(9); assert(newer && newer->initial == 12);
  assert(!bounded.Closed(*newer, 11));
  assert(!bounded.Acquire(9));
  assert(bounded.ContextGone(9));

  Book reserved;
  const auto original = reserved.Acquire(10); assert(original);
  assert(reserved.Closed(*original, Book::kAdmissionSequence - 1));
  const auto last = reserved.Acquire(10); assert(last && last->initial == Book::kAdmissionSequence - 1);
  sequence = last->initial;
  assert(Book::Next(sequence) && sequence == Book::kAdmissionSequence);
  assert(!Book::CanNext(sequence) && !Book::Next(sequence));
  for (uint64_t i = 0; i < Book::kRetirementSequences; ++i) assert(Book::Next(sequence, true));
  assert(sequence == Book::kMaxSequence && !Book::Next(sequence, true));
  assert(!reserved.Acquire(10)); // A live physical owner cannot be replaced.
  assert(reserved.Closed(*last, sequence));
  assert(!reserved.Acquire(10)); // Exhausted history cannot be silently reset.
  assert(reserved.ContextGone(10));
  const auto new_context = reserved.Acquire(10); assert(new_context && new_context->initial == 0);
}
