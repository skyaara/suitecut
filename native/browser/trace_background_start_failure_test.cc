// SPDX-License-Identifier: MIT
// Linux executable interposition exercises genuine std::thread failures.
#include "trace_background.h"
#include <atomic>
#include <cassert>
#include <cerrno>
#include <dlfcn.h>
#include <future>
#include <pthread.h>

namespace {
std::atomic_int refuse_threads{0};
using CreateThread = int (*)(pthread_t*, const pthread_attr_t*, void* (*)(void*), void*);
}

extern "C" int pthread_create(pthread_t* thread, const pthread_attr_t* attributes,
                              void* (*entry)(void*), void* argument) noexcept {
  if (refuse_threads.exchange(0)) return EAGAIN;
  static const auto create = reinterpret_cast<CreateThread>(dlsym(RTLD_NEXT, "pthread_create"));
  if (!create) std::abort();
  return create(thread, attributes, entry, argument);
}

int main() {
  using namespace std::chrono_literals;
  suitecut::TraceBackground pool(2, 4);
  refuse_threads = 1;
  assert(!pool.Submit([] {}));
  assert(pool.PhysicalCount() == 0 && pool.WorkerCount() == 0);
  assert(pool.WorkerStartFailures() == 1);

  std::promise<void> started, release, drained;
  const auto gate = release.get_future().share();
  assert(pool.Submit([&] { started.set_value(); gate.wait(); }));
  assert(started.get_future().wait_for(2s) == std::future_status::ready);
  // Growth failure must not discard work that an existing worker can drain.
  refuse_threads = 1;
  assert(pool.Submit([&] { drained.set_value(); }));
  assert(pool.WorkerCount() == 1 && pool.PhysicalCount() == 2);
  assert(pool.WorkerStartFailures() == 2 && pool.Failures() == 0);
  release.set_value();
  assert(drained.get_future().wait_for(2s) == std::future_status::ready);
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (pool.PhysicalCount() && std::chrono::steady_clock::now() < deadline)
    std::this_thread::yield();
  assert(pool.PhysicalCount() == 0);
}
