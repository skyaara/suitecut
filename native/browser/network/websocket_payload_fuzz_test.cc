// SPDX-License-Identifier: MIT
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <random>
#include "include/cef_api_hash.h"
#include "network/websocket_payloads.h"
#include <climits>
#include <cstdlib>
#include <new>
// Records the largest C++ allocation so a declared length alone is proven to reserve nothing.
static size_t largest_new = 0;
[[gnu::noinline]] void* operator new(std::size_t size) {
  largest_new = std::max(largest_new, size);
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }
[[gnu::noinline]] void* operator new[](std::size_t size) { return ::operator new(size); }
[[gnu::noinline]] void operator delete[](void* p) noexcept { ::operator delete(p); }
[[gnu::noinline]] void operator delete[](void* p, std::size_t) noexcept { ::operator delete(p); }
int main(int argc, char** argv) {
  assert(cef_api_hash(CEF_API_VERSION, 0));
  const unsigned iterations = argc > 1 ? std::stoul(argv[1]) : 10000;
  std::mt19937 random(0x57a9ed01);
  using Payloads = suitecut::NativeWebSocketPayloads;
  using Dict = Payloads::Dict;
  double now = 1;
  Payloads payloads([&] { return now; });
  size_t actual_commits = 0;
  for (unsigned iteration = 0; iteration < iterations; ++iteration) {
    const int length = 1 + random() % 1024;
    auto begin = CefDictionaryValue::Create();
    begin->SetString("action", "routePayloadBegin");
    begin->SetString("operation", "replace");
    begin->SetInt("opcode", 2);
    begin->SetInt("bytes", length);
    begin->SetString("targetId", "actual-target");
    begin->SetString("routingToken", std::to_string(iteration + 1));
    begin->SetString("requestId", "actual-channel");
    std::string token;
    Payloads::Reply actual;
    auto validate = [](Dict) { return true; };
    auto commit = [&](Dict metadata, double deadline, auto reply) {
      assert(deadline == now + 100);
      auto decoded = CefBase64Decode(metadata->GetString("payloadBase64"));
      assert(decoded && decoded->GetSize() == size_t(length));
      actual = std::move(reply);
      ++actual_commits;
    };
    auto command = [&](Dict input, auto reply, int context = 1) {
      payloads.Command(context, 1, input, now + 100, validate, commit, std::move(reply));
    };
    command(begin, [&](bool ok, Dict result, const auto&) {
      assert(ok);
      token = result->GetString("payloadToken");
    });
    assert(!token.empty() && payloads.Pending());
    auto input = begin->Copy(false);
    input->SetString("payloadToken", token);
    input->SetString("action", "routePayloadCommit");
    command(input, [](bool ok, Dict, const auto&) { assert(!ok); });
    input->SetString("action", "routePayloadAbort");
    command(input, [](bool ok, Dict, const auto&) { assert(!ok); }, 2);
    assert(payloads.Pending());
    input->SetString("routingToken", "stale");
    command(input, [](bool ok, Dict, const auto&) { assert(!ok); });
    input->SetString("routingToken", begin->GetString("routingToken"));
    input->SetString("action", "routePayloadAppend");
    input->SetString("base64", "@@@");
    command(input, [](bool ok, Dict, const auto&) { assert(!ok); });
    std::vector<uint8_t> bytes(length);
    for (auto& byte : bytes) byte = uint8_t(random());
    for (size_t offset = 0; offset < bytes.size();) {
      const auto size = std::min(bytes.size() - offset, size_t(1 + random() % 97));
      input->SetString("base64", CefBase64Encode(bytes.data() + offset, size));
      command(input, [](bool ok, Dict, const auto&) { assert(ok); });
      offset += size;
    }
    switch (random() % 4) {
      case 0:
        input->SetString("action", "routePayloadCommit");
        command(input, [](bool ok, Dict, const auto&) { assert(ok); });
        assert(actual && !payloads.Pending());
        actual(true, nullptr, "");
        actual = {};
        break;
      case 1:
        input->SetString("action", "routePayloadAbort");
        command(input, [](bool ok, Dict, const auto&) { assert(ok); });
        break;
      case 2:
        now += 100;
        payloads.Tick();
        break;
      case 3:
        payloads.Closed(1, 1);
        break;
    }
    assert(!payloads.Pending());
    now += 1;
  }
  {
    // An INT_MAX declaration admits without allocating it; a small chunk allocates only itself.
    auto begin = CefDictionaryValue::Create();
    begin->SetString("action", "routePayloadBegin");
    begin->SetString("operation", "replace");
    begin->SetInt("opcode", 2);
    begin->SetInt("bytes", INT_MAX);
    begin->SetString("targetId", "actual-target");
    begin->SetString("routingToken", "declared-max");
    begin->SetString("requestId", "actual-channel");
    std::string token;
    largest_new = 0;
    const auto validate = [](Dict) { return true; };
    const auto commit = [](Dict, double, Payloads::Reply) { assert(false); };
    payloads.Command(1, 1, begin, now + 100, validate, commit,
                     [&](bool ok, Dict result, const std::string&) {
                       assert(ok);
                       token = result->GetString("payloadToken");
                     });
    assert(!token.empty() && largest_new < (1u << 20));
    auto input = begin->Copy(false);
    input->SetString("payloadToken", token);
    input->SetString("action", "routePayloadAppend");
    const std::vector<uint8_t> chunk(4096, 7);
    input->SetString("base64", CefBase64Encode(chunk.data(), chunk.size()));
    payloads.Command(1, 1, input, now + 100, validate, commit,
                     [](bool ok, Dict, const std::string&) { assert(ok); });
    assert(largest_new < (1u << 20));
    input->SetString("action", "routePayloadAbort");
    payloads.Command(1, 1, input, now + 100, validate, commit,
                     [](bool ok, Dict, const std::string&) { assert(ok); });
    assert(!payloads.Pending());
  }
  std::printf("%u native WebSocket payload schedules passed (seed 0x57a9ed01, %zu real commits)\n",
              iterations, actual_commits);
}
