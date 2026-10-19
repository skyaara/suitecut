// SPDX-License-Identifier: MIT
#include "command_framer.h"
#include "command_credits.h"
#include "operation_registry.h"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include <thread>
#include <latch>

void Check(bool condition) { if (!condition) std::abort(); }

int main(int argc, char** argv) {
  const unsigned iterations = argc > 1 ? std::stoul(argv[1]) : 10000;
  SuiteCutOperationRegistry operations;
  {
    SuiteCutCommandCredits credits(4);
    std::atomic<bool> canceled{false};
    std::atomic<int> active{0};
    std::latch start(8);
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 8; ++worker) workers.emplace_back([&] {
      start.arrive_and_wait();
      for (int i = 0; i < 1000; ++i) {
        Check(credits.Reserve(canceled));
        Check(active.fetch_add(1) < 4);
        std::this_thread::yield();
        Check(active.fetch_sub(1) > 0);
        credits.Release();
      }
    });
    for (auto& worker : workers) worker.join();
    Check(active == 0);
    credits.Stop();
    Check(!credits.Reserve(canceled));
  }
  {
    SuiteCutCommandCredits credits(1);
    std::atomic<bool> canceled{false};
    Check(credits.Reserve(canceled));
    std::latch started(1);
    std::thread waiter([&] { started.count_down(); Check(!credits.Reserve(canceled)); });
    started.wait();
    canceled = true;
    credits.Wake();
    waiter.join();
    credits.Release();
  }
  {
    SuiteCutCommandCredits credits(1);
    std::atomic<bool> canceled{false};
    Check(credits.Reserve(canceled));
    std::latch started(1);
    std::thread waiter([&] { started.count_down(); Check(!credits.Reserve(canceled)); });
    started.wait();
    credits.Stop();
    waiter.join();
    credits.Release();
  }
  const auto first = operations.Begin(1, 10, 20);
  Check(first.has_value());
  Check(!operations.Begin(1, 10, 20));
  Check(!operations.Begin(0, 10, 20));
  Check(!operations.IsExpired(1, first->generation, 29));
  Check(operations.IsExpired(1, first->generation, 30));
  Check(operations.Expired(29).empty());
  Check(operations.Expired(30) == std::vector<int>{1});
  Check(operations.Finish(1));
  const auto reused = operations.Begin(1, 40, 0);
  Check(reused && !operations.IsCurrent(1, first->generation));
  Check(!operations.IsExpired(1, reused->generation, 100000));
  operations.Clear();
  for (int id = 1; id <= 64; ++id) Check(operations.Begin(id, 0, 10).has_value());
  Check(!operations.Begin(65, 0, 10));
  operations.Clear();

  for (const std::size_t length : {0U, 1U, 65535U, 65536U, 65537U}) {
    SuiteCutCommandFramer framer;
    std::size_t delivered = 0;
    const bool accepted = framer.Push(std::string(length, 'x') + '\n', [&](std::string line) {
      Check(line.size() == length); ++delivered; return true;
    });
    Check(accepted == (length < 65536));
    Check(delivered == (accepted ? 1 : 0));
    Check(framer.End() == accepted);
    if (!accepted) Check(!framer.Push("valid\n", [](std::string) { return true; }));
  }
  {
    SuiteCutCommandFramer framer;
    Check(framer.Push("unfinished", [](std::string) { return true; }));
    Check(!framer.End());
  }
  {
    SuiteCutCommandFramer framer;
    Check(!framer.Push(std::string_view("a\0b\n", 4), [](std::string) { return true; }));
    Check(!framer.End());
  }
  {
    SuiteCutCommandFramer framer;
    Check(!framer.Push("first\nsecond\n", [](std::string) { return false; }));
    Check(!framer.End());
  }

  std::mt19937 random(0x5c17c0de);
  for (unsigned iteration = 0; iteration < iterations; ++iteration) {
    SuiteCutCommandFramer framer;
    std::vector<std::string> expected, actual;
    std::string stream;
    const unsigned count = random() % 12;
    for (unsigned i = 0; i < count; ++i) {
      std::string line;
      const unsigned length = random() % 1024;
      for (unsigned j = 0; j < length; ++j) {
        char byte;
        do { byte = static_cast<char>(random()); } while (byte == '\0' || byte == '\n');
        line += byte;
      }
      expected.push_back(line);
      stream += line + '\n';
    }
    for (std::size_t offset = 0; offset < stream.size();) {
      const auto size = std::min<std::size_t>(stream.size() - offset, 1 + random() % 71);
      Check(framer.Push(std::string_view(stream).substr(offset, size), [&](std::string line) {
        actual.push_back(std::move(line)); return true;
      }));
      Check(framer.BufferedBytes() < SuiteCutCommandFramer::kMaxLineBytes);
      offset += size;
    }
    Check(framer.End() && actual == expected);
    const int id = 1 + random() % 64;
    const auto operation = operations.Begin(id, iteration, 1);
    Check(operation.has_value());
    Check(operations.IsExpired(id, operation->generation, iteration + 1));
    Check(operations.Finish(id) && operations.Size() == 0);
  }
  std::printf("Native control checks and %u deterministic fuzz cases passed (seed 0x5c17c0de)\n", iterations);
}
